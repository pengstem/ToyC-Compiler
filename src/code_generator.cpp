#include "code_generator.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <unordered_set>
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

bool fitsSigned12(int value) {
  return value >= -2048 && value <= 2047;
}

struct RawRemainderPattern {
  Operand source;
  Operand destination;
  int modulus = 0;
  std::size_t length = 0;
};

struct NormalizedRemainderPattern {
  Operand source;
  Operand destination;
  Operand intermediate;
  int modulus = 0;
  std::size_t length = 0;
};

bool sameOperand(const Operand& lhs, const Operand& rhs) {
  if (lhs.type != rhs.type) {
    return false;
  }
  if (lhs.isImm()) {
    return lhs.immVal == rhs.immVal;
  }
  return lhs.name == rhs.name;
}

std::optional<RawRemainderPattern> matchRawRemainder(const std::vector<IRInst>& ir,
                                                     std::size_t index, std::size_t end) {
  if (index >= end) {
    return std::nullopt;
  }
  const IRInst& first = ir[index];
  if (first.op == IROp::MOD && first.src2.isImm() && first.src2.immVal > 1 &&
      (first.dest.isLocalVar() || first.dest.isGlobalVar())) {
    return RawRemainderPattern{first.src1, first.dest, first.src2.immVal, 1};
  }
  if (index + 2 >= end || first.op != IROp::DIV || !first.dest.isLocalVar() ||
      !first.src2.isImm() || first.src2.immVal <= 1) {
    return std::nullopt;
  }
  const IRInst& product = ir[index + 1];
  const IRInst& remainder = ir[index + 2];
  const bool productMatches =
      product.op == IROp::MUL && product.dest.isLocalVar() &&
      ((sameOperand(product.src1, first.dest) && sameOperand(product.src2, first.src2)) ||
       (sameOperand(product.src2, first.dest) && sameOperand(product.src1, first.src2)));
  if (!productMatches || remainder.op != IROp::SUB ||
      (!remainder.dest.isLocalVar() && !remainder.dest.isGlobalVar()) ||
      !sameOperand(remainder.src1, first.src1) || !sameOperand(remainder.src2, product.dest)) {
    return std::nullopt;
  }
  return RawRemainderPattern{first.src1, remainder.dest, first.src2.immVal, 3};
}

