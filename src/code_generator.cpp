#include "code_generator.h"

#include <algorithm>
#include <ostream>
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

} // namespace

int CodeGenerator::StackFrame::frameSizeBytes() const {
  const int reservedWords = 2 + static_cast<int>(usedCalleeSavedRegisters.size());
  const int computedLocalBytes =
      std::max(this->localBytes, static_cast<int>(localOffsets.size()) * kWordBytes);
  return alignTo((reservedWords * kWordBytes) + computedLocalBytes + outgoingArgumentBytes,
                 kStackAlignmentBytes);
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
  frame.localBytes = 0;
  frame.outgoingArgumentBytes = 0;

  int localIndex = 0;
  int maxOverflowArgs = 0;
  int nextReg = 2; // s2-s11 可用于局部变量分配

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

  // 第二遍：为前 10 个局部变量分配 s2-s11 寄存器
  for (const auto& varName : varOrder) {
    if (nextReg <= 11) {
      frame.regAlloc[varName] = nextReg++;
    }
  }

  // 更新 usedCalleeSavedRegisters：包含所有分配的 s 寄存器
  frame.usedCalleeSavedRegisters.clear();
  for (int r = 2; r <= 11; ++r) {
    if (r < nextReg) {
      frame.usedCalleeSavedRegisters.push_back("s" + std::to_string(r));
    }
  }

  frame.localBytes = static_cast<int>(frame.localOffsets.size()) * kWordBytes;
  frame.outgoingArgumentBytes = maxOverflowArgs * kWordBytes;
  return frame;
}

void CodeGenerator::generateFunction(const FunctionRange& function, std::ostream& out) {
  frame_ = analyzeStackFrame(function);
  currentFunction_ = function.name;
  currentParamIndex_ = 0;

  emitRaw(out, "    .align 2\n");
  emitRaw(out, "    .globl " + function.name + "\n");
  emitRaw(out, "    .type " + function.name + ", @function\n");
  out << function.name << ":\n";

  emitPrologue(frame_, out);

  for (std::size_t i = function.begin; i < function.end; ++i) {
    const auto& inst = ir_[i];
    if (inst.op == IROp::FUNC_BEGIN || inst.op == IROp::FUNC_END) {
      continue;
    }
    generateInstruction(inst, out);
  }

  out << ".L_" << function.name << "_exit:\n";
  emitEpilogue(frame_, out);
  out << "    .size " << function.name << ", .-" << function.name << "\n";
}

