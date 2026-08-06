#include "code_generator.h"

#include <algorithm>
#include <cstdint>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>

namespace toycc {

namespace {

constexpr int kWordBytes = 4;
constexpr int kStackAlignmentBytes = 16;

int alignTo(int value, int alignment) {
  const int remainder = value % alignment;
  if (remainder == 0) {
    return value;
  }
  return value + alignment - remainder;
}

// Hacker's Delight magc：计算正数除数 d（2 <= d <= 2^31-1，非 2 的幂）的
// magic number M 与 shift s，使 q = (mulh(x,M) [+ x]) >> s - (x>>31) 等于 x/d。
bool computeSignedDivMagic(int32_t d, uint32_t& magic, int32_t& shift) {
  if (d <= 1 || d == INT32_MIN) {
    return false;
  }
  const uint32_t ad = static_cast<uint32_t>(d);
  if ((ad & (ad - 1)) == 0) {
    return false; // 2 的幂走移位路径
  }
  constexpr uint32_t two31 = 0x80000000u;
  const uint32_t anc = two31 - 1 - two31 % ad;
  int p = 31;
  uint32_t q1 = two31 / anc;
  uint32_t r1 = two31 - q1 * anc;
  uint32_t q2 = two31 / ad;
  uint32_t r2 = two31 - q2 * ad;
  uint32_t delta;
  do {
    ++p;
    q1 *= 2;
    r1 *= 2;
    if (r1 >= anc) {
      ++q1;
      r1 -= anc;
    }
    q2 *= 2;
    r2 *= 2;
    if (r2 >= ad) {
      ++q2;
      r2 -= ad;
    }
    delta = ad - r2;
  } while (q1 < delta || (q1 == delta && r1 == 0));
  magic = q2 + 1;
  shift = p - 32;
  return true;
}

} // namespace

int CodeGenerator::StackFrame::frameSizeBytes() const {
  if (!needsFrame()) {
    return 0;
  }
  const int reservedWords = 2 + static_cast<int>(usedCalleeSavedRegisters.size());
  // 局部变量栈槽数：取最大有效索引 +1（已分配寄存器的变量标记为 -1，不占槽）
  int maxSlot = -1;
  for (const auto& entry : localOffsets) {
    if (entry.second > maxSlot) {
      maxSlot = entry.second;
    }
  }
  const int computedLocalBytes = std::max(localBytes, (maxSlot + 1) * kWordBytes);
  return alignTo((reservedWords * kWordBytes) + computedLocalBytes + outgoingArgumentBytes,
                 kStackAlignmentBytes);
}

bool CodeGenerator::StackFrame::needsFrame() const {
  return hasCall || hasStackParameters || localBytes != 0 || outgoingArgumentBytes != 0 ||
         !usedCalleeSavedRegisters.empty();
}

void CodeGenerator::generateDefaultMain(std::ostream& out) {
  out << "    .text\n";
  out << "    .align 2\n";
  out << "    .globl main\n";
  out << "    .type main, @function\n";
  out << "main:\n";
  out << "    li a0, 0\n";
  out << "    ret\n";
  out << "    .size main, .-main\n";
}

void CodeGenerator::generate(const std::vector<IRInst>& ir, std::ostream& out) {
  ir_ = ir;
  out_ = &out;
  currentFunction_.clear();
  currentParamIndex_ = 0;

  emitGlobalData(out);
  emitRaw(out, "    .text\n");

  const auto functions = collectFunctions();
  for (const auto& function : functions) {
    generateFunction(function, out);
  }
}

void CodeGenerator::emitGlobalData(std::ostream& out) {
  bool emittedData = false;
  for (const auto& inst : ir_) {
    if (inst.op != IROp::GLOBAL_VAR_DECL) {
      continue;
    }
    if (!emittedData) {
      emitRaw(out, "    .data\n");
      emitRaw(out, "    .align 2\n");
      emittedData = true;
    }
    out << globalSymbol(inst.dest.name) << ":\n";
    out << "    .word " << inst.src1.immVal << "\n";
  }
  if (emittedData) {
    out << '\n';
  }
}

std::vector<CodeGenerator::FunctionRange> CodeGenerator::collectFunctions() const {
  std::vector<FunctionRange> functions;
  for (std::size_t i = 0; i < ir_.size(); ++i) {
    if (ir_[i].op == IROp::FUNC_BEGIN) {
      FunctionRange range;
      range.name = ir_[i].dest.name;
      range.begin = i + 1;
      for (std::size_t j = i + 1; j < ir_.size(); ++j) {
        if (ir_[j].op == IROp::FUNC_END) {
          range.end = j;
          break;
        }
      }
      functions.push_back(range);
    }
  }
  return functions;
}

CodeGenerator::StackFrame CodeGenerator::analyzeStackFrame(const FunctionRange& function) const {
  StackFrame frame;
  frame.functionName = function.name;
  frame.localOffsets.clear();
  frame.regAlloc.clear();
  frame.leafRegAlloc.clear();
  frame.localBytes = 0;
  frame.outgoingArgumentBytes = 0;

  int localIndex = 0;
  int maxOverflowArgs = 0;
  int nextReg = 2; // s2-s11 可用于局部变量分配
  int incomingParamIndex = 0;
  std::unordered_map<std::string, int> incomingParamRegs;

  // 第一遍：收集所有局部变量
  std::vector<std::string> varOrder; // 保持插入顺序
  for (std::size_t i = function.begin; i < function.end; ++i) {
    const auto& inst = ir_[i];

    if (inst.op == IROp::PARAM) {
      const int argIndex = inst.src1.immVal;
      if (argIndex >= 8) {
        maxOverflowArgs = std::max(maxOverflowArgs, argIndex - 7);
      }
    }

    if (inst.op == IROp::LOCAL_VAR_DECL) {
      if (inst.dest.type == OperandType::LOCAL_VAR) {
        if (frame.localOffsets.find(inst.dest.name) == frame.localOffsets.end()) {
          frame.localOffsets[inst.dest.name] = localIndex++;
          varOrder.push_back(inst.dest.name);
        }
        if (inst.src1.isParam()) {
          if (incomingParamIndex < 8) {
            incomingParamRegs[inst.dest.name] = incomingParamIndex;
          } else {
            frame.hasStackParameters = true;
          }
          ++incomingParamIndex;
        }
      }
      continue;
    }

    if (inst.dest.type == OperandType::LOCAL_VAR) {
      if (frame.localOffsets.find(inst.dest.name) == frame.localOffsets.end()) {
        frame.localOffsets[inst.dest.name] = localIndex++;
        varOrder.push_back(inst.dest.name);
      }
    }
    if (inst.src1.type == OperandType::LOCAL_VAR) {
      if (frame.localOffsets.find(inst.src1.name) == frame.localOffsets.end()) {
        frame.localOffsets[inst.src1.name] = localIndex++;
        varOrder.push_back(inst.src1.name);
      }
    }
    if (inst.src2.type == OperandType::LOCAL_VAR) {
      if (frame.localOffsets.find(inst.src2.name) == frame.localOffsets.end()) {
        frame.localOffsets[inst.src2.name] = localIndex++;
        varOrder.push_back(inst.src2.name);
      }
    }
    // RETURN 和 PARAM 的 dest 也是 LOCAL_VAR 使用
    if (inst.op == IROp::RETURN || inst.op == IROp::PARAM) {
      if (inst.dest.type == OperandType::LOCAL_VAR) {
        if (frame.localOffsets.find(inst.dest.name) == frame.localOffsets.end()) {
          frame.localOffsets[inst.dest.name] = localIndex++;
          varOrder.push_back(inst.dest.name);
        }
      }
    }
  }

  // 第二遍：统计每个局部变量的使用频率（读写次数）
  std::unordered_map<std::string, int> useFreq;
  // 全局变量使用频率（仅当函数无函数调用时启用寄存器分配，避免跨调用同步）
  std::unordered_map<std::string, int> globalUseFreq;
  bool hasCall = false;
  for (std::size_t i = function.begin; i < function.end; ++i) {
    const auto& inst = ir_[i];
    if (inst.op == IROp::CALL) {
      hasCall = true;
    }
    if (inst.src1.isLocalVar()) {
      useFreq[inst.src1.name]++;
    }
    if (inst.src2.isLocalVar()) {
      useFreq[inst.src2.name]++;
    }
    if (inst.dest.isLocalVar()) {
      useFreq[inst.dest.name]++;
    }
    if (inst.src1.isGlobalVar()) {
      globalUseFreq[inst.src1.name]++;
    }
    if (inst.src2.isGlobalVar()) {
      globalUseFreq[inst.src2.name]++;
    }
    if (inst.dest.isGlobalVar()) {
      globalUseFreq[inst.dest.name]++;
    }
  }
  frame.hasCall = hasCall;

  // 按使用频率降序排列，优先为高频变量分配寄存器（循环计数器等）
  // 循环常量提升变量（'k' 前缀）无条件优先，保证循环内比较变成寄存器比较
  std::stable_sort(varOrder.begin(), varOrder.end(),
                   [&](const std::string& a, const std::string& b) {
                     const bool aHoisted = !a.empty() && a[0] == 'k';
                     const bool bHoisted = !b.empty() && b[0] == 'k';
                     if (aHoisted != bHoisted) {
                       return aHoisted;
                     }
                     return useFreq[a] > useFreq[b];
                   });

  // 临时变量 t 寄存器分配（t4-t6）：
  // 一个 IR 名可能被临时回收 pass 在多个基本块内重新定义。把每次定义切成独立
  // live range，只要求每段的使用不跨基本块/调用，再按所有分段的冲突关系着色。
  // 这避免把互不同时存活的分支临时各占一个 s 寄存器并在每次叶函数调用时保存。
  frame.tempRegs.clear();
  {
    // 基本块编号：以控制流/调用为界
    std::unordered_map<std::size_t, int> blockId;
    int curBlock = 0;
    for (std::size_t idx = function.begin; idx < function.end; ++idx) {
      blockId[idx] = curBlock;
      const auto& inst = ir_[idx];
      if (inst.op == IROp::LABEL || inst.op == IROp::BRANCH || inst.op == IROp::BEQZ ||
          inst.op == IROp::BNEZ || inst.op == IROp::CALL || inst.op == IROp::RETURN ||
          inst.op == IROp::PARAM) {
        ++curBlock;
      }
    }
    // 收集每个局部变量的全部引用点：记录 (指令索引, 是否定义)
    std::unordered_map<std::string, std::vector<std::pair<std::size_t, bool>>> varRefs;
    for (std::size_t idx = function.begin; idx < function.end; ++idx) {
      const auto& inst = ir_[idx];
      const auto recordUse = [&](const Operand& op) {
        if (op.isLocalVar()) {
          varRefs[op.name].push_back({idx, false});
        }
      };
      recordUse(inst.src1);
      recordUse(inst.src2);
      if (inst.op == IROp::RETURN || inst.op == IROp::PARAM) {
        recordUse(inst.dest);
      }
      if (inst.dest.isLocalVar()) {
        varRefs[inst.dest.name].push_back({idx, true});
      }
    }
    struct LiveRange {
      std::size_t start = 0;
      std::size_t end = 0;
      int block = 0;
    };
    struct VarRanges {
      std::string name;
      std::vector<LiveRange> ranges;
    };
    std::vector<VarRanges> candidates;
    for (const auto& entry : varRefs) {
      const std::string& name = entry.first;
      const auto& refs = entry.second;
      if (refs.size() < 2) {
        continue; // 只有单一引用（或纯定义无使用），无需分配
      }
      bool ok = true;
      bool haveDefinition = false;
      VarRanges var;
      var.name = name;
      for (const auto& ref : refs) {
        if (ref.second) {
          var.ranges.push_back({ref.first, ref.first, blockId[ref.first]});
          haveDefinition = true;
          continue;
        }
        if (!haveDefinition || var.ranges.empty() ||
            blockId[ref.first] != var.ranges.back().block) {
          ok = false;
          break;
        }
        if (ref.first > var.ranges.back().end) {
          var.ranges.back().end = ref.first;
        }
      }
      var.ranges.erase(
          std::remove_if(var.ranges.begin(), var.ranges.end(),
                         [](const LiveRange& range) { return range.start == range.end; }),
          var.ranges.end());
      if (!ok || var.ranges.empty()) {
        continue;
      }
      candidates.push_back(std::move(var));
    }
    std::sort(candidates.begin(), candidates.end(), [](const VarRanges& a, const VarRanges& b) {
      return a.ranges.front().start < b.ranges.front().start;
    });
    constexpr const char* kTempRegs[3] = {"t4", "t5", "t6"};
    std::vector<std::vector<LiveRange>> assigned(3);
    const auto overlaps = [](const LiveRange& a, const LiveRange& b) {
      return a.start <= b.end && b.start <= a.end;
    };
    for (const auto& candidate : candidates) {
      int color = -1;
      for (int r = 0; r < 3 && color < 0; ++r) {
        bool conflicts = false;
        for (const auto& range : candidate.ranges) {
          for (const auto& occupied : assigned[static_cast<std::size_t>(r)]) {
            if (overlaps(range, occupied)) {
              conflicts = true;
              break;
            }
          }
          if (conflicts) {
            break;
          }
        }
        if (!conflicts) {
          color = r;
        }
      }
      if (color >= 0) {
        frame.tempRegs[candidate.name] = kTempRegs[color];
        auto& occupied = assigned[static_cast<std::size_t>(color)];
        occupied.insert(occupied.end(), candidate.ranges.begin(), candidate.ranges.end());
      }
    }
  }

  // 叶函数没有 CALL，不需要跨调用保值。优先把形参固定到其 ABI 输入寄存器，
  // 其余高频长期值使用空闲的 a0-a7；这既避免参数平行搬运互相覆盖，也免去
  // s2-s11 的逐调用保存/恢复。块内短生命周期仍由上面的 t4-t6 承担。
  if (!hasCall) {
    bool usedArgRegs[8] = {false, false, false, false, false, false, false, false};
    for (const auto& entry : incomingParamRegs) {
      if (frame.tempRegs.find(entry.first) == frame.tempRegs.end()) {
        frame.leafRegAlloc[entry.first] = "a" + std::to_string(entry.second);
        usedArgRegs[entry.second] = true;
      }
    }
    for (const auto& varName : varOrder) {
      if (frame.tempRegs.find(varName) != frame.tempRegs.end() ||
          frame.leafRegAlloc.find(varName) != frame.leafRegAlloc.end()) {
        continue;
      }
      for (int r = 0; r < 8; ++r) {
        if (!usedArgRegs[r]) {
          frame.leafRegAlloc[varName] = "a" + std::to_string(r);
          usedArgRegs[r] = true;
          break;
        }
      }
    }
  }

  // 为前 10 个高频局部变量分配 s2-s11 寄存器
  for (const auto& varName : varOrder) {
    if (frame.tempRegs.find(varName) != frame.tempRegs.end() ||
        frame.leafRegAlloc.find(varName) != frame.leafRegAlloc.end()) {
      continue; // 已分配到 caller-saved 寄存器，不再占用 s 寄存器
    }
    if (nextReg <= 11) {
      frame.regAlloc[varName] = nextReg++;
    }
  }

  // 全局变量寄存器分配：仅当函数无函数调用时（调用者可能修改全局变量，寄存器中的
  // 副本会失效），把高频全局变量放入剩余的 s 寄存器，函数入口加载、出口存回，
  // 循环内对全局的访问从 la+lw/sw（6 条指令）降为寄存器直用
  if (!hasCall) {
    std::vector<std::string> globalOrder;
    for (const auto& entry : globalUseFreq) {
      globalOrder.push_back(entry.first);
    }
    std::stable_sort(globalOrder.begin(), globalOrder.end(),
                     [&](const std::string& a, const std::string& b) {
                       return globalUseFreq[a] > globalUseFreq[b];
                     });
    for (const auto& name : globalOrder) {
      if (nextReg <= 11) {
        frame.globalRegAlloc[name] = nextReg++;
      }
    }
  }

  // 已分配寄存器（s 或 t）的局部变量无需栈槽：紧凑重编号，只统计未分配寄存器的变量
  int stackSlotCount = 0;
  for (auto& entry : frame.localOffsets) {
    if (frame.regAlloc.find(entry.first) != frame.regAlloc.end() ||
        frame.tempRegs.find(entry.first) != frame.tempRegs.end() ||
        frame.leafRegAlloc.find(entry.first) != frame.leafRegAlloc.end()) {
      entry.second = -1; // 标记：无栈槽
    } else {
      entry.second = stackSlotCount++;
    }
  }

  // 统计实际被指令引用的 s 寄存器（避免保存未使用的寄存器，精简 prologue/epilogue）
  std::vector<bool> regUsed(10, false); // 索引 0..9 对应 s2..s11
  for (std::size_t i = function.begin; i < function.end; ++i) {
    const auto& inst = ir_[i];
    const Operand* operands[3] = {&inst.dest, &inst.src1, &inst.src2};
    for (const Operand* op : operands) {
      if (op->isLocalVar()) {
        auto it = frame.regAlloc.find(op->name);
        if (it != frame.regAlloc.end()) {
          const int idx = it->second - 2;
          if (idx >= 0 && idx < 10) {
            regUsed[static_cast<std::size_t>(idx)] = true;
          }
        }
      }
      if (op->isGlobalVar()) {
        auto it = frame.globalRegAlloc.find(op->name);
        if (it != frame.globalRegAlloc.end()) {
          const int idx = it->second - 2;
          if (idx >= 0 && idx < 10) {
            regUsed[static_cast<std::size_t>(idx)] = true;
          }
        }
      }
    }
  }

  // 更新 usedCalleeSavedRegisters：只保存实际使用的 s 寄存器
  frame.usedCalleeSavedRegisters.clear();
  for (int r = 2; r <= 11; ++r) {
    if (r < nextReg && regUsed[static_cast<std::size_t>(r - 2)]) {
      frame.usedCalleeSavedRegisters.push_back("s" + std::to_string(r));
    }
  }

  frame.localBytes = stackSlotCount * kWordBytes;
  frame.outgoingArgumentBytes = maxOverflowArgs * kWordBytes;
  return frame;
}

void CodeGenerator::generateFunction(const FunctionRange& function, std::ostream& out) {
  frame_ = analyzeStackFrame(function);
  currentFunction_ = function.name;
  currentParamIndex_ = 0;
  tailCalls_ = detectTailCalls(function);
  analyzeNonNegativeVars(function);

  // 统计函数内每个局部变量的使用次数（供比较+分支融合判断：结果临时是否仅被分支使用）
  irUseCount_.clear();
  for (std::size_t idx = function.begin; idx < function.end; ++idx) {
    const auto& inst = ir_[idx];
    if (inst.src1.isLocalVar()) {
      ++irUseCount_[inst.src1.name];
    }
    if (inst.src2.isLocalVar()) {
      ++irUseCount_[inst.src2.name];
    }
    if (inst.op == IROp::RETURN || inst.op == IROp::PARAM) {
      if (inst.dest.isLocalVar()) {
        ++irUseCount_[inst.dest.name];
      }
    }
  }

  const auto isTailCallIndex = [this](std::size_t index) {
    for (const auto& tc : tailCalls_) {
      if (tc.callIndex == index) {
        return true;
      }
    }
    return false;
  };

  // 第一遍：生成函数体（不含 prologue/epilogue），窥孔优化后扫描实际使用的 s 寄存器，
  // 避免把被比较+分支窥孔合并吞掉的临时寄存器（如 slt 的结果）计入保存列表
  std::ostringstream bodyOnly;
  bool seenTailCall = false;
  for (std::size_t i = function.begin; i < function.end;) {
    const auto& inst = ir_[i];
    if (inst.op == IROp::FUNC_BEGIN || inst.op == IROp::FUNC_END) {
      ++i;
      continue;
    }
    if (inst.op == IROp::RETURN && seenTailCall) {
      ++i;
      continue; // 尾调用后的 RETURN 不可达，跳过
    }
    if (inst.op == IROp::CALL && isTailCallIndex(i)) {
      // 最小化尾调用序列（仅跳转），不影响 s 寄存器扫描
      bodyOnly << "    j " << inst.src1.name << "\n";
      seenTailCall = true;
      ++i;
      continue;
    }
    i += generateInstruction(i, function.end, bodyOnly);
  }

  std::ostringstream peepBody;
  applyPeephole(bodyOnly.str(), peepBody);
  frame_.usedCalleeSavedRegisters.clear();
  scanUsedSRegisters(peepBody.str(), frame_.usedCalleeSavedRegisters);

  // 第二遍：按最终帧布局重新生成（局部变量栈偏移依赖 usedCalleeSavedRegisters 数量）
  std::ostringstream body;
  emitRaw(body, "    .align 2\n");
  emitRaw(body, "    .globl " + function.name + "\n");
  emitRaw(body, "    .type " + function.name + ", @function\n");
  body << function.name << ":\n";

  emitPrologue(frame_, body);

  // 全局变量寄存器副本：函数入口加载一次（位于 .L_body 之前，
  // 自递归尾调用跳回 .L_body 时不会重复加载，保留迭代间的值）
  for (const auto& entry : frame_.globalRegAlloc) {
    const std::string sreg = "s" + std::to_string(entry.second);
    emit(body, "la", "t2, " + globalSymbol(entry.first));
    emit(body, "lw", sreg + ", 0(t2)");
  }

  // 自递归尾调用跳回点：位于参数装载之前（复用当前栈帧，等价于循环）
  body << ".L_" << function.name << "_body:\n";

  currentParamIndex_ = 0;
  seenTailCall = false;
  for (std::size_t i = function.begin; i < function.end;) {
    const auto& inst = ir_[i];
    if (inst.op == IROp::FUNC_BEGIN || inst.op == IROp::FUNC_END) {
      ++i;
      continue;
    }
    if (inst.op == IROp::RETURN && seenTailCall) {
      ++i;
      continue; // 尾调用后的 RETURN 不可达，跳过
    }
    if (inst.op == IROp::CALL && isTailCallIndex(i)) {
      for (const auto& tc : tailCalls_) {
        if (tc.callIndex == i) {
          emitTailCall(tc, body);
          break;
        }
      }
      seenTailCall = true;
      ++i;
      continue;
    }
    i += generateInstruction(i, function.end, body);
  }

  body << ".L_" << function.name << "_exit:\n";

  // 全局变量寄存器副本存回（位于 epilogue 恢复寄存器之前）
  for (const auto& entry : frame_.globalRegAlloc) {
    const std::string sreg = "s" + std::to_string(entry.second);
    emit(body, "la", "t2, " + globalSymbol(entry.first));
    emit(body, "sw", sreg + ", 0(t2)");
  }

  emitEpilogue(frame_, body);
  body << "    .size " << function.name << ", .-" << function.name << "\n";

  applyPeephole(body.str(), out);
}

std::size_t CodeGenerator::generateInstruction(std::size_t index, std::size_t end,
                                               std::ostream& out) {
  currentInstIndex_ = index;
  const IRInst& inst = ir_[index];
  switch (inst.op) {
  case IROp::LOCAL_VAR_DECL: {
    if (inst.dest.type == OperandType::LOCAL_VAR) {
      ensureLocalOffset(inst.dest);
      if (inst.src1.type == OperandType::PARAM) {
        if (currentParamIndex_ < 8) {
          const std::string reg = "a" + std::to_string(currentParamIndex_);
          // 若 dest 在寄存器中，直接 mv 到该寄存器，避免 t0 中转
          const std::string destReg = regForVar(inst.dest.name);
          if (!destReg.empty()) {
            if (destReg != reg) {
              emit(out, "mv", destReg + ", " + reg);
            }
          } else {
            emit(out, "mv", "t0, " + reg);
            storeOperand("t0", inst.dest, out);
          }
        } else {
          const int stackOffset = (currentParamIndex_ - 8) * kWordBytes;
          const std::string destReg = regForVar(inst.dest.name);
          if (!destReg.empty()) {
            emit(out, "lw", destReg + ", " + std::to_string(stackOffset) + "(s0)");
          } else {
            emit(out, "lw", "t0, " + std::to_string(stackOffset) + "(s0)");
            storeOperand("t0", inst.dest, out);
          }
        }
        currentParamIndex_ += 1;
      } else if (inst.src1.type == OperandType::IMM) {
        loadOperand(inst.src1, destRegOrT0(inst.dest), out);
        if (!isDestInReg(inst.dest)) {
          storeOperand("t0", inst.dest, out);
        }
      } else if (!inst.src1.isNone()) {
        loadOperand(inst.src1, destRegOrT0(inst.dest), out);
        if (!isDestInReg(inst.dest)) {
          storeOperand("t0", inst.dest, out);
        }
      }
    }
    break;
  }
  case IROp::ASSIGN: {
    if (inst.dest.type == OperandType::LOCAL_VAR || inst.dest.type == OperandType::GLOBAL_VAR) {
      loadOperand(inst.src1, destRegOrT0(inst.dest), out);
      if (!isDestInReg(inst.dest)) {
        storeOperand("t0", inst.dest, out);
      }
    }
    break;
  }
  case IROp::ADD:
  case IROp::SUB:
  case IROp::MUL:
  case IROp::DIV:
  case IROp::MOD:
  case IROp::NOT:
    emitBinaryOp(inst, out);
    break;
  case IROp::LT:
  case IROp::GT:
  case IROp::LE:
  case IROp::GE:
  case IROp::EQ:
  case IROp::NE:
    return emitCompareOrFuse(index, end, out);
  case IROp::PARAM: {
    const int argIndex = inst.src1.immVal;
    if (argIndex < 8) {
      const std::string reg = "a" + std::to_string(argIndex);
      if (inst.dest.type == OperandType::IMM) {
        emit(out, "li", reg + ", " + std::to_string(inst.dest.immVal));
      } else {
        loadOperand(inst.dest, reg, out);
      }
    } else {
      // 溢出参数存入栈（调用者栈帧低地址区）
      const int stackOffset = (argIndex - 8) * kWordBytes;
      if (inst.dest.type == OperandType::IMM) {
        emit(out, "li", "t0, " + std::to_string(inst.dest.immVal));
      } else {
        loadOperand(inst.dest, "t0", out);
      }
      emit(out, "sw", "t0, " + std::to_string(stackOffset) + "(sp)");
    }
    break;
  }
  case IROp::CALL:
    emitCall(inst, out);
    break;
  case IROp::RETURN: {
    if (!inst.dest.isNone()) {
      loadOperand(inst.dest, "a0", out);
    }
    out << "    j .L_" << currentFunction_ << "_exit\n";
    break;
  }
  case IROp::LABEL:
    out << asmLabel(inst.dest.name) << ":\n";
    break;
  case IROp::BRANCH:
    out << "    j " << asmLabel(inst.dest.name) << "\n";
    break;
  case IROp::BEQZ: {
    // 若条件变量已在寄存器中，直接使用该寄存器，省去 mv
    if (inst.src1.isLocalVar()) {
      const std::string reg = regForVar(inst.src1.name);
      if (!reg.empty()) {
        out << "    beqz " << reg << ", " << asmLabel(inst.dest.name) << "\n";
        break;
      }
    } else if (inst.src1.isGlobalVar()) {
      auto it = frame_.globalRegAlloc.find(inst.src1.name);
      if (it != frame_.globalRegAlloc.end()) {
        std::string reg = "s" + std::to_string(it->second);
        out << "    beqz " << reg << ", " << asmLabel(inst.dest.name) << "\n";
        break;
      }
    }
    loadOperand(inst.src1, "t0", out);
    out << "    beqz t0, " << asmLabel(inst.dest.name) << "\n";
    break;
  }
  case IROp::BNEZ: {
    if (inst.src1.isLocalVar()) {
      const std::string reg = regForVar(inst.src1.name);
      if (!reg.empty()) {
        out << "    bnez " << reg << ", " << asmLabel(inst.dest.name) << "\n";
        break;
      }
    } else if (inst.src1.isGlobalVar()) {
      auto it = frame_.globalRegAlloc.find(inst.src1.name);
      if (it != frame_.globalRegAlloc.end()) {
        std::string reg = "s" + std::to_string(it->second);
        out << "    bnez " << reg << ", " << asmLabel(inst.dest.name) << "\n";
        break;
      }
    }
    loadOperand(inst.src1, "t0", out);
    out << "    bnez t0, " << asmLabel(inst.dest.name) << "\n";
    break;
  }
  case IROp::LOAD: {
    if (inst.dest.type == OperandType::LOCAL_VAR || inst.dest.type == OperandType::GLOBAL_VAR) {
      loadOperand(inst.src1, destRegOrT0(inst.dest), out);
      if (!isDestInReg(inst.dest)) {
        storeOperand("t0", inst.dest, out);
      }
    }
    break;
  }
  case IROp::STORE: {
    if (inst.dest.type == OperandType::LOCAL_VAR || inst.dest.type == OperandType::GLOBAL_VAR) {
      loadOperand(inst.src1, destRegOrT0(inst.dest), out);
      if (!isDestInReg(inst.dest)) {
        storeOperand("t0", inst.dest, out);
      }
    }
    break;
  }
  case IROp::GLOBAL_VAR_DECL:
    break;
  case IROp::FUNC_BEGIN:
  case IROp::FUNC_END:
    break;
  }
  return 1;
}

// 比较指令 + 紧随其后的分支融合：
//   %t = LT/GT/LE/GE/EQ/NE a, b; BEQZ/BNEZ %t, L
// 当 %t 仅被该分支使用（irUseCount_ == 1）时，不把比较结果落栈再重载，
// 而是直接计算到 t0 后分支。生成的 slt/beqz（或 addi/beqz）序列紧凑相邻，
// 由 applyPeephole 进一步合并为单条条件分支（blt/bge 等）。
std::size_t CodeGenerator::emitCompareOrFuse(std::size_t index, std::size_t end,
                                             std::ostream& out) {
  const IRInst& inst = ir_[index];
  if (index + 1 < end) {
    const IRInst& br = ir_[index + 1];
    if ((br.op == IROp::BEQZ || br.op == IROp::BNEZ) && br.src1.isLocalVar() &&
        br.src1.name == inst.dest.name && inst.dest.isLocalVar() &&
        irUseCount_[inst.dest.name] == 1) {
      emitCompareInto(inst, "t0", out);
      if (br.op == IROp::BEQZ) {
        emit(out, "beqz", "t0, " + asmLabel(br.dest.name));
      } else {
        emit(out, "bnez", "t0, " + asmLabel(br.dest.name));
      }
      return 2;
    }
  }
  emitCompareOp(inst, out);
  return 1;
}

void CodeGenerator::emitPrologue(const StackFrame& frame, std::ostream& out) const {
  const int frameSize = frame.frameSizeBytes();
  if (frameSize == 0) {
    return;
  }
  emit(out, "addi", "sp, sp, -" + std::to_string(frameSize));
  // 在更新 s0 之前保存 ra 和旧 s0（使用 sp 相对偏移）
  emit(out, "sw", "ra, " + std::to_string(frameSize - 4) + "(sp)");
  emit(out, "sw", "s0, " + std::to_string(frameSize - 8) + "(sp)");
  // 设置新帧指针
  emit(out, "addi", "s0, sp, " + std::to_string(frameSize));

  int saveOffset = -12;
  for (const auto& reg : frame.usedCalleeSavedRegisters) {
    emit(out, "sw", reg + ", " + std::to_string(saveOffset) + "(s0)");
    saveOffset -= 4;
  }
}

void CodeGenerator::emitEpilogue(const StackFrame& frame, std::ostream& out) const {
  if (!frame.needsFrame()) {
    emit(out, "ret", "");
    return;
  }
  int restoreOffset = -12;
  for (const auto& reg : frame.usedCalleeSavedRegisters) {
    emit(out, "lw", reg + ", " + std::to_string(restoreOffset) + "(s0)");
    restoreOffset -= 4;
  }
  emit(out, "lw", "ra, -4(s0)");
  emit(out, "lw", "s0, -8(s0)");
  emit(out, "addi", "sp, sp, " + std::to_string(frame.frameSizeBytes()));
  emit(out, "ret", "");
}

namespace {

// 解析汇编行：去掉前导空白，拆分为操作码 + 参数列表
struct AsmToken {
  std::string opcode;
  std::vector<std::string> args;
};

AsmToken parseAsmLine(const std::string& line) {
  AsmToken token;
  std::size_t pos = line.find_first_not_of(" \t");
  if (pos == std::string::npos) {
    return token;
  }
  const std::size_t end = line.find_last_not_of(" \t");
  const std::string content = line.substr(pos, end - pos + 1);
  if (content.empty() || content.back() == ':') {
    return token; // 标签行
  }
  std::size_t sp = content.find(' ');
  if (sp == std::string::npos) {
    token.opcode = content;
    return token;
  }
  token.opcode = content.substr(0, sp);
  std::string args = content.substr(sp + 1);
  // 按逗号分割参数
  std::size_t start = 0;
  while (start <= args.size()) {
    std::size_t comma = args.find(',', start);
    const std::string part =
        args.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
    std::size_t p0 = part.find_first_not_of(" \t");
    std::size_t p1 = part.find_last_not_of(" \t");
    if (p0 != std::string::npos) {
      token.args.push_back(part.substr(p0, p1 - p0 + 1));
    }
    if (comma == std::string::npos) {
      break;
    }
    start = comma + 1;
  }
  return token;
}

} // namespace

void CodeGenerator::applyPeephole(const std::string& asmText, std::ostream& out) {
  // 按行拆分
  std::vector<std::string> lines;
  {
    std::istringstream iss(asmText);
    std::string line;
    while (std::getline(iss, line)) {
      lines.push_back(line);
    }
  }
  const auto n = lines.size();
  for (std::size_t i = 0; i < n; ++i) {
    const AsmToken tok = parseAsmLine(lines[i]);
    const auto& op = tok.opcode;
    const auto& args = tok.args;

    // An unconditional jump to the immediately following label is a no-op.
    if (op == "j" && args.size() == 1 && i + 1 < n) {
      const std::string& nextLine = lines[i + 1];
      const std::size_t first = nextLine.find_first_not_of(" \t");
      const std::size_t last = nextLine.find_last_not_of(" \t");
      if (first != std::string::npos && nextLine.substr(first, last - first + 1) == args[0] + ":") {
        continue;
      }
    }

    // 模式12: sw rd, off + lw rd, off（同寄存器同偏移，且相邻）
    // 值刚从 rd 存入内存，寄存器 rd 仍持有该值，紧随其后的 lw 是冗余的。
    // 保留 sw（内存值不变），删除 lw（寄存器值不变），避免一次内存往返。
    if (op == "sw" && args.size() == 2 && i + 1 < n) {
      const AsmToken next = parseAsmLine(lines[i + 1]);
      if (next.opcode == "lw" && next.args.size() == 2 && next.args[0] == args[0] &&
          next.args[1] == args[1]) {
        out << lines[i] << "\n"; // 保留 sw
        ++i;                     // 跳过冗余 lw
        continue;
      }
    }

    // 模式0: 通用空操作消除
    //   mv rd, rd / addi rd, rd, 0 / sub rd, rd, 0 / xori rd, rd, 0
    if (op == "mv" && args.size() >= 2 && args[0] == args[1]) {
      continue;
    }
    if (op == "sub" && args.size() == 3 && args[0] == args[1] && args[2] == "0") {
      continue;
    }
    if (args.size() == 3 && args[0] == args[1] && op == "addi" && args[2] == "0") {
      continue;
    }
    if (args.size() == 3 && args[0] == args[1] && op == "xori" && args[2] == "0") {
      continue;
    }

    // 模式1: slt rd, rs1, rs2  +  beqz rd, L  ->  bge rs1, rs2, L
    // 模式2: slt rd, rs1, rs2  +  bnez rd, L  ->  blt rs1, rs2, L
    if (op == "slt" && args.size() == 3 && i + 1 < n) {
      const AsmToken next = parseAsmLine(lines[i + 1]);
      if (next.opcode == "beqz" && next.args.size() == 2 && next.args[0] == args[0]) {
        emit(out, "bge", args[1] + ", " + args[2] + ", " + next.args[1]);
        ++i;
        continue;
      }
      if (next.opcode == "bnez" && next.args.size() == 2 && next.args[0] == args[0]) {
        emit(out, "blt", args[1] + ", " + args[2] + ", " + next.args[1]);
        ++i;
        continue;
      }
    }

    // 模式3: sub rd, rs1, rs2 + seqz rd, rd + beqz rd, L -> bne rs1, rs2, L
    // 模式4: sub rd, rs1, rs2 + seqz rd, rd + bnez rd, L -> beq rs1, rs2, L
    // 模式5: sub rd, rs1, rs2 + snez rd, rd + beqz rd, L -> beq rs1, rs2, L
    // 模式6: sub rd, rs1, rs2 + snez rd, rd + bnez rd, L -> bne rs1, rs2, L
    if (op == "sub" && args.size() == 3 && i + 2 < n) {
      const AsmToken t1 = parseAsmLine(lines[i + 1]);
      const AsmToken t2 = parseAsmLine(lines[i + 2]);
      if ((t1.opcode == "seqz" || t1.opcode == "snez") && t1.args.size() == 2 &&
          t1.args[0] == args[0] && t1.args[1] == args[0]) {
        const bool isEq = t1.opcode == "seqz";
        if (t2.opcode == "beqz" && t2.args.size() == 2 && t2.args[0] == args[0]) {
          // seqz+beqz -> bne ; snez+beqz -> beq
          emit(out, (isEq ? "bne" : "beq"), args[1] + ", " + args[2] + ", " + t2.args[1]);
          i += 2;
          continue;
        }
        if (t2.opcode == "bnez" && t2.args.size() == 2 && t2.args[0] == args[0]) {
          emit(out, (isEq ? "beq" : "bne"), args[1] + ", " + args[2] + ", " + t2.args[1]);
          i += 2;
          continue;
        }
      }
    }

    // 模式7: slt rd, rs1, rs2 + xori rd, rd, 1 + beqz rd, L -> blt rs1, rs2, L
    // 模式8: slt rd, rs1, rs2 + xori rd, rd, 1 + bnez rd, L -> bge rs1, rs2, L
    if (op == "slt" && args.size() == 3 && i + 2 < n) {
      const AsmToken t1 = parseAsmLine(lines[i + 1]);
      const AsmToken t2 = parseAsmLine(lines[i + 2]);
      if (t1.opcode == "xori" && t1.args.size() == 3 && t1.args[0] == args[0] &&
          t1.args[1] == args[0] && t1.args[2] == "1") {
        if (t2.opcode == "beqz" && t2.args.size() == 2 && t2.args[0] == args[0]) {
          emit(out, "blt", args[1] + ", " + args[2] + ", " + t2.args[1]);
          i += 2;
          continue;
        }
        if (t2.opcode == "bnez" && t2.args.size() == 2 && t2.args[0] == args[0]) {
          emit(out, "bge", args[1] + ", " + args[2] + ", " + t2.args[1]);
          i += 2;
          continue;
        }
      }
    }

    // 模式9: 立即数相等/不等比较 + 分支
    // li t1, imm + sub rd, rs, t1 + seqz rd, rd + beqz rd, L -> addi rd, rs, -imm + beqz rd, L
    // li t1, imm + sub rd, rs, t1 + snez rd, rd + bnez rd, L -> addi rd, rs, -imm + bnez rd, L
    if (op == "li" && args.size() == 2 && args[0] == "t1" && i + 3 < n) {
      int immVal = 0;
      try {
        immVal = std::stoi(args[1]);
      } catch (...) {
        immVal = 0;
        if (args[1] == "-2147483648") {
          // 超出 addi 范围，跳过
        }
      }
      const AsmToken t1 = parseAsmLine(lines[i + 1]);
      const AsmToken t2 = parseAsmLine(lines[i + 2]);
      const AsmToken t3 = parseAsmLine(lines[i + 3]);
      // 仅当 -imm 可用 addi 表示时才替换
      // 四种组合：
      //   seqz+beqz（跳转当 rs != imm）-> addi + bnez
      //   snez+beqz（跳转当 rs == imm）-> addi + beqz
      //   seqz+bnez（跳转当 rs == imm）-> addi + beqz
      //   snez+bnez（跳转当 rs != imm）-> addi + bnez
      if (immVal >= -2047 && immVal <= 2048) {
        // sub rd, rs, t1：t1 是第三个操作数
        if (t1.opcode == "sub" && t1.args.size() == 3 && t1.args[2] == "t1" &&
            ((t2.opcode == "seqz" && t3.opcode == "beqz") ||
             (t2.opcode == "snez" && t3.opcode == "bnez"))) {
          // seqz+beqz 与 snez+bnez 都是跳转当不等（rs != imm）-> addi + bnez
          if (t2.args.size() == 2 && t2.args[0] == t1.args[0] && t2.args[1] == t1.args[0] &&
              t3.args.size() == 2 && t3.args[0] == t1.args[0]) {
            if (-immVal != 0) {
              emit(out, "addi", t1.args[0] + ", " + t1.args[1] + ", " + std::to_string(-immVal));
              emit(out, "bnez", t1.args[0] + ", " + t3.args[1]);
            } else {
              // imm==0：addi rd, rs, 0 会把 rd 从布尔值重算为 rs，不能省略；
              // 直接对源寄存器分支（rs != 0）
              emit(out, "bnez", t1.args[1] + ", " + t3.args[1]);
            }
            i += 3;
            continue;
          }
        }
        if (t1.opcode == "sub" && t1.args.size() == 3 && t1.args[2] == "t1" &&
            ((t2.opcode == "seqz" && t3.opcode == "bnez") ||
             (t2.opcode == "snez" && t3.opcode == "beqz"))) {
          // seqz+bnez 与 snez+beqz 都是跳转当相等 -> addi + beqz
          if (t2.args.size() == 2 && t2.args[0] == t1.args[0] && t2.args[1] == t1.args[0] &&
              t3.args.size() == 2 && t3.args[0] == t1.args[0]) {
            if (-immVal != 0) {
              emit(out, "addi", t1.args[0] + ", " + t1.args[1] + ", " + std::to_string(-immVal));
              emit(out, "beqz", t1.args[0] + ", " + t3.args[1]);
            } else {
              // imm==0：直接对源寄存器分支（rs == 0）
              emit(out, "beqz", t1.args[1] + ", " + t3.args[1]);
            }
            i += 3;
            continue;
          }
        }
      }
    }

    // 模式10: slti rd, rs, imm + xori rd, rd, 1 + beqz/bnez rd, L
    // -> addi rd, rs, -imm + bltz/bgez rd, L（需 -imm 可表示）
    // slti rd, rs, imm = rs < imm; xori 后 = rs >= imm
    //   beqz（跳转当 rs < imm）-> bltz
    //   bnez（跳转当 rs >= imm）-> bgez
    if (op == "slti" && args.size() == 3 && i + 2 < n) {
      int immVal = 0;
      try {
        immVal = std::stoi(args[2]);
      } catch (...) {
        immVal = 0;
      }
      const AsmToken t1 = parseAsmLine(lines[i + 1]);
      const AsmToken t2 = parseAsmLine(lines[i + 2]);
      if (immVal != -2048 && t1.opcode == "xori" && t1.args.size() == 3 && t1.args[0] == args[0] &&
          t1.args[1] == args[0] && t1.args[2] == "1") {
        if (t2.opcode == "beqz" && t2.args.size() == 2 && t2.args[0] == args[0]) {
          emit(out, "addi", args[0] + ", " + args[1] + ", " + std::to_string(-immVal));
          emit(out, "bltz", args[0] + ", " + t2.args[1]);
          i += 2;
          continue;
        }
        if (t2.opcode == "bnez" && t2.args.size() == 2 && t2.args[0] == args[0]) {
          emit(out, "addi", args[0] + ", " + args[1] + ", " + std::to_string(-immVal));
          emit(out, "bgez", args[0] + ", " + t2.args[1]);
          i += 2;
          continue;
        }
      }
      // 模式11: slti rd, rs, 0/1 + beqz/bnez rd, L -> 零比较分支
      //   slti rd, rs, 0 + beqz -> bgez rs（rs >= 0）; slti rd, rs, 0 + bnez -> bltz rs
      //   slti rd, rs, 1 + beqz -> bgtz rs（rs > 0）; slti rd, rs, 1 + bnez -> blez rs
      if ((immVal == 0 || immVal == 1) && i + 1 < n) {
        const bool isOne = (immVal == 1);
        if (t1.opcode == "beqz" && t1.args.size() == 2 && t1.args[0] == args[0]) {
          emit(out, (isOne ? "bgtz" : "bgez"), args[1] + ", " + t1.args[1]);
          ++i;
          continue;
        }
        if (t1.opcode == "bnez" && t1.args.size() == 2 && t1.args[0] == args[0]) {
          emit(out, (isOne ? "blez" : "bltz"), args[1] + ", " + t1.args[1]);
          ++i;
          continue;
        }
      }
    }

    out << lines[i] << "\n";
  }
}

void CodeGenerator::loadOperand(const Operand& operand, std::string_view reg, std::ostream& out) {
  if (operand.type == OperandType::IMM) {
    emit(out, "li", std::string(reg) + ", " + std::to_string(operand.immVal));
    return;
  }

  if (operand.type == OperandType::LOCAL_VAR) {
    // 如果变量分配了寄存器（s 或 t），直接从寄存器复制（mv 比 lw 快）
    const std::string varReg = regForVar(operand.name);
    if (!varReg.empty()) {
      if (std::string(reg) != varReg) {
        emit(out, "mv", std::string(reg) + ", " + varReg);
      }
      return;
    }
    // 否则从栈上加载
    const int offset = ensureLocalOffset(operand);
    emit(out, "lw", std::string(reg) + ", " + std::to_string(offset) + "(s0)");
    return;
  }

  if (operand.type == OperandType::GLOBAL_VAR) {
    // 若全局变量分配了寄存器，直接从寄存器复制
    auto it = frame_.globalRegAlloc.find(operand.name);
    if (it != frame_.globalRegAlloc.end()) {
      std::string sreg = "s" + std::to_string(it->second);
      if (std::string(reg) != sreg) {
        emit(out, "mv", std::string(reg) + ", " + sreg);
      }
      return;
    }
    emit(out, "la", "t2, " + globalSymbol(operand.name));
    emit(out, "lw", std::string(reg) + ", 0(t2)");
    return;
  }

  if (operand.type == OperandType::LABEL) {
    emit(out, "la", std::string(reg) + ", " + asmLabel(operand.name));
    return;
  }

  if (operand.type == OperandType::FUNC) {
    emit(out, "la", std::string(reg) + ", " + operand.name);
    return;
  }
}

void CodeGenerator::storeOperand(std::string_view reg, const Operand& operand, std::ostream& out) {
  if (operand.type == OperandType::LOCAL_VAR) {
    // 如果变量分配了寄存器（s 或 t），直接存到寄存器（mv 比 sw 快）
    const std::string varReg = regForVar(operand.name);
    if (!varReg.empty()) {
      if (std::string(reg) != varReg) {
        emit(out, "mv", varReg + ", " + std::string(reg));
      }
      return;
    }
    // 否则存到栈上
    const int offset = ensureLocalOffset(operand);
    emit(out, "sw", std::string(reg) + ", " + std::to_string(offset) + "(s0)");
    return;
  }

  if (operand.type == OperandType::GLOBAL_VAR) {
    // 若全局变量分配了寄存器，直接存到寄存器
    auto it = frame_.globalRegAlloc.find(operand.name);
    if (it != frame_.globalRegAlloc.end()) {
      std::string sreg = "s" + std::to_string(it->second);
      if (std::string(reg) != sreg) {
        emit(out, "mv", sreg + ", " + std::string(reg));
      }
      return;
    }
    emit(out, "la", "t2, " + globalSymbol(operand.name));
    emit(out, "sw", std::string(reg) + ", 0(t2)");
    return;
  }
}

bool CodeGenerator::emitMagicDiv(int imm, const std::string& srcReg, const std::string& destReg,
                                 std::ostream& out) const {
  if (imm == INT32_MIN) {
    return false;
  }
  const int32_t ad = imm < 0 ? -imm : imm;
  uint32_t magic;
  int32_t shift;
  if (!computeSignedDivMagic(ad, magic, shift)) {
    return false;
  }
  const int32_t magicSigned = static_cast<int32_t>(magic);
  emit(out, "li", "t1, " + std::to_string(magicSigned));
  emit(out, "mulh", "t2, " + srcReg + ", t1");
  if (magicSigned < 0) {
    emit(out, "add", "t2, t2, " + srcReg);
  }
  if (shift > 0) {
    emit(out, "srai", "t2, t2, " + std::to_string(shift));
  }
  emit(out, "srai", "t0, " + srcReg + ", 31");
  emit(out, "sub", "t2, t2, t0");
  if (imm < 0) {
    emit(out, "sub", "t2, x0, t2");
  }
  if (destReg != "t2") {
    emit(out, "mv", destReg + ", t2");
  }
  return true;
}

bool CodeGenerator::emitMagicMod(int imm, const std::string& srcReg, const std::string& destReg,
                                 std::ostream& out) const {
  if (imm == INT32_MIN) {
    return false;
  }
  const int32_t ad = imm < 0 ? -imm : imm;
  uint32_t magic;
  int32_t shift;
  if (!computeSignedDivMagic(ad, magic, shift)) {
    return false;
  }
  const int32_t magicSigned = static_cast<int32_t>(magic);
  // srcReg 可能为 t0：序列末尾会破坏 t0，先备份到 t3
  const bool srcIsT0 = (srcReg == "t0");
  if (srcIsT0) {
    emit(out, "mv", "t3, t0");
  }
  emit(out, "li", "t1, " + std::to_string(magicSigned));
  emit(out, "mulh", "t2, " + srcReg + ", t1");
  if (magicSigned < 0) {
    emit(out, "add", "t2, t2, " + srcReg);
  }
  if (shift > 0) {
    emit(out, "srai", "t2, t2, " + std::to_string(shift));
  }
  emit(out, "srai", "t0, " + srcReg + ", 31");
  emit(out, "sub", "t2, t2, t0");
  if (imm < 0) {
    emit(out, "sub", "t2, x0, t2");
  }
  emit(out, "li", "t1, " + std::to_string(imm));
  emit(out, "mul", "t2, t2, t1");
  emit(out, "sub", destReg + ", " + (srcIsT0 ? std::string("t3") : srcReg) + ", t2");
  return true;
}

void CodeGenerator::emitBinaryOp(const IRInst& inst, std::ostream& out) {
  // 确定目标寄存器：若 dest 分配了寄存器（s 或 t）则直接用该寄存器运算，避免 mv 往返
  std::string destReg = "t0";
  bool destInReg = false;
  if (inst.dest.isLocalVar()) {
    const std::string reg = regForVar(inst.dest.name);
    if (!reg.empty()) {
      destReg = reg;
      destInReg = true;
    }
  } else if (inst.dest.isGlobalVar()) {
    auto it = frame_.globalRegAlloc.find(inst.dest.name);
    if (it != frame_.globalRegAlloc.end()) {
      destReg = "s" + std::to_string(it->second);
      destInReg = true;
    }
  }

  // 一元 NOT
  if (inst.op == IROp::NOT) {
    loadOperand(inst.src1, destReg, out);
    emit(out, "sltiu", destReg + ", " + destReg + ", 1");
    if (!destInReg) {
      storeOperand(destReg, inst.dest, out);
    }
    return;
  }

  // RISC-V 没有“立即数减寄存器”指令。若目标与右操作数共用寄存器，通用
  // 调度会先把右值 mv 到 t1，再用 li 覆盖目标，共需 3 条。改用空闲 scratch
  // 保存立即数即可原地完成，热循环中的 `x = 1 - x` 每轮少 1 条指令。
  if (inst.op == IROp::SUB && inst.src1.isImm() && inst.src2.isLocalVar()) {
    const std::string rhsReg = regForVar(inst.src2.name);
    if (!rhsReg.empty() && rhsReg == destReg) {
      if (inst.src1.immVal == 0) {
        emit(out, "sub", destReg + ", x0, " + rhsReg);
      } else {
        const std::string scratch = (destReg == "t0") ? "t2" : "t0";
        emit(out, "li", scratch + ", " + std::to_string(inst.src1.immVal));
        emit(out, "sub", destReg + ", " + scratch + ", " + rhsReg);
      }
      if (!destInReg) {
        storeOperand(destReg, inst.dest, out);
      }
      return;
    }
  }

  // 若 `x = invariant - x` 的左值已在另一寄存器，RISC-V 允许 rd 与 rs2
  // 相同，可直接原地读取旧 x 后写回，避免通用路径先 mv 备份右操作数。
  if (inst.op == IROp::SUB && inst.src1.isLocalVar() && inst.src2.isLocalVar()) {
    const std::string lhsReg = regForVar(inst.src1.name);
    const std::string rhsReg = regForVar(inst.src2.name);
    if (!lhsReg.empty() && rhsReg == destReg && lhsReg != destReg) {
      emit(out, "sub", destReg + ", " + lhsReg + ", " + rhsReg);
      if (!destInReg) {
        storeOperand(destReg, inst.dest, out);
      }
      return;
    }
  }

  // 确定 src2 的寄存器：若已在寄存器中且不是 destReg，直接用该寄存器
  const bool src2IsSmallImm =
      inst.src2.isImm() && inst.src2.immVal >= -2048 && inst.src2.immVal <= 2047;
  int imm = 0;
  std::string src2Reg = "t1";
  bool src2InReg = false;
  if (src2IsSmallImm) {
    imm = inst.src2.immVal;
  } else if (inst.src2.isLocalVar()) {
    const std::string reg = regForVar(inst.src2.name);
    if (!reg.empty()) {
      // 若 src2 的寄存器就是 destReg，加载 src1 会覆盖 src2，需用 t1
      if (reg != destReg) {
        src2Reg = reg;
        src2InReg = true;
      }
    }
  }
  if (!src2IsSmallImm && !src2InReg) {
    loadOperand(inst.src2, "t1", out);
  }

  // 确定 src1 的寄存器：若已在寄存器中，直接用该寄存器运算，避免 mv 到 destReg
  std::string src1Reg = destReg;
  if (inst.src1.isLocalVar()) {
    const std::string reg = regForVar(inst.src1.name);
    if (!reg.empty()) {
      src1Reg = reg;
    } else {
      loadOperand(inst.src1, destReg, out);
      src1Reg = destReg;
    }
  } else {
    loadOperand(inst.src1, destReg, out);
    src1Reg = destReg;
  }

  // 执行运算，结果直接落在 destReg
  if (src2IsSmallImm) {
    switch (inst.op) {
    case IROp::ADD:
      emit(out, "addi", destReg + ", " + src1Reg + ", " + std::to_string(imm));
      break;
    case IROp::SUB:
      emit(out, "addi", destReg + ", " + src1Reg + ", " + std::to_string(-imm));
      break;
    case IROp::MUL:
      if (imm > 0 && (imm & (imm - 1)) == 0) {
        emit(out, "slli",
             destReg + ", " + src1Reg + ", " +
                 std::to_string(__builtin_ctz(static_cast<unsigned>(imm))));
      } else if (imm == 3) {
        emit(out, "slli", "t1, " + src1Reg + ", 1");
        emit(out, "add", destReg + ", t1, " + src1Reg);
      } else if (imm == 5) {
        emit(out, "slli", "t1, " + src1Reg + ", 2");
        emit(out, "add", destReg + ", t1, " + src1Reg);
      } else if (imm == 7) {
        emit(out, "slli", "t1, " + src1Reg + ", 3");
        emit(out, "sub", destReg + ", t1, " + src1Reg);
      } else if (imm == 9) {
        emit(out, "slli", "t1, " + src1Reg + ", 3");
        emit(out, "add", destReg + ", t1, " + src1Reg);
      } else if (imm == 15) {
        emit(out, "slli", "t1, " + src1Reg + ", 4");
        emit(out, "sub", destReg + ", t1, " + src1Reg);
      } else if (imm == -1) {
        emit(out, "sub", destReg + ", x0, " + src1Reg);
      } else if (imm < 0 && (-imm & (-imm - 1)) == 0) {
        const int shift = __builtin_ctz(static_cast<unsigned>(-imm));
        emit(out, "slli", destReg + ", " + src1Reg + ", " + std::to_string(shift));
        emit(out, "sub", destReg + ", x0, " + destReg);
      } else {
        emit(out, "li", "t1, " + std::to_string(imm));
        emit(out, "mul", destReg + ", " + src1Reg + ", t1");
      }
      break;
    case IROp::DIV:
      if (imm > 0 && (imm & (imm - 1)) == 0) {
        // x / 2^n：已知非负时直接用算术右移（1 条指令）
        const int shift = __builtin_ctz(static_cast<unsigned>(imm));
        const bool srcNonNeg =
            inst.src1.isLocalVar() && isNonNegative(inst.src1.name, currentInstIndex_);
        if (srcNonNeg) {
          emit(out, "srai", destReg + ", " + src1Reg + ", " + std::to_string(shift));
          break;
        }
        // 向零取整: t=(x>>31)>>(32-n); q=(x+t)>>n
        emit(out, "srai", "t1, " + src1Reg + ", 31");
        emit(out, "srli", "t1, t1, " + std::to_string(32 - shift));
        emit(out, "add", "t1, " + src1Reg + ", t1");
        emit(out, "srai", destReg + ", t1, " + std::to_string(shift));
      } else if (imm == -1) {
        emit(out, "sub", destReg + ", x0, " + src1Reg);
      } else if (imm != 0 && imm != 1 && emitMagicDiv(imm, src1Reg, destReg, out)) {
        // 已生成 magic 序列
      } else {
        emit(out, "li", "t1, " + std::to_string(imm));
        emit(out, "div", destReg + ", " + src1Reg + ", t1");
      }
      break;
    case IROp::MOD:
      if (imm > 0 && (imm & (imm - 1)) == 0) {
        // x % 2^n：已知非负时直接掩码（1 条指令）
        const int shift = __builtin_ctz(static_cast<unsigned>(imm));
        const bool srcNonNeg =
            inst.src1.isLocalVar() && isNonNegative(inst.src1.name, currentInstIndex_);
        if (srcNonNeg) {
          emit(out, "andi", destReg + ", " + src1Reg + ", " + std::to_string(imm - 1));
          break;
        }
        // x - (x / 2^n) * 2^n（对任意符号正确的向零取模）
        emit(out, "srai", "t1, " + src1Reg + ", 31");
        emit(out, "srli", "t1, t1, " + std::to_string(32 - shift));
        emit(out, "add", "t1, " + src1Reg + ", t1");
        emit(out, "srai", "t1, t1, " + std::to_string(shift));
        emit(out, "slli", "t1, t1, " + std::to_string(shift));
        emit(out, "sub", destReg + ", " + src1Reg + ", t1");
      } else if (imm == 1) {
        emit(out, "li", destReg + ", 0");
      } else if (imm == -1) {
        emit(out, "li", destReg + ", 0");
      } else if (imm != 0 && emitMagicMod(imm, src1Reg, destReg, out)) {
        // 已生成 magic 序列
      } else {
        emit(out, "li", "t1, " + std::to_string(imm));
        emit(out, "rem", destReg + ", " + src1Reg + ", t1");
      }
      break;
    default:
      break;
    }
  } else {
    switch (inst.op) {
    case IROp::ADD:
      emit(out, "add", destReg + ", " + src1Reg + ", " + src2Reg);
      break;
    case IROp::SUB:
      emit(out, "sub", destReg + ", " + src1Reg + ", " + src2Reg);
      break;
    case IROp::MUL:
      emit(out, "mul", destReg + ", " + src1Reg + ", " + src2Reg);
      break;
    case IROp::DIV:
      if (inst.src2.isImm() && emitMagicDiv(inst.src2.immVal, src1Reg, destReg, out)) {
        break;
      }
      emit(out, "div", destReg + ", " + src1Reg + ", " + src2Reg);
      break;
    case IROp::MOD:
      if (inst.src2.isImm() && emitMagicMod(inst.src2.immVal, src1Reg, destReg, out)) {
        break;
      }
      emit(out, "rem", destReg + ", " + src1Reg + ", " + src2Reg);
      break;
    default:
      break;
    }
  }

  if (!destInReg) {
    storeOperand(destReg, inst.dest, out);
  }
}

void CodeGenerator::emitCompareOp(const IRInst& inst, std::ostream& out) {
  // 确定目标寄存器：若 dest 分配了寄存器（s 或 t）则直接用该寄存器
  std::string destReg = "t0";
  bool destInReg = false;
  if (inst.dest.isLocalVar()) {
    const std::string reg = regForVar(inst.dest.name);
    if (!reg.empty()) {
      destReg = reg;
      destInReg = true;
    }
  } else if (inst.dest.isGlobalVar()) {
    auto it = frame_.globalRegAlloc.find(inst.dest.name);
    if (it != frame_.globalRegAlloc.end()) {
      destReg = "s" + std::to_string(it->second);
      destInReg = true;
    }
  }

  emitCompareInto(inst, destReg, out);

  if (!destInReg) {
    storeOperand(destReg, inst.dest, out);
  }
}

// 将比较结果计算到 destReg（调用方负责结果的存储/使用，此处不落栈）
void CodeGenerator::emitCompareInto(const IRInst& inst, std::string_view destReg,
                                    std::ostream& out) {
  const std::string dest = std::string(destReg);

  // 确定 src2 的寄存器：若已在寄存器中且不是 destReg，直接用该寄存器
  const bool src2IsSmallImm =
      inst.src2.isImm() && inst.src2.immVal >= -2048 && inst.src2.immVal <= 2047;
  int imm = 0;
  std::string src2Reg = "t1";
  bool src2InReg = false;
  if (src2IsSmallImm) {
    imm = inst.src2.immVal;
  } else if (inst.src2.isLocalVar()) {
    const std::string reg = regForVar(inst.src2.name);
    if (!reg.empty()) {
      if (reg != dest) {
        src2Reg = reg;
        src2InReg = true;
      }
    }
  }
  if (!src2IsSmallImm && !src2InReg) {
    loadOperand(inst.src2, "t1", out);
  }

  // 确定 src1 的寄存器：若已在寄存器中，直接用该寄存器做比较，避免 mv 到 destReg
  std::string src1Reg = dest;
  if (inst.src1.isLocalVar()) {
    const std::string reg = regForVar(inst.src1.name);
    if (!reg.empty()) {
      src1Reg = reg;
    } else {
      loadOperand(inst.src1, dest, out);
      src1Reg = dest;
    }
  } else {
    loadOperand(inst.src1, dest, out);
    src1Reg = dest;
  }

  if (src2IsSmallImm) {
    switch (inst.op) {
    case IROp::LT:
      emit(out, "slti", dest + ", " + src1Reg + ", " + std::to_string(imm));
      break;
    case IROp::GT:
      emit(out, "li", "t1, " + std::to_string(imm));
      emit(out, "slt", dest + ", t1, " + src1Reg);
      break;
    case IROp::LE:
      emit(out, "slti", dest + ", " + src1Reg + ", " + std::to_string(imm + 1));
      break;
    case IROp::GE:
      emit(out, "slti", dest + ", " + src1Reg + ", " + std::to_string(imm));
      emit(out, "xori", dest + ", " + dest + ", 1");
      break;
    case IROp::EQ:
      emit(out, "li", "t1, " + std::to_string(imm));
      emit(out, "sub", dest + ", " + src1Reg + ", t1");
      emit(out, "seqz", dest + ", " + dest);
      break;
    case IROp::NE:
      emit(out, "li", "t1, " + std::to_string(imm));
      emit(out, "sub", dest + ", " + src1Reg + ", t1");
      emit(out, "snez", dest + ", " + dest);
      break;
    default:
      break;
    }
  } else {
    switch (inst.op) {
    case IROp::LT:
      emit(out, "slt", dest + ", " + src1Reg + ", " + src2Reg);
      break;
    case IROp::GT:
      emit(out, "slt", dest + ", " + src2Reg + ", " + src1Reg);
      break;
    case IROp::LE:
      emit(out, "slt", dest + ", " + src2Reg + ", " + src1Reg);
      emit(out, "xori", dest + ", " + dest + ", 1");
      break;
    case IROp::GE:
      emit(out, "slt", dest + ", " + src1Reg + ", " + src2Reg);
      emit(out, "xori", dest + ", " + dest + ", 1");
      break;
    case IROp::EQ:
      emit(out, "sub", dest + ", " + src1Reg + ", " + src2Reg);
      emit(out, "seqz", dest + ", " + dest);
      break;
    case IROp::NE:
      emit(out, "sub", dest + ", " + src1Reg + ", " + src2Reg);
      emit(out, "snez", dest + ", " + dest);
      break;
    default:
      break;
    }
  }
}

void CodeGenerator::emitCall(const IRInst& inst, std::ostream& out) {
  if (inst.dest.type == OperandType::LOCAL_VAR || inst.dest.type == OperandType::GLOBAL_VAR) {
    emit(out, "call", inst.src1.name);
    storeOperand("a0", inst.dest, out);
  } else {
    emit(out, "call", inst.src1.name);
  }
}

std::vector<CodeGenerator::TailCallInfo>
CodeGenerator::detectTailCalls(const FunctionRange& function) const {
  std::vector<TailCallInfo> result;
  for (std::size_t i = function.begin; i + 1 < function.end; ++i) {
    const auto& inst = ir_[i];
    if (inst.op != IROp::CALL || !inst.src1.isFunc()) {
      continue;
    }
    const auto& next = ir_[i + 1];
    // 尾位置：CALL 之后立即是 RETURN，且 CALL 的结果（若有）正是该 RETURN 的返回值
    bool tailPosition = false;
    if (inst.dest.isLocalVar()) {
      tailPosition =
          (next.op == IROp::RETURN && next.dest.isLocalVar() && next.dest.name == inst.dest.name);
    } else if (inst.dest.isNone()) {
      tailPosition = (next.op == IROp::RETURN && next.dest.isNone());
    }
    if (!tailPosition) {
      continue;
    }
    // 确保 CALL 之后除 RETURN 外没有其他指令（避免死代码之外的控制流）
    bool trailingOnlyReturns = true;
    for (std::size_t j = i + 2; j < function.end; ++j) {
      if (ir_[j].op != IROp::RETURN) {
        trailingOnlyReturns = false;
        break;
      }
    }
    if (!trailingOnlyReturns) {
      continue;
    }
    // 检查是否有溢出参数（>8 个实参），尾调用下栈帧恢复会导致偏移错位，保守跳过
    bool hasOverflowArgs = false;
    for (std::size_t j = i; j > function.begin; --j) {
      const auto& prev = ir_[j - 1];
      if (prev.op == IROp::PARAM) {
        if (prev.src1.immVal >= 8) {
          hasOverflowArgs = true;
          break;
        }
      } else {
        break;
      }
    }
    if (hasOverflowArgs) {
      continue;
    }
    TailCallInfo info;
    info.callIndex = i;
    info.target = inst.src1.name;
    info.isSelf = (inst.src1.name == function.name);
    result.push_back(info);
  }
  return result;
}

// 分析函数内反转循环的计数变量及非负表达式，记录变量恒非负的 IR 索引范围：
//   BRANCH Lc; LABEL Lb; <body>; LABEL Lc; <cond>; BNEZ Lb
// 若计数变量初值非负（#imm, imm>=0），且循环体内对其的所有定义都是正步长
// 自增（i = i + c, c>0），则该变量在循环体内恒非负。在此基础上按基本块
// 直线传播：非负 + 非负（或非负立即数）的 ADD/ASSIGN 结果也非负。
// 范围内 2 的幂常量除法/取模可直接用 srai/andi（负数的 C 除/模语义不同，
// 因此范围必须精确限定到循环体/传播窗口内，循环外重新赋值不受影响）。
void CodeGenerator::analyzeNonNegativeVars(const FunctionRange& function) {
  nonNegativeRanges_.clear();
  const auto addRange = [&](const std::string& name, std::size_t begin, std::size_t end) {
    if (begin >= end) {
      return;
    }
    nonNegativeRanges_[name].push_back(NonNegRange{begin, end});
  };

  // 第一遍：识别循环计数变量种子，记录其循环体范围
  for (std::size_t i = function.begin; i + 1 < function.end; ++i) {
    if (ir_[i].op != IROp::BRANCH || !ir_[i].dest.isLabel()) {
      continue;
    }
    if (ir_[i + 1].op != IROp::LABEL) {
      continue;
    }
    const std::string condLabel = ir_[i].dest.name;
    const std::string bodyLabel = ir_[i + 1].dest.name;
    // 定位条件标签与循环回跳
    std::size_t ci = i + 2;
    for (; ci < function.end; ++ci) {
      if (ir_[ci].op == IROp::LABEL && ir_[ci].dest.name == condLabel) {
        break;
      }
    }
    if (ci >= function.end) {
      continue;
    }
    std::size_t bi = ci + 1;
    for (; bi < function.end; ++bi) {
      if (ir_[bi].op == IROp::BNEZ && ir_[bi].dest.name == bodyLabel) {
        break;
      }
    }
    if (bi >= function.end || bi == ci + 1) {
      continue;
    }
    // 条件块最后一条必须是 LT/LE（继续循环的比较），其 src1 为计数变量
    const IRInst& cond = ir_[bi - 1];
    if ((cond.op != IROp::LT && cond.op != IROp::LE) || !cond.src1.isLocalVar()) {
      continue;
    }
    const std::string indName = cond.src1.name;
    // 初值非负：循环入口前最近定义必须为 ASSIGN/LOCAL_VAR_DECL #imm(>=0)
    bool initNonNeg = false;
    for (std::size_t k = i; k > function.begin; --k) {
      const IRInst& prev = ir_[k - 1];
      if (prev.dest.isLocalVar() && prev.dest.name == indName) {
        if ((prev.op == IROp::ASSIGN || prev.op == IROp::LOCAL_VAR_DECL) && prev.src1.isImm() &&
            prev.src1.immVal >= 0) {
          initNonNeg = true;
        }
        break;
      }
      if (prev.op == IROp::LABEL || prev.op == IROp::BRANCH || prev.op == IROp::BEQZ ||
          prev.op == IROp::BNEZ || prev.op == IROp::CALL || prev.op == IROp::FUNC_BEGIN) {
        break;
      }
    }
    if (!initNonNeg) {
      continue;
    }
    // 循环体内（含条件块）对计数变量的所有定义都必须是正步长自增
    bool onlySelfInc = true;
    for (std::size_t k = i + 2; k < bi; ++k) {
      const IRInst& inst = ir_[k];
      if (!inst.dest.isLocalVar() || inst.dest.name != indName) {
        continue;
      }
      const bool selfInc = (inst.op == IROp::ADD || inst.op == IROp::SUB) &&
                           inst.src1.isLocalVar() && inst.src1.name == indName && inst.src2.isImm();
      if (!selfInc) {
        onlySelfInc = false;
        break;
      }
      const int step = (inst.op == IROp::ADD) ? inst.src2.immVal : -inst.src2.immVal;
      if (step <= 0) {
        onlySelfInc = false;
        break;
      }
    }
    if (onlySelfInc) {
      // 循环体 [i+2, bi) 内恒非负；条件块（bi-1 的 LT/LE）中 i 尚未自增到
      // 本轮末值，但仍由上一轮的非负值 + 正步长得到，同样非负，一并覆盖。
      addRange(indName, i + 2, bi + 1);
    }
  }

  // 第二遍：按基本块直线传播非负（块边界处清空，仅保留循环种子已覆盖的范围）。
  // 逐条记录"由非负源产生的定义"范围 [defIdx, nextDefIdx)。
  std::vector<std::pair<std::string, std::size_t>> curNonNeg; // 变量 -> 最近定义索引
  const auto flushWindow = [&](std::size_t end) {
    // curNonNeg 中每项的有效范围 [defIdx, end)，作为传播结果追加
    for (const auto& entry : curNonNeg) {
      addRange(entry.first, entry.second, end);
    }
    curNonNeg.clear();
  };
  for (std::size_t idx = function.begin; idx < function.end; ++idx) {
    const IRInst& inst = ir_[idx];
    // 基本块边界：分支/标签/调用/返回后传播窗口结束（保守）
    if (inst.op == IROp::LABEL || inst.op == IROp::BRANCH || inst.op == IROp::BEQZ ||
        inst.op == IROp::BNEZ || inst.op == IROp::CALL || inst.op == IROp::RETURN ||
        inst.op == IROp::FUNC_BEGIN || inst.op == IROp::FUNC_END) {
      flushWindow(idx);
      continue;
    }
    // 先判定源操作数（RISC-V read-before-write：dest 与 src 同名时，
    // 本指令读取的是旧值，须先查 src 再使 dest 的旧传播失效）
    const auto srcNonNeg = [&](const Operand& op, std::size_t atIdx) {
      if (op.isImm()) {
        return op.immVal >= 0;
      }
      if (op.isLocalVar()) {
        for (const auto& entry : curNonNeg) {
          if (entry.first == op.name) {
            return true;
          }
        }
        // 循环计数变量等种子范围同样视为非负源
        return isNonNegative(op.name, atIdx);
      }
      return false;
    };
    bool isNonNegDef = false;
    if ((inst.op == IROp::ASSIGN || inst.op == IROp::LOCAL_VAR_DECL) && inst.src1.isImm() &&
        inst.src1.immVal >= 0) {
      isNonNegDef = true;
    } else if (inst.op == IROp::ASSIGN && inst.src1.isLocalVar() && srcNonNeg(inst.src1, idx)) {
      isNonNegDef = true;
    } else if (inst.op == IROp::ADD && inst.dest.isLocalVar() && srcNonNeg(inst.src1, idx) &&
               srcNonNeg(inst.src2, idx)) {
      isNonNegDef = true;
    } else if ((inst.op == IROp::DIV || inst.op == IROp::MOD) && inst.dest.isLocalVar() &&
               srcNonNeg(inst.src1, idx) && inst.src2.isImm() && inst.src2.immVal > 0) {
      // 非负 / 正 与 非负 % 正 的结果仍非负
      isNonNegDef = true;
    }
    // 移除此指令重新定义的变量（其旧传播失效）；先提交旧窗口再移除，
    // 避免"定义后立即被重定义"（如 ADD t6; DIV t6, t6）丢失非负范围
    if (inst.dest.isLocalVar()) {
      for (auto it = curNonNeg.begin(); it != curNonNeg.end();) {
        if (it->first == inst.dest.name) {
          addRange(it->first, it->second, idx);
          it = curNonNeg.erase(it);
        } else {
          ++it;
        }
      }
    }
    if (isNonNegDef && inst.dest.isLocalVar()) {
      curNonNeg.emplace_back(inst.dest.name, idx);
    }
  }
  flushWindow(function.end);
}

bool CodeGenerator::isNonNegative(const std::string& name, std::size_t index) const {
  auto it = nonNegativeRanges_.find(name);
  if (it == nonNegativeRanges_.end()) {
    return false;
  }
  for (const auto& range : it->second) {
    if (index >= range.begin && index < range.end) {
      return true;
    }
  }
  return false;
}

void CodeGenerator::emitTailCall(const TailCallInfo& tailCall, std::ostream& out) const {
  if (tailCall.isSelf) {
    // 自递归尾调用：实参已在 a0-a7，直接跳回函数体入口（参数装载点），
    // 复用当前栈帧，等价于把递归转换成循环，避免每次迭代的帧设置/恢复开销
    emit(out, "j", ".L_" + frame_.functionName + "_body");
    return;
  }
  // 实参已由前置 PARAM 指令装载到 a0-a7，恢复被调用者保存寄存器后直接跳转
  int restoreOffset = -12;
  for (const auto& reg : frame_.usedCalleeSavedRegisters) {
    emit(out, "lw", reg + ", " + std::to_string(restoreOffset) + "(s0)");
    restoreOffset -= 4;
  }
  emit(out, "lw", "ra, -4(s0)");
  emit(out, "lw", "s0, -8(s0)");
  emit(out, "addi", "sp, sp, " + std::to_string(frame_.frameSizeBytes()));
  emit(out, "j", tailCall.target);
}

void CodeGenerator::scanUsedSRegisters(const std::string& asmText,
                                       std::vector<std::string>& used) const {
  std::vector<bool> seen(10, false);
  std::istringstream iss(asmText);
  std::string line;
  while (std::getline(iss, line)) {
    const AsmToken tok = parseAsmLine(line);
    for (const auto& arg : tok.args) {
      if (arg.size() >= 2 && arg[0] == 's' && arg.size() <= 3) {
        const bool isSReg =
            (arg == "s2" || arg == "s3" || arg == "s4" || arg == "s5" || arg == "s6" ||
             arg == "s7" || arg == "s8" || arg == "s9" || arg == "s10" || arg == "s11");
        if (isSReg) {
          const int idx = (arg.size() == 3) ? (arg[1] - '0') * 10 + (arg[2] - '0') : (arg[1] - '0');
          if (idx >= 2 && idx <= 11) {
            seen[static_cast<std::size_t>(idx - 2)] = true;
          }
        }
      }
    }
  }
  used.clear();
  for (int r = 2; r <= 11; ++r) {
    if (seen[static_cast<std::size_t>(r - 2)]) {
      used.push_back("s" + std::to_string(r));
    }
  }
}

std::string CodeGenerator::destRegOrT0(const Operand& dest) const {
  if (dest.isLocalVar()) {
    const std::string reg = regForVar(dest.name);
    if (!reg.empty()) {
      return reg;
    }
  }
  if (dest.isGlobalVar()) {
    auto it = frame_.globalRegAlloc.find(dest.name);
    if (it != frame_.globalRegAlloc.end()) {
      return "s" + std::to_string(it->second);
    }
  }
  return "t0";
}

bool CodeGenerator::isDestInReg(const Operand& dest) const {
  if (dest.isLocalVar()) {
    return varHasReg(dest.name);
  }
  if (dest.isGlobalVar()) {
    return frame_.globalRegAlloc.find(dest.name) != frame_.globalRegAlloc.end();
  }
  return false;
}

std::string CodeGenerator::regForVar(const std::string& name) const {
  auto lit = frame_.leafRegAlloc.find(name);
  if (lit != frame_.leafRegAlloc.end()) {
    return lit->second;
  }
  auto it = frame_.regAlloc.find(name);
  if (it != frame_.regAlloc.end()) {
    return "s" + std::to_string(it->second);
  }
  auto tit = frame_.tempRegs.find(name);
  if (tit != frame_.tempRegs.end()) {
    return tit->second;
  }
  return std::string();
}

bool CodeGenerator::varHasReg(const std::string& name) const {
  return !regForVar(name).empty();
}

std::string CodeGenerator::asmLabel(const std::string& label) const {
  return label;
}

std::string CodeGenerator::globalSymbol(const std::string& name) const {
  return name;
}

int CodeGenerator::localOffset(const Operand& operand) const {
  if (operand.type != OperandType::LOCAL_VAR) {
    return 0;
  }
  const auto it = frame_.localOffsets.find(operand.name);
  if (it == frame_.localOffsets.end() || it->second < 0) {
    return 0;
  }
  const int reservedWords = 2 + static_cast<int>(frame_.usedCalleeSavedRegisters.size());
  return -(kWordBytes * (reservedWords + it->second + 1));
}

int CodeGenerator::ensureLocalOffset(const Operand& operand) {
  if (operand.type != OperandType::LOCAL_VAR) {
    return 0;
  }
  const auto it = frame_.localOffsets.find(operand.name);
  if (it != frame_.localOffsets.end()) {
    return localOffset(operand);
  }
  const int newIndex = static_cast<int>(frame_.localOffsets.size());
  frame_.localOffsets[operand.name] = newIndex;
  return localOffset(operand);
}

void CodeGenerator::emit(std::ostream& out, std::string_view opcode,
                         std::string_view operands) const {
  out << "    " << opcode;
  if (!operands.empty()) {
    out << ' ' << operands;
  }
  out << '\n';
}

void CodeGenerator::emitRaw(std::ostream& out, std::string_view text) const {
  out << text;
}

} // namespace toycc