std::optional<NormalizedRemainderPattern>
matchNormalizedRemainder(const std::vector<IRInst>& ir, std::size_t index, std::size_t end) {
  const auto first = matchRawRemainder(ir, index, end);
  if (!first) {
    return std::nullopt;
  }
  const std::size_t addIndex = index + first->length;
  if (addIndex >= end) {
    return std::nullopt;
  }
  const IRInst& add = ir[addIndex];
  if (add.op != IROp::ADD || !add.dest.isLocalVar() || !sameOperand(add.dest, first->destination) ||
      !sameOperand(add.src1, first->destination) || !add.src2.isImm() ||
      add.src2.immVal != first->modulus) {
    return std::nullopt;
  }
  const auto second = matchRawRemainder(ir, addIndex + 1, end);
  if (!second || second->modulus != first->modulus || !sameOperand(second->source, add.dest)) {
    return std::nullopt;
  }
  return NormalizedRemainderPattern{first->source, second->destination, add.dest, first->modulus,
                                    first->length + 1 + second->length};
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
  frame.constantRegAlloc.clear();
  frame.localBytes = 0;
  frame.outgoingArgumentBytes = 0;

  int localIndex = 0;
  int maxOverflowArgs = 0;
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

  // 第二遍：由后向边恢复循环区间，并按循环深度估算每次读写的动态成本。
  // 同一循环头可能因 continue 等控制流产生多条回边，只保留覆盖最广的一条，
  // 避免把一个循环误算成多层嵌套。每深入一层权重乘 8，最深封顶以免溢出。
  std::unordered_map<std::string, std::size_t> labelPositions;
  for (std::size_t i = function.begin; i < function.end; ++i) {
    if (ir_[i].op == IROp::LABEL && ir_[i].dest.isLabel()) {
      labelPositions[ir_[i].dest.name] = i;
    }
  }

  std::unordered_map<std::size_t, std::size_t> loopEndsByHeader;
  for (std::size_t i = function.begin; i < function.end; ++i) {
    const auto& inst = ir_[i];
    if ((inst.op != IROp::BRANCH && inst.op != IROp::BEQZ && inst.op != IROp::BNEZ) ||
        !inst.dest.isLabel()) {
      continue;
    }
    const auto target = labelPositions.find(inst.dest.name);
    if (target == labelPositions.end() || target->second >= i) {
      continue;
    }
    auto [found, inserted] = loopEndsByHeader.emplace(target->second, i);
    if (!inserted) {
      found->second = std::max(found->second, i);
    }
  }

  std::vector<unsigned> loopDepth(function.end - function.begin, 0);
  for (const auto& [header, end] : loopEndsByHeader) {
    for (std::size_t i = header; i <= end; ++i) {
      auto& depth = loopDepth[i - function.begin];
      depth = std::min(depth + 1, 7u);
    }
  }
  const auto useWeightAt = [&](std::size_t index) -> std::uint64_t {
    return std::uint64_t{1} << (3u * loopDepth[index - function.begin]);
  };
  const auto addWeight = [](auto& weights, const std::string& name, std::uint64_t amount) {
    auto& value = weights[name];
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    value = maximum - value < amount ? maximum : value + amount;
  };

  std::unordered_map<std::string, std::uint64_t> useWeight;
  // 全局变量使用权重（仅当函数无函数调用时启用寄存器分配，避免跨调用同步）
  std::unordered_map<std::string, std::uint64_t> globalUseWeight;
  // 热循环中需要寄存器物化的立即数。权重近似为动态执行次数乘以 `li`
  // 展开的指令数；只在循环内记分，避免为冷常量付出保存寄存器的代价。
  std::unordered_map<int, std::uint64_t> constantUseWeight;
  const auto noteHotConstant = [&](int value, std::uint64_t weight) {
    if (value == 0 || weight <= 1) {
      return;
    }
    const std::uint64_t materializeCost = (value >= -2048 && value <= 2047) ? 1 : 2;
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    const std::uint64_t amount =
        weight > maximum / materializeCost ? maximum : weight * materializeCost;
    auto& score = constantUseWeight[value];
    score = maximum - score < amount ? maximum : score + amount;
  };
  const auto noteMagicConstant = [&](int divisor, std::uint64_t weight) {
    if (divisor == 0 || divisor == 1 || divisor == -1 || divisor == INT32_MIN) {
      return false;
    }
    const int32_t positiveDivisor =
        divisor < 0 ? static_cast<int32_t>(-static_cast<int64_t>(divisor)) : divisor;
    uint32_t magic = 0;
    int32_t shift = 0;
    if (!computeSignedDivMagic(positiveDivisor, magic, shift)) {
      return false;
    }
    noteHotConstant(static_cast<int32_t>(magic), weight);
    return true;
  };
  bool hasCall = false;
  for (std::size_t i = function.begin; i < function.end; ++i) {
    const auto& inst = ir_[i];
    const std::uint64_t weight = useWeightAt(i);
    if (inst.op == IROp::CALL) {
      hasCall = true;
    }
    if (inst.src1.isLocalVar()) {
      addWeight(useWeight, inst.src1.name, weight);
    }
    if (inst.src2.isLocalVar()) {
      addWeight(useWeight, inst.src2.name, weight);
    }
    // 无初始化声明不产生机器指令，不能因声明本身获得寄存器优先级。
    if (inst.dest.isLocalVar() && !(inst.op == IROp::LOCAL_VAR_DECL && inst.src1.isNone())) {
      addWeight(useWeight, inst.dest.name, weight);
    }
    if (inst.src1.isGlobalVar()) {
      addWeight(globalUseWeight, inst.src1.name, weight);
    }
    if (inst.src2.isGlobalVar()) {
      addWeight(globalUseWeight, inst.src2.name, weight);
    }
    if (inst.dest.isGlobalVar()) {
      addWeight(globalUseWeight, inst.dest.name, weight);
    }

    if (weight > 1) {
      const bool arithmeticOrCompare =
          inst.op == IROp::ADD || inst.op == IROp::SUB || inst.op == IROp::MUL ||
          inst.op == IROp::DIV || inst.op == IROp::MOD || inst.op == IROp::LT ||
          inst.op == IROp::GT || inst.op == IROp::LE || inst.op == IROp::GE ||
          inst.op == IROp::EQ || inst.op == IROp::NE;
      if (arithmeticOrCompare && inst.src1.isImm()) {
        noteHotConstant(inst.src1.immVal, weight);
      } else if ((inst.op == IROp::ASSIGN || inst.op == IROp::LOCAL_VAR_DECL) &&
                 inst.src1.isImm() && (inst.src1.immVal < -2048 || inst.src1.immVal > 2047)) {
        // `mv` 只比多指令 `li` 更便宜；12 位初始化本身已是一条指令。
        noteHotConstant(inst.src1.immVal, weight);
      }

      if (inst.src2.isImm()) {
        const int value = inst.src2.immVal;
        const bool fits12 = value >= -2048 && value <= 2047;
        switch (inst.op) {
        case IROp::ADD:
        case IROp::SUB:
          if (!fits12) {
            noteHotConstant(value, weight);
          }
          break;
        case IROp::MUL: {
          const int64_t magnitude = value < 0 ? -static_cast<int64_t>(value) : value;
          const bool powerOfTwo = magnitude > 0 && (magnitude & (magnitude - 1)) == 0;
          const bool shiftAdd = value == 3 || value == 5 || value == 7 || value == 9 ||
                                value == 15 || value == -1 || powerOfTwo;
          if (!shiftAdd) {
            noteHotConstant(value, weight);
          }
          break;
        }
        case IROp::DIV: {
          const bool positivePowerOfTwo = value > 0 && (value & (value - 1)) == 0;
          if (!positivePowerOfTwo && value != -1 && !noteMagicConstant(value, weight)) {
            noteHotConstant(value, weight);
          }
          break;
        }
        case IROp::MOD: {
          const bool positivePowerOfTwo = value > 0 && (value & (value - 1)) == 0;
          if (!positivePowerOfTwo && value != 1 && value != -1) {
            if (noteMagicConstant(value, weight)) {
              noteHotConstant(value, weight); // 余数序列还要乘回除数
            } else {
              noteHotConstant(value, weight);
            }
          }
          break;
        }
        case IROp::GT:
        case IROp::EQ:
        case IROp::NE:
          noteHotConstant(value, weight);
          break;
        case IROp::LT:
        case IROp::LE:
        case IROp::GE:
          if (!fits12) {
            noteHotConstant(value, weight);
          }
          break;
        default:
          break;
        }
      }

      if (inst.op == IROp::PARAM && inst.dest.isImm()) {
        noteHotConstant(inst.dest.immVal, weight);
      }
      if (inst.op == IROp::RETURN && inst.dest.isImm() &&
          (inst.dest.immVal < -2048 || inst.dest.immVal > 2047)) {
        noteHotConstant(inst.dest.immVal, weight);
      }
    }
  }
  frame.hasCall = hasCall;

  for (const auto& name : varOrder) {
    useWeight.try_emplace(name, 0);
  }

  // 按估算的动态读写成本降序排列，让循环状态优先占用稀缺寄存器。
  std::stable_sort(
      varOrder.begin(), varOrder.end(),
      [&](const std::string& a, const std::string& b) { return useWeight[a] > useWeight[b]; });

  // 临时变量 t 寄存器分配（t4-t6）：
  // 一个 IR 名可能被临时回收 pass 在多个基本块内重新定义。把每次定义切成独立
  // live range，只要求每段的使用不跨基本块/调用，再按所有分段的冲突关系着色。
  // 这避免把互不同时存活的分支临时各占一个 s 寄存器并在每次叶函数调用时保存。
  frame.tempRegs.clear();
  {
    bool hasDirectModulo = false;
    for (std::size_t idx = function.begin; idx < function.end; ++idx) {
      if (ir_[idx].op == IROp::MOD) {
        hasDirectModulo = true;
        break;
      }
    }
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
      // 即使某一段定义没有使用，机器指令仍会写该寄存器。把零长度段作为真实的
      // 点冲突保留在图中；删除它会允许该死写覆盖同寄存器中仍存活的参数/局部值。
      if (!ok || var.ranges.empty()) {
        continue;
      }
      candidates.push_back(std::move(var));
    }
    std::sort(candidates.begin(), candidates.end(), [](const VarRanges& a, const VarRanges& b) {
      return a.ranges.front().start < b.ranges.front().start;
    });
    const std::vector<std::string> tempRegisters =
        hasDirectModulo ? std::vector<std::string>{"t4", "t5", "t6"}
                        : std::vector<std::string>{"t3", "t4", "t5", "t6"};
    std::vector<std::vector<LiveRange>> assigned(tempRegisters.size());
    const auto overlaps = [](const LiveRange& a, const LiveRange& b) {
      return a.start <= b.end && b.start <= a.end;
    };
    for (const auto& candidate : candidates) {
      int color = -1;
      for (std::size_t r = 0; r < tempRegisters.size() && color < 0; ++r) {
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
          color = static_cast<int>(r);
        }
      }
      if (color >= 0) {
        frame.tempRegs[candidate.name] = tempRegisters[static_cast<std::size_t>(color)];
        auto& occupied = assigned[static_cast<std::size_t>(color)];
        occupied.insert(occupied.end(), candidate.ranges.begin(), candidate.ranges.end());
      }
    }
  }

  // 为剩余局部变量构造指令级活跃性冲突图并着色。旧分配器对每个变量永久
  // 占用一个寄存器，生命周期互不相交的复制链也会产生 mv，并在图/矩阵类
  // 大函数中很快耗尽寄存器。冲突图允许不同时存活的变量复用同一物理寄存器；
  // ASSIGN 的源/目标不建立冲突边，并优先选择复制伙伴的颜色以完成合并。
  std::unordered_map<std::string, std::unordered_set<std::string>> interference;
  std::unordered_map<std::string, std::unordered_set<std::string>> copyPartners;
  {
    const std::size_t instructionCount = function.end - function.begin;
    std::vector<std::vector<std::size_t>> successors(instructionCount);
    for (std::size_t index = function.begin; index < function.end; ++index) {
      const IRInst& inst = ir_[index];
      const std::size_t relative = index - function.begin;
      const auto addTarget = [&](const Operand& target) {
        if (!target.isLabel()) {
          return;
        }
        const auto found = labelPositions.find(target.name);
        if (found != labelPositions.end() && found->second >= function.begin &&
            found->second < function.end) {
          successors[relative].push_back(found->second - function.begin);
        }
      };
      if (inst.op == IROp::BRANCH) {
        addTarget(inst.dest);
      } else if (inst.op == IROp::BEQZ || inst.op == IROp::BNEZ) {
        addTarget(inst.dest);
        if (index + 1 < function.end) {
          successors[relative].push_back(relative + 1);
        }
      } else if (inst.op != IROp::RETURN && index + 1 < function.end) {
        successors[relative].push_back(relative + 1);
      }
    }

    std::vector<std::unordered_set<std::string>> liveIn(instructionCount);
    std::vector<std::unordered_set<std::string>> liveOut(instructionCount);
    bool livenessChanged = true;
    while (livenessChanged) {
      livenessChanged = false;
      for (std::size_t offset = instructionCount; offset-- > 0;) {
        const IRInst& inst = ir_[function.begin + offset];
        std::unordered_set<std::string> newOut;
        for (const std::size_t successor : successors[offset]) {
          newOut.insert(liveIn[successor].begin(), liveIn[successor].end());
        }
        std::unordered_set<std::string> newIn = newOut;
        const bool definesLocal =
            inst.dest.isLocalVar() && inst.op != IROp::RETURN && inst.op != IROp::PARAM;
        if (definesLocal) {
          newIn.erase(inst.dest.name);
        }
        if (inst.src1.isLocalVar()) {
          newIn.insert(inst.src1.name);
        }
        if (inst.src2.isLocalVar()) {
          newIn.insert(inst.src2.name);
        }
        if ((inst.op == IROp::RETURN || inst.op == IROp::PARAM) && inst.dest.isLocalVar()) {
          newIn.insert(inst.dest.name);
        }
        if (newIn != liveIn[offset] || newOut != liveOut[offset]) {
          liveIn[offset] = std::move(newIn);
          liveOut[offset] = std::move(newOut);
          livenessChanged = true;
        }
      }
    }

    const auto addInterference = [&](const std::string& lhs, const std::string& rhs) {
      if (lhs == rhs || frame.tempRegs.count(lhs) != 0 || frame.tempRegs.count(rhs) != 0) {
        return;
      }
      interference[lhs].insert(rhs);
      interference[rhs].insert(lhs);
    };
    for (std::size_t offset = 0; offset < instructionCount; ++offset) {
      const IRInst& inst = ir_[function.begin + offset];
      const bool definesLocal =
          inst.dest.isLocalVar() && inst.op != IROp::RETURN && inst.op != IROp::PARAM;
      if (!definesLocal || frame.tempRegs.count(inst.dest.name) != 0) {
        continue;
      }
      const bool isCopy = inst.op == IROp::ASSIGN && inst.src1.isLocalVar();
      if (isCopy && frame.tempRegs.count(inst.src1.name) == 0) {
        copyPartners[inst.dest.name].insert(inst.src1.name);
        copyPartners[inst.src1.name].insert(inst.dest.name);
      }
      for (const auto& live : liveOut[offset]) {
        if (isCopy && live == inst.src1.name) {
          continue;
        }
        addInterference(inst.dest.name, live);
      }
    }
  }

  std::unordered_map<std::string, std::string> assignedRegisters;
  if (!hasCall) {
    // 形参预着色到自己的 ABI 输入寄存器，避免入口处的平行搬运互相覆盖。
    for (const auto& entry : incomingParamRegs) {
      if (frame.tempRegs.count(entry.first) == 0) {
        const std::string reg = "a" + std::to_string(entry.second);
        frame.leafRegAlloc[entry.first] = reg;
        assignedRegisters[entry.first] = reg;
      }
    }
  }

  std::vector<std::string> allocatableRegisters;
  if (!hasCall) {
    for (int reg = 0; reg < 8; ++reg) {
      allocatableRegisters.push_back("a" + std::to_string(reg));
    }
  }
  for (int reg = 2; reg <= 11; ++reg) {
    allocatableRegisters.push_back("s" + std::to_string(reg));
  }

  for (const auto& varName : varOrder) {
    if (frame.tempRegs.count(varName) != 0 || assignedRegisters.count(varName) != 0) {
      continue;
    }
    std::vector<std::string> preferred;
    const auto partners = copyPartners.find(varName);
    if (partners != copyPartners.end()) {
      for (const auto& partner : partners->second) {
        const auto assigned = assignedRegisters.find(partner);
        if (assigned != assignedRegisters.end()) {
          preferred.push_back(assigned->second);
        }
      }
    }
    preferred.insert(preferred.end(), allocatableRegisters.begin(), allocatableRegisters.end());

    std::string selected;
    for (const auto& candidate : preferred) {
      bool conflicts = false;
      const auto neighbors = interference.find(varName);
      if (neighbors != interference.end()) {
        for (const auto& neighbor : neighbors->second) {
          const auto assigned = assignedRegisters.find(neighbor);
          if (assigned != assignedRegisters.end() && assigned->second == candidate) {
            conflicts = true;
            break;
          }
        }
      }
      if (!conflicts) {
        selected = candidate;
        break;
      }
    }
    if (selected.empty()) {
      continue;
    }
    assignedRegisters[varName] = selected;
    if (selected[0] == 'a') {
      frame.leafRegAlloc[varName] = selected;
    } else {
      frame.regAlloc[varName] = std::stoi(selected.substr(1));
    }
  }

  std::array<bool, 12> occupiedSRegisters{};
  for (const auto& entry : frame.regAlloc) {
    occupiedSRegisters[static_cast<std::size_t>(entry.second)] = true;
  }

  // 全局变量寄存器分配：仅当函数无函数调用时（调用者可能修改全局变量，寄存器中的
  // 副本会失效）。叶函数除剩余 s 寄存器外还可安全使用未分配的 a1-a7；a0
  // 留给返回值，否则在公共退出块存回全局变量时会把返回值误写回。标量矩阵类
  // 循环常有十几个全局状态，这 7 个额外寄存器可消除每轮反复的 la+lw/sw。
  if (!hasCall) {
    std::vector<std::string> globalOrder;
    for (const auto& entry : globalUseWeight) {
      globalOrder.push_back(entry.first);
    }
    std::stable_sort(globalOrder.begin(), globalOrder.end(),
                     [&](const std::string& a, const std::string& b) {
                       return globalUseWeight[a] > globalUseWeight[b];
                     });
    std::unordered_set<std::string> occupiedRegisters;
    for (const auto& entry : assignedRegisters) {
      occupiedRegisters.insert(entry.second);
    }
    std::vector<std::string> globalRegisters;
    for (int reg = 1; reg < 8; ++reg) {
      const std::string name = "a" + std::to_string(reg);
      if (occupiedRegisters.count(name) == 0) {
        globalRegisters.push_back(name);
      }
    }
    for (int reg = 2; reg <= 11; ++reg) {
      if (!occupiedSRegisters[static_cast<std::size_t>(reg)]) {
        globalRegisters.push_back("s" + std::to_string(reg));
      }
    }
    const std::size_t count = std::min(globalOrder.size(), globalRegisters.size());
    for (std::size_t index = 0; index < count; ++index) {
      frame.globalRegAlloc[globalOrder[index]] = globalRegisters[index];
      const std::string& reg = globalRegisters[index];
      if (!reg.empty() && reg[0] == 's') {
        occupiedSRegisters[static_cast<std::size_t>(std::stoi(reg.substr(1)))] = true;
      }
    }
  }

  // 把最热的循环常量放进变量/全局分配后仍空闲的寄存器。叶函数优先用
  // caller-saved a 寄存器；含调用函数只能用跨调用保值的 s 寄存器。
  // 限制为 6 个，避免极端函数为边际常量扩大过多保存现场。
  {
    std::vector<std::pair<int, std::uint64_t>> rankedConstants(constantUseWeight.begin(),
                                                               constantUseWeight.end());
    std::stable_sort(rankedConstants.begin(), rankedConstants.end(),
                     [](const auto& lhs, const auto& rhs) {
                       if (lhs.second != rhs.second) {
                         return lhs.second > rhs.second;
                       }
                       return lhs.first < rhs.first;
                     });
    rankedConstants.erase(std::remove_if(rankedConstants.begin(), rankedConstants.end(),
                                         [](const auto& entry) { return entry.second < 8; }),
                          rankedConstants.end());

    std::vector<std::string> spareRegisters;
    if (!hasCall) {
      bool needsRemainderScratch = false;
      for (std::size_t index = function.begin; index < function.end; ++index) {
        const IRInst& inst = ir_[index];
        if (inst.op != IROp::MOD) {
          continue;
        }
        bool sourceInRegister = false;
        if (inst.src1.isLocalVar()) {
          sourceInRegister = frame.tempRegs.count(inst.src1.name) != 0 ||
                             frame.leafRegAlloc.count(inst.src1.name) != 0 ||
                             frame.regAlloc.count(inst.src1.name) != 0;
        } else if (inst.src1.isGlobalVar()) {
          sourceInRegister = frame.globalRegAlloc.count(inst.src1.name) != 0;
        }
        if (!sourceInRegister) {
          needsRemainderScratch = true;
          break;
        }
      }
      // t3 只在直接 MOD 的被除数落到 t0 时用于保值。常见的优化 IR 已把
      // 余数拆成 DIV/MUL/SUB；若函数中不存在这种冲突，叶函数可把 t3 用作
      // 第 19 个长期寄存器，尤其适合提升常量除法的 magic multiplier。
      const bool t3Occupied = std::any_of(frame.tempRegs.begin(), frame.tempRegs.end(),
                                          [](const auto& entry) { return entry.second == "t3"; });
      if (!needsRemainderScratch && !t3Occupied) {
        spareRegisters.push_back("t3");
      }
      bool usedArgRegs[8] = {false, false, false, false, false, false, false, false};
      // 即使形参入口后会搬到 t 寄存器，也不能在搬运前用常量覆盖其 ABI 寄存器。
      for (const auto& entry : incomingParamRegs) {
        usedArgRegs[entry.second] = true;
      }
      for (const auto& entry : frame.leafRegAlloc) {
        if (entry.second.size() == 2 && entry.second[0] == 'a') {
          const int index = entry.second[1] - '0';
          if (index >= 0 && index < 8) {
            usedArgRegs[index] = true;
          }
        }
      }
      for (const auto& entry : frame.globalRegAlloc) {
        const std::string& reg = entry.second;
        if (reg.size() == 2 && reg[0] == 'a') {
          const int index = reg[1] - '0';
          if (index >= 0 && index < 8) {
            usedArgRegs[index] = true;
          }
        }
      }
      for (int r = 0; r < 8; ++r) {
        if (!usedArgRegs[r]) {
          spareRegisters.push_back("a" + std::to_string(r));
        }
      }
    }
    for (int reg = 2; reg <= 11; ++reg) {
      if (!occupiedSRegisters[static_cast<std::size_t>(reg)]) {
        spareRegisters.push_back("s" + std::to_string(reg));
      }
    }

    const std::size_t hoistCount =
        std::min({rankedConstants.size(), spareRegisters.size(), std::size_t{6}});
    for (std::size_t index = 0; index < hoistCount; ++index) {
      frame.constantRegAlloc[rankedConstants[index].first] = spareRegisters[index];
    }
  }

  // 已分配寄存器（s 或 t）的局部变量无需栈槽。其余变量按动态使用权重重排：
  // 最热的溢出值靠近 s0，既稳定输出，也尽量让循环内访存使用单条 12 位 lw/sw。
  std::vector<std::pair<std::string, int>> spilledLocals;
  for (auto& entry : frame.localOffsets) {
    if (frame.regAlloc.find(entry.first) != frame.regAlloc.end() ||
        frame.tempRegs.find(entry.first) != frame.tempRegs.end() ||
        frame.leafRegAlloc.find(entry.first) != frame.leafRegAlloc.end()) {
      entry.second = -1; // 标记：无栈槽
    } else {
      spilledLocals.emplace_back(entry.first, entry.second);
    }
  }
  std::stable_sort(
      spilledLocals.begin(), spilledLocals.end(), [&](const auto& lhs, const auto& rhs) {
        const std::uint64_t lhsWeight = useWeight[lhs.first];
        const std::uint64_t rhsWeight = useWeight[rhs.first];
        return lhsWeight != rhsWeight ? lhsWeight > rhsWeight : lhs.second < rhs.second;
      });
  for (std::size_t index = 0; index < spilledLocals.size(); ++index) {
    frame.localOffsets[spilledLocals[index].first] = static_cast<int>(index);
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
        if (it != frame.globalRegAlloc.end() && !it->second.empty() && it->second[0] == 's') {
          const int number = std::stoi(it->second.substr(1));
          if (number >= 2 && number <= 11) {
            regUsed[static_cast<std::size_t>(number - 2)] = true;
          }
        }
      }
    }
  }
  for (const auto& entry : frame.constantRegAlloc) {
    const std::string& reg = entry.second;
    if (reg.size() >= 2 && reg[0] == 's') {
      const int number = std::stoi(reg.substr(1));
      if (number >= 2 && number <= 11) {
        regUsed[static_cast<std::size_t>(number - 2)] = true;
      }
    }
  }

  // 更新 usedCalleeSavedRegisters：只保存实际使用的 s 寄存器
  frame.usedCalleeSavedRegisters.clear();
  for (int r = 2; r <= 11; ++r) {
    if (regUsed[static_cast<std::size_t>(r - 2)]) {
      frame.usedCalleeSavedRegisters.push_back("s" + std::to_string(r));
    }
  }

  frame.localBytes = static_cast<int>(spilledLocals.size()) * kWordBytes;
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

  // 热循环常量只在函数入口物化一次。它们位于递归回跳标签之前，因此自尾递归
  // 转成循环后也不会重复加载。
  std::vector<std::pair<int, std::string>> hoistedConstants(frame_.constantRegAlloc.begin(),
                                                            frame_.constantRegAlloc.end());
  std::stable_sort(hoistedConstants.begin(), hoistedConstants.end(),
                   [](const auto& lhs, const auto& rhs) { return lhs.second < rhs.second; });
  for (const auto& entry : hoistedConstants) {
    emit(body, "li", entry.second + ", " + std::to_string(entry.first));
  }

  // 全局变量寄存器副本：函数入口加载一次（位于 .L_body 之前，
  // 自递归尾调用跳回 .L_body 时不会重复加载，保留迭代间的值）
  for (const auto& entry : frame_.globalRegAlloc) {
    emit(body, "la", "t2, " + globalSymbol(entry.first));
    emit(body, "lw", entry.second + ", 0(t2)");
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
    emit(body, "la", "t2, " + globalSymbol(entry.first));
    emit(body, "sw", entry.second + ", 0(t2)");
  }

  emitEpilogue(frame_, body);
  body << "    .size " << function.name << ", .-" << function.name << "\n";

  applyPeephole(body.str(), out);
}