void CodeGenerator::generateInstruction(const IRInst& inst, std::ostream& out) {
  switch (inst.op) {
  case IROp::LOCAL_VAR_DECL: {
    if (inst.dest.type == OperandType::LOCAL_VAR) {
      ensureLocalOffset(inst.dest);
      if (inst.src1.type == OperandType::PARAM) {
        if (currentParamIndex_ < 8) {
          const std::string reg = "a" + std::to_string(currentParamIndex_);
          emit(out, "mv", "t0, " + reg);
          storeOperand("t0", inst.dest, out);
        } else {
          // 溢出参数从调用者栈帧读取（位于 s0 正偏移处）
          const int stackOffset = (currentParamIndex_ - 8) * kWordBytes;
          emit(out, "lw", "t0, " + std::to_string(stackOffset) + "(s0)");
          storeOperand("t0", inst.dest, out);
        }
        currentParamIndex_ += 1;
      } else if (inst.src1.type == OperandType::IMM) {
        loadOperand(inst.src1, "t0", out);
        storeOperand("t0", inst.dest, out);
      } else if (!inst.src1.isNone()) {
        loadOperand(inst.src1, "t0", out);
        storeOperand("t0", inst.dest, out);
      }
    }
    break;
  }
  case IROp::ASSIGN: {
    if (inst.dest.type == OperandType::LOCAL_VAR || inst.dest.type == OperandType::GLOBAL_VAR) {
      loadOperand(inst.src1, "t0", out);
      storeOperand("t0", inst.dest, out);
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
    emitCompareOp(inst, out);
    break;
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
  case IROp::BEQZ:
    loadOperand(inst.src1, "t0", out);
    out << "    beqz t0, " << asmLabel(inst.dest.name) << "\n";
    break;
  case IROp::BNEZ:
    loadOperand(inst.src1, "t0", out);
    out << "    bnez t0, " << asmLabel(inst.dest.name) << "\n";
    break;
  case IROp::LOAD: {
    if (inst.dest.type == OperandType::LOCAL_VAR || inst.dest.type == OperandType::GLOBAL_VAR) {
      loadOperand(inst.src1, "t0", out);
      storeOperand("t0", inst.dest, out);
    }
    break;
  }
  case IROp::STORE: {
    if (inst.dest.type == OperandType::LOCAL_VAR || inst.dest.type == OperandType::GLOBAL_VAR) {
      loadOperand(inst.src1, "t0", out);
      storeOperand("t0", inst.dest, out);
    }
    break;
  }
  case IROp::GLOBAL_VAR_DECL:
    break;
  case IROp::FUNC_BEGIN:
  case IROp::FUNC_END:
    break;
  }
}

void CodeGenerator::emitPrologue(const StackFrame& frame, std::ostream& out) const {
  const int frameSize = frame.frameSizeBytes();
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

void CodeGenerator::loadOperand(const Operand& operand, std::string_view reg, std::ostream& out) {
  if (operand.type == OperandType::IMM) {
    emit(out, "li", std::string(reg) + ", " + std::to_string(operand.immVal));
    return;
  }

  if (operand.type == OperandType::LOCAL_VAR) {
    // 如果变量分配了寄存器，直接从寄存器复制（mv 比 lw 快）
    auto it = frame_.regAlloc.find(operand.name);
    if (it != frame_.regAlloc.end()) {
      std::string sreg = "s" + std::to_string(it->second);
      if (std::string(reg) != sreg) {
        emit(out, "mv", std::string(reg) + ", " + sreg);
      }
      return;
    }
    // 否则从栈上加载
    const int offset = ensureLocalOffset(operand);
    emit(out, "lw", std::string(reg) + ", " + std::to_string(offset) + "(s0)");
    return;
  }

  if (operand.type == OperandType::GLOBAL_VAR) {
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
    // 如果变量分配了寄存器，直接存到寄存器（mv 比 sw 快）
    auto it = frame_.regAlloc.find(operand.name);
    if (it != frame_.regAlloc.end()) {
      std::string sreg = "s" + std::to_string(it->second);
      if (std::string(reg) != sreg) {
        emit(out, "mv", sreg + ", " + std::string(reg));
      }
      return;
    }
    // 否则存到栈上
    const int offset = ensureLocalOffset(operand);
    emit(out, "sw", std::string(reg) + ", " + std::to_string(offset) + "(s0)");
    return;
  }

  if (operand.type == OperandType::GLOBAL_VAR) {
    emit(out, "la", "t2, " + globalSymbol(operand.name));
    emit(out, "sw", std::string(reg) + ", 0(t2)");
    return;
  }
}

void CodeGenerator::emitBinaryOp(const IRInst& inst, std::ostream& out) {
  // 确定目标寄存器：若 dest 分配了寄存器则直接用该寄存器运算，避免 mv 往返
  std::string destReg = "t0";
  bool destInReg = false;
  if (inst.dest.isLocalVar()) {
    auto it = frame_.regAlloc.find(inst.dest.name);
    if (it != frame_.regAlloc.end()) {
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

  // 先处理 src2（加载到 t1 或使用立即数），避免 destReg 覆盖 src2 寄存器
  const bool src2IsSmallImm =
      inst.src2.isImm() && inst.src2.immVal >= -2048 && inst.src2.immVal <= 2047;
  int imm = 0;
  if (src2IsSmallImm) {
    imm = inst.src2.immVal;
  } else {
    loadOperand(inst.src2, "t1", out);
  }

  // 再加载 src1 到 destReg（若 src1==dest 且都在寄存器，loadOperand 自动跳过 mv）
  loadOperand(inst.src1, destReg, out);

  // 执行运算，结果直接落在 destReg
  if (src2IsSmallImm) {
    switch (inst.op) {
    case IROp::ADD:
      emit(out, "addi", destReg + ", " + destReg + ", " + std::to_string(imm));
      break;
    case IROp::SUB:
      emit(out, "addi", destReg + ", " + destReg + ", " + std::to_string(-imm));
      break;
    case IROp::MUL:
      if (imm > 0 && (imm & (imm - 1)) == 0) {
        emit(out, "slli",
             destReg + ", " + destReg + ", " +
                 std::to_string(__builtin_ctz(static_cast<unsigned>(imm))));
      } else {
        emit(out, "li", "t1, " + std::to_string(imm));
        emit(out, "mul", destReg + ", " + destReg + ", t1");
      }
      break;
    case IROp::DIV:
      emit(out, "li", "t1, " + std::to_string(imm));
      emit(out, "div", destReg + ", " + destReg + ", t1");
      break;
    case IROp::MOD:
      emit(out, "li", "t1, " + std::to_string(imm));
      emit(out, "rem", destReg + ", " + destReg + ", t1");
      break;
    default:
      break;
    }
  } else {
    switch (inst.op) {
    case IROp::ADD:
      emit(out, "add", destReg + ", " + destReg + ", t1");
      break;
    case IROp::SUB:
      emit(out, "sub", destReg + ", " + destReg + ", t1");
      break;
    case IROp::MUL:
      emit(out, "mul", destReg + ", " + destReg + ", t1");
      break;
    case IROp::DIV:
      emit(out, "div", destReg + ", " + destReg + ", t1");
      break;
    case IROp::MOD:
      emit(out, "rem", destReg + ", " + destReg + ", t1");
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
  // 确定目标寄存器：若 dest 分配了寄存器则直接用该寄存器
  std::string destReg = "t0";
  bool destInReg = false;
  if (inst.dest.isLocalVar()) {
    auto it = frame_.regAlloc.find(inst.dest.name);
    if (it != frame_.regAlloc.end()) {
      destReg = "s" + std::to_string(it->second);
      destInReg = true;
    }
  }

  // 先处理 src2
  const bool src2IsSmallImm =
      inst.src2.isImm() && inst.src2.immVal >= -2048 && inst.src2.immVal <= 2047;
  int imm = 0;
  if (src2IsSmallImm) {
    imm = inst.src2.immVal;
  } else {
    loadOperand(inst.src2, "t1", out);
  }

  loadOperand(inst.src1, destReg, out);

  if (src2IsSmallImm) {
    switch (inst.op) {
    case IROp::LT:
      emit(out, "slti", destReg + ", " + destReg + ", " + std::to_string(imm));
      break;
    case IROp::GT:
      // x > imm  <=>  imm < x，需用 t1 加载 imm
      emit(out, "li", "t1, " + std::to_string(imm));
      emit(out, "slt", destReg + ", t1, " + destReg);
      break;
    case IROp::LE:
      // x <= imm  <=>  x < imm+1
      emit(out, "slti", destReg + ", " + destReg + ", " + std::to_string(imm + 1));
      break;
    case IROp::GE:
      emit(out, "slti", destReg + ", " + destReg + ", " + std::to_string(imm));
      emit(out, "xori", destReg + ", " + destReg + ", 1");
      break;
    case IROp::EQ:
      emit(out, "li", "t1, " + std::to_string(imm));
      emit(out, "sub", destReg + ", " + destReg + ", t1");
      emit(out, "seqz", destReg + ", " + destReg);
      break;
    case IROp::NE:
      emit(out, "li", "t1, " + std::to_string(imm));
      emit(out, "sub", destReg + ", " + destReg + ", t1");
      emit(out, "snez", destReg + ", " + destReg);
      break;
    default:
      break;
    }
  } else {
    switch (inst.op) {
    case IROp::LT:
      emit(out, "slt", destReg + ", " + destReg + ", t1");
      break;
    case IROp::GT:
      emit(out, "slt", destReg + ", t1, " + destReg);
      break;
    case IROp::LE:
      emit(out, "slt", destReg + ", t1, " + destReg);
      emit(out, "xori", destReg + ", " + destReg + ", 1");
      break;
    case IROp::GE:
      emit(out, "slt", destReg + ", " + destReg + ", t1");
      emit(out, "xori", destReg + ", " + destReg + ", 1");
      break;
    case IROp::EQ:
      emit(out, "sub", destReg + ", " + destReg + ", t1");
      emit(out, "seqz", destReg + ", " + destReg);
      break;
    case IROp::NE:
      emit(out, "sub", destReg + ", " + destReg + ", t1");
      emit(out, "snez", destReg + ", " + destReg);
      break;
    default:
      break;
    }
  }

  if (!destInReg) {
    storeOperand(destReg, inst.dest, out);
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
  if (it == frame_.localOffsets.end()) {
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