std::size_t CodeGenerator::generateInstruction(std::size_t index, std::size_t end,
                                               std::ostream& out) {
  currentInstIndex_ = index;
  const IRInst& inst = ir_[index];

  // `((x % m) + m) % m` 是前端为数学非负模生成的规范形式。若 x 已知非负，
  // 第一次余数已经位于 [0,m)，后两条运算完全冗余；若 m 是 2 的幂，则对任意
  // int32 x，规范结果都等于保留低 log2(m) 位，可直接用两条移位完成。这里在
  // 后端窗口内融合，兼容 Pass 0d 已把普通 MOD 展开成 DIV/MUL/SUB 的两种 IR。
  if (const auto normalized = matchNormalizedRemainder(ir_, index, end)) {
    bool intermediateLive = false;
    if (!sameOperand(normalized->intermediate, normalized->destination) &&
        normalized->intermediate.isLocalVar()) {
      for (std::size_t position = index + normalized->length; position < end; ++position) {
        const IRInst& candidate = ir_[position];
        const bool used =
            (candidate.src1.isLocalVar() && candidate.src1.name == normalized->intermediate.name) ||
            (candidate.src2.isLocalVar() && candidate.src2.name == normalized->intermediate.name) ||
            ((candidate.op == IROp::RETURN || candidate.op == IROp::PARAM) &&
             candidate.dest.isLocalVar() && candidate.dest.name == normalized->intermediate.name);
        if (used) {
          intermediateLive = true;
          break;
        }
        if (candidate.dest.isLocalVar() && candidate.dest.name == normalized->intermediate.name) {
          break;
        }
      }
    }
    const bool powerOfTwo = (normalized->modulus & (normalized->modulus - 1)) == 0;
    const bool sourceNonnegative =
        (normalized->source.isImm() && normalized->source.immVal >= 0) ||
        (normalized->source.isLocalVar() && isNonNegative(normalized->source.name, index));
    if (!intermediateLive && (powerOfTwo || sourceNonnegative)) {
      if (powerOfTwo) {
        const int valueBits = __builtin_ctz(static_cast<unsigned>(normalized->modulus));
        const int clearBits = 32 - valueBits;
        const std::string destination = destRegOrT0(normalized->destination);
        loadOperand(normalized->source, destination, out);
        emit(out, "slli", destination + ", " + destination + ", " + std::to_string(clearBits));
        emit(out, "srli", destination + ", " + destination + ", " + std::to_string(clearBits));
        if (!isDestInReg(normalized->destination)) {
          storeOperand(destination, normalized->destination, out);
        }
      } else {
        emitBinaryOp(IRInst(IROp::MOD, normalized->destination, normalized->source,
                            Operand::imm(normalized->modulus)),
                     out);
      }
      return normalized->length;
    }
  }

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
            emitLoadFromAddress(destReg, "s0", stackOffset, out);
          } else {
            emitLoadFromAddress("t0", "s0", stackOffset, out);
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
        emitLoadImmediate(inst.dest.immVal, reg, out);
      } else {
        loadOperand(inst.dest, reg, out);
      }
    } else {
      // 溢出参数存入栈（调用者栈帧低地址区）
      const int stackOffset = (argIndex - 8) * kWordBytes;
      if (inst.dest.type == OperandType::IMM) {
        emitLoadImmediate(inst.dest.immVal, "t0", out);
      } else {
        loadOperand(inst.dest, "t0", out);
      }
      emitStoreToAddress("t0", "sp", stackOffset, out);
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
        out << "    beqz " << it->second << ", " << asmLabel(inst.dest.name) << "\n";
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
        out << "    bnez " << it->second << ", " << asmLabel(inst.dest.name) << "\n";
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
  adjustStackPointer(-frameSize, out);
  // 在更新 s0 之前保存 ra 和旧 s0（使用 sp 相对偏移）
  if (frame.hasCall) {
    emitStoreToAddress("ra", "sp", frameSize - 4, out);
  }
  emitStoreToAddress("s0", "sp", frameSize - 8, out);
  // 设置新帧指针
  if (fitsSigned12(frameSize)) {
    emit(out, "addi", "s0, sp, " + std::to_string(frameSize));
  } else {
    emit(out, "li", "t2, " + std::to_string(frameSize));
    emit(out, "add", "s0, sp, t2");
  }

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
  if (frame.hasCall) {
    emit(out, "lw", "ra, -4(s0)");
  }
  emit(out, "lw", "s0, -8(s0)");
  adjustStackPointer(frame.frameSizeBytes(), out);
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

std::string CodeGenerator::regForConstant(int value) const {
  const auto found = frame_.constantRegAlloc.find(value);
  return found == frame_.constantRegAlloc.end() ? std::string() : found->second;
}

void CodeGenerator::emitLoadImmediate(int value, std::string_view reg, std::ostream& out) const {
  const std::string constantReg = regForConstant(value);
  if (!constantReg.empty()) {
    if (reg != constantReg) {
      emit(out, "mv", std::string(reg) + ", " + constantReg);
    }
    return;
  }
  emit(out, "li", std::string(reg) + ", " + std::to_string(value));
}

void CodeGenerator::emitLoadFromAddress(std::string_view reg, std::string_view base, int offset,
                                        std::ostream& out) const {
  if (fitsSigned12(offset)) {
    emit(out, "lw",
         std::string(reg) + ", " + std::to_string(offset) + "(" + std::string(base) + ")");
    return;
  }
  const std::string scratch = reg == "t2" ? "t1" : "t2";
  emit(out, "li", scratch + ", " + std::to_string(offset));
  emit(out, "add", scratch + ", " + std::string(base) + ", " + scratch);
  emit(out, "lw", std::string(reg) + ", 0(" + scratch + ")");
}

void CodeGenerator::emitStoreToAddress(std::string_view reg, std::string_view base, int offset,
                                       std::ostream& out) const {
  if (fitsSigned12(offset)) {
    emit(out, "sw",
         std::string(reg) + ", " + std::to_string(offset) + "(" + std::string(base) + ")");
    return;
  }
  const std::string scratch = reg == "t2" ? "t1" : "t2";
  emit(out, "li", scratch + ", " + std::to_string(offset));
  emit(out, "add", scratch + ", " + std::string(base) + ", " + scratch);
  emit(out, "sw", std::string(reg) + ", 0(" + scratch + ")");
}

void CodeGenerator::adjustStackPointer(int amount, std::ostream& out) const {
  if (fitsSigned12(amount)) {
    emit(out, "addi", "sp, sp, " + std::to_string(amount));
    return;
  }
  emit(out, "li", "t2, " + std::to_string(amount));
  emit(out, "add", "sp, sp, t2");
}

void CodeGenerator::loadOperand(const Operand& operand, std::string_view reg, std::ostream& out) {
  if (operand.type == OperandType::IMM) {
    emitLoadImmediate(operand.immVal, reg, out);
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
    emitLoadFromAddress(reg, "s0", offset, out);
    return;
  }

  if (operand.type == OperandType::GLOBAL_VAR) {
    // 若全局变量分配了寄存器，直接从寄存器复制
    auto it = frame_.globalRegAlloc.find(operand.name);
    if (it != frame_.globalRegAlloc.end()) {
      if (std::string(reg) != it->second) {
        emit(out, "mv", std::string(reg) + ", " + it->second);
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
    emitStoreToAddress(reg, "s0", offset, out);
    return;
  }

  if (operand.type == OperandType::GLOBAL_VAR) {
    // 若全局变量分配了寄存器，直接存到寄存器
    auto it = frame_.globalRegAlloc.find(operand.name);
    if (it != frame_.globalRegAlloc.end()) {
      if (std::string(reg) != it->second) {
        emit(out, "mv", it->second + ", " + std::string(reg));
      }
      return;
    }
    emit(out, "la", "t2, " + globalSymbol(operand.name));
    emit(out, "sw", std::string(reg) + ", 0(t2)");
    return;
  }
}

bool CodeGenerator::emitMagicDiv(int imm, const std::string& srcReg, const std::string& destReg,
                                 std::ostream& out, bool sourceNonnegative) const {
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
  std::string magicReg = regForConstant(magicSigned);
  if (magicReg.empty()) {
    emit(out, "li", "t1, " + std::to_string(magicSigned));
    magicReg = "t1";
  }
  emit(out, "mulh", "t2, " + srcReg + ", " + magicReg);
  if (magicSigned < 0) {
    emit(out, "add", "t2, t2, " + srcReg);
  }
  if (shift > 0) {
    emit(out, "srai", "t2, t2, " + std::to_string(shift));
  }
  if (!sourceNonnegative) {
    emit(out, "srai", "t0, " + srcReg + ", 31");
    emit(out, "sub", "t2, t2, t0");
  }
  if (imm < 0) {
    emit(out, "sub", "t2, x0, t2");
  }
  if (destReg != "t2") {
    emit(out, "mv", destReg + ", t2");
  }
  return true;
}

bool CodeGenerator::emitMagicMod(int imm, const std::string& srcReg, const std::string& destReg,
                                 std::ostream& out, bool sourceNonnegative) const {
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
  std::string magicReg = regForConstant(magicSigned);
  if (magicReg.empty()) {
    emit(out, "li", "t1, " + std::to_string(magicSigned));
    magicReg = "t1";
  }
  emit(out, "mulh", "t2, " + srcReg + ", " + magicReg);
  if (magicSigned < 0) {
    emit(out, "add", "t2, t2, " + srcReg);
  }
  if (shift > 0) {
    emit(out, "srai", "t2, t2, " + std::to_string(shift));
  }
  if (!sourceNonnegative) {
    emit(out, "srai", "t0, " + srcReg + ", 31");
    emit(out, "sub", "t2, t2, t0");
  }
  if (imm < 0) {
    emit(out, "sub", "t2, x0, t2");
  }
  std::string divisorReg = regForConstant(imm);
  if (divisorReg.empty()) {
    emit(out, "li", "t1, " + std::to_string(imm));
    divisorReg = "t1";
  }
  emit(out, "mul", "t2, t2, " + divisorReg);
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
      destReg = it->second;
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
  if (inst.op == IROp::SUB && inst.src1.isImm() &&
      (inst.src2.isLocalVar() || inst.src2.isGlobalVar())) {
    const std::string rhsReg = regForOperand(inst.src2);
    if (!rhsReg.empty() && rhsReg == destReg) {
      if (inst.src1.immVal == 0) {
        emit(out, "sub", destReg + ", x0, " + rhsReg);
      } else {
        std::string lhsReg = regForConstant(inst.src1.immVal);
        if (lhsReg.empty()) {
          lhsReg = (destReg == "t0") ? "t2" : "t0";
          emit(out, "li", lhsReg + ", " + std::to_string(inst.src1.immVal));
        }
        emit(out, "sub", destReg + ", " + lhsReg + ", " + rhsReg);
      }
      if (!destInReg) {
        storeOperand(destReg, inst.dest, out);
      }
      return;
    }
  }

  // 两个源都已在寄存器时可直接使用三地址指令。RISC-V 在写 rd 前读取
  // rs1/rs2，因此 rd 与任一源相同都安全；通用路径把 `d = a op d` 的 d
  // 先搬到 t1，会在宽表达式图和矩阵内层产生大量无意义 mv。
  if ((inst.src1.isLocalVar() || inst.src1.isGlobalVar()) &&
      (inst.src2.isLocalVar() || inst.src2.isGlobalVar())) {
    const std::string lhsReg = regForOperand(inst.src1);
    const std::string rhsReg = regForOperand(inst.src2);
    if (!lhsReg.empty() && !rhsReg.empty()) {
      switch (inst.op) {
      case IROp::ADD:
        emit(out, "add", destReg + ", " + lhsReg + ", " + rhsReg);
        break;
      case IROp::SUB:
        emit(out, "sub", destReg + ", " + lhsReg + ", " + rhsReg);
        break;
      case IROp::MUL:
        emit(out, "mul", destReg + ", " + lhsReg + ", " + rhsReg);
        break;
      case IROp::DIV:
        emit(out, "div", destReg + ", " + lhsReg + ", " + rhsReg);
        break;
      case IROp::MOD:
        emit(out, "rem", destReg + ", " + lhsReg + ", " + rhsReg);
        break;
      default:
        break;
      }
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
  } else if (inst.src2.isImm()) {
    const std::string reg = regForConstant(inst.src2.immVal);
    if (!reg.empty() && reg != destReg) {
      src2Reg = reg;
      src2InReg = true;
    }
  } else if (inst.src2.isLocalVar() || inst.src2.isGlobalVar()) {
    const std::string reg = regForOperand(inst.src2);
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
  if (inst.src1.isLocalVar() || inst.src1.isGlobalVar()) {
    const std::string reg = regForOperand(inst.src1);
    if (!reg.empty()) {
      src1Reg = reg;
    } else {
      loadOperand(inst.src1, destReg, out);
      src1Reg = destReg;
    }
  } else if (inst.src1.isImm()) {
    const std::string reg = regForConstant(inst.src1.immVal);
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
  const bool src1Nonnegative =
      (inst.src1.isImm() && inst.src1.immVal >= 0) ||
      (inst.src1.isLocalVar() && isNonNegative(inst.src1.name, currentInstIndex_));

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
        std::string multiplierReg = regForConstant(imm);
        if (multiplierReg.empty()) {
          emit(out, "li", "t1, " + std::to_string(imm));
          multiplierReg = "t1";
        }
        emit(out, "mul", destReg + ", " + src1Reg + ", " + multiplierReg);
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
      } else if (imm != 0 && imm != 1 &&
                 emitMagicDiv(imm, src1Reg, destReg, out, src1Nonnegative)) {
        // 已生成 magic 序列
      } else {
        std::string divisorReg = regForConstant(imm);
        if (divisorReg.empty()) {
          emit(out, "li", "t1, " + std::to_string(imm));
          divisorReg = "t1";
        }
        emit(out, "div", destReg + ", " + src1Reg + ", " + divisorReg);
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
      } else if (imm != 0 && emitMagicMod(imm, src1Reg, destReg, out, src1Nonnegative)) {
        // 已生成 magic 序列
      } else {
        std::string divisorReg = regForConstant(imm);
        if (divisorReg.empty()) {
          emit(out, "li", "t1, " + std::to_string(imm));
          divisorReg = "t1";
        }
        emit(out, "rem", destReg + ", " + src1Reg + ", " + divisorReg);
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
      if (inst.src2.isImm() &&
          emitMagicDiv(inst.src2.immVal, src1Reg, destReg, out, src1Nonnegative)) {
        break;
      }
      emit(out, "div", destReg + ", " + src1Reg + ", " + src2Reg);
      break;
    case IROp::MOD:
      if (inst.src2.isImm() &&
          emitMagicMod(inst.src2.immVal, src1Reg, destReg, out, src1Nonnegative)) {
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
      destReg = it->second;
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
  } else if (inst.src2.isImm()) {
    const std::string reg = regForConstant(inst.src2.immVal);
    if (!reg.empty() && reg != dest) {
      src2Reg = reg;
      src2InReg = true;
    }
  } else if (inst.src2.isLocalVar() || inst.src2.isGlobalVar()) {
    const std::string reg = regForOperand(inst.src2);
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
  if (inst.src1.isLocalVar() || inst.src1.isGlobalVar()) {
    const std::string reg = regForOperand(inst.src1);
    if (!reg.empty()) {
      src1Reg = reg;
    } else {
      loadOperand(inst.src1, dest, out);
      src1Reg = dest;
    }
  } else if (inst.src1.isImm()) {
    const std::string reg = regForConstant(inst.src1.immVal);
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
      if (const std::string reg = regForConstant(imm); !reg.empty()) {
        emit(out, "slt", dest + ", " + reg + ", " + src1Reg);
      } else {
        emit(out, "li", "t1, " + std::to_string(imm));
        emit(out, "slt", dest + ", t1, " + src1Reg);
      }
      break;
    case IROp::LE:
      emit(out, "slti", dest + ", " + src1Reg + ", " + std::to_string(imm + 1));
      break;
    case IROp::GE:
      emit(out, "slti", dest + ", " + src1Reg + ", " + std::to_string(imm));
      emit(out, "xori", dest + ", " + dest + ", 1");
      break;
    case IROp::EQ:
      if (const std::string reg = regForConstant(imm); !reg.empty()) {
        emit(out, "sub", dest + ", " + src1Reg + ", " + reg);
      } else {
        emit(out, "li", "t1, " + std::to_string(imm));
        emit(out, "sub", dest + ", " + src1Reg + ", t1");
      }
      emit(out, "seqz", dest + ", " + dest);
      break;
    case IROp::NE:
      if (const std::string reg = regForConstant(imm); !reg.empty()) {
        emit(out, "sub", dest + ", " + src1Reg + ", " + reg);
      } else {
        emit(out, "li", "t1, " + std::to_string(imm));
        emit(out, "sub", dest + ", " + src1Reg + ", t1");
      }
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

  std::unordered_set<std::size_t> normalizedRemainderFinals;
  for (std::size_t index = function.begin; index < function.end; ++index) {
    const auto normalized = matchNormalizedRemainder(ir_, index, function.end);
    if (normalized) {
      normalizedRemainderFinals.insert(index + normalized->length - 1);
    }
  }

  // 循环携带的随机数/模状态会在基本块入口丢失普通直线传播信息。若一个变量
  // 的每个真实定义都是非负常量或正模规范链的最终结果，则它在整个函数内恒
  // 非负；这使下一轮入口处的 `% small_constant` 也能安全删除第二次规范化。
  std::unordered_map<std::string, bool> alwaysNonnegative;
  std::unordered_set<std::string> hasDefinition;
  for (std::size_t index = function.begin; index < function.end; ++index) {
    const IRInst& inst = ir_[index];
    if (!inst.dest.isLocalVar() || (inst.op == IROp::LOCAL_VAR_DECL && inst.src1.isNone()) ||
        inst.op == IROp::RETURN || inst.op == IROp::PARAM) {
      continue;
    }
    const std::string& name = inst.dest.name;
    hasDefinition.insert(name);
    alwaysNonnegative.try_emplace(name, true);
    const bool nonnegativeConstant = (inst.op == IROp::ASSIGN || inst.op == IROp::LOCAL_VAR_DECL) &&
                                     inst.src1.isImm() && inst.src1.immVal >= 0;
    if (!nonnegativeConstant && normalizedRemainderFinals.count(index) == 0) {
      alwaysNonnegative[name] = false;
    }
  }
  for (const auto& [name, proven] : alwaysNonnegative) {
    if (proven && hasDefinition.count(name) != 0) {
      addRange(name, function.begin, function.end);
    }
  }

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
    if (normalizedRemainderFinals.count(idx) != 0 && inst.dest.isLocalVar()) {
      isNonNegDef = true;
    } else if ((inst.op == IROp::ASSIGN || inst.op == IROp::LOCAL_VAR_DECL) && inst.src1.isImm() &&
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
  adjustStackPointer(frame_.frameSizeBytes(), out);
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
      return it->second;
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

std::string CodeGenerator::regForOperand(const Operand& operand) const {
  if (operand.isLocalVar()) {
    return regForVar(operand.name);
  }
  if (operand.isGlobalVar()) {
    const auto found = frame_.globalRegAlloc.find(operand.name);
    if (found != frame_.globalRegAlloc.end()) {
      return found->second;
    }
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
