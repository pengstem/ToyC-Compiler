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
  const int localBytes = std::max(this->localBytes, static_cast<int>(localOffsets.size()) * kWordBytes);
  return alignTo((reservedWords * kWordBytes) + localBytes + outgoingArgumentBytes,
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
    out << "    .word 0\n";
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
  frame.usedCalleeSavedRegisters = {"s1"};
  frame.localOffsets.clear();
  frame.localBytes = 0;

  int localIndex = 0;
  for (std::size_t i = function.begin; i < function.end; ++i) {
    const auto& inst = ir_[i];
    if (inst.op == IROp::LOCAL_VAR_DECL) {
      if (inst.dest.type == OperandType::LOCAL_VAR) {
        if (frame.localOffsets.find(inst.dest.name) == frame.localOffsets.end()) {
          frame.localOffsets[inst.dest.name] = localIndex++;
        }
      }
      continue;
    }

    if (inst.dest.type == OperandType::LOCAL_VAR) {
      if (frame.localOffsets.find(inst.dest.name) == frame.localOffsets.end()) {
        frame.localOffsets[inst.dest.name] = localIndex++;
      }
    }
    if (inst.src1.type == OperandType::LOCAL_VAR) {
      if (frame.localOffsets.find(inst.src1.name) == frame.localOffsets.end()) {
        frame.localOffsets[inst.src1.name] = localIndex++;
      }
    }
    if (inst.src2.type == OperandType::LOCAL_VAR) {
      if (frame.localOffsets.find(inst.src2.name) == frame.localOffsets.end()) {
        frame.localOffsets[inst.src2.name] = localIndex++;
      }
    }
  }
  frame.localBytes = static_cast<int>(frame.localOffsets.size()) * kWordBytes;
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
        const std::string reg = currentParamIndex_ < 8 ? ("a" + std::to_string(currentParamIndex_))
                                                     : "a7";
        emit(out, "mv", "t0, " + reg);
        storeOperand("t0", inst.dest, out);
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
    const int argIndex = inst.src2.immVal;
    const std::string reg = argIndex < 8 ? ("a" + std::to_string(argIndex)) : "a7";
    if (inst.dest.type == OperandType::IMM) {
      emit(out, "li", reg + ", " + std::to_string(inst.dest.immVal));
    } else {
      loadOperand(inst.dest, reg, out);
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
  emit(out, "addi", "s0, sp, " + std::to_string(frameSize));
  emit(out, "sw", "ra, -4(s0)");
  emit(out, "sw", "s0, -8(s0)");

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
    const int offset = ensureLocalOffset(operand);
    emit(out, "lw", std::string(reg) + ", " + std::to_string(offset) + "(s0)");
    return;
  }

  if (operand.type == OperandType::GLOBAL_VAR) {
    emit(out, "lw", std::string(reg) + ", " + globalSymbol(operand.name));
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
    const int offset = ensureLocalOffset(operand);
    emit(out, "sw", std::string(reg) + ", " + std::to_string(offset) + "(s0)");
    return;
  }

  if (operand.type == OperandType::GLOBAL_VAR) {
    emit(out, "sw", std::string(reg) + ", " + globalSymbol(operand.name));
    return;
  }
}

void CodeGenerator::emitBinaryOp(const IRInst& inst, std::ostream& out) {
  loadOperand(inst.src1, "t0", out);
  if (inst.op == IROp::NOT) {
    emit(out, "sltiu", "t0, t0, 1");
  } else {
    loadOperand(inst.src2, "t1", out);
    switch (inst.op) {
    case IROp::ADD:
      emit(out, "add", "t0, t0, t1");
      break;
    case IROp::SUB:
      emit(out, "sub", "t0, t0, t1");
      break;
    case IROp::MUL:
      emit(out, "mul", "t0, t0, t1");
      break;
    case IROp::DIV:
      emit(out, "div", "t0, t0, t1");
      break;
    case IROp::MOD:
      emit(out, "rem", "t0, t0, t1");
      break;
    default:
      break;
    }
  }
  storeOperand("t0", inst.dest, out);
}

void CodeGenerator::emitCompareOp(const IRInst& inst, std::ostream& out) {
  loadOperand(inst.src1, "t0", out);
  loadOperand(inst.src2, "t1", out);

  switch (inst.op) {
  case IROp::LT:
    emit(out, "slt", "t0, t0, t1");
    break;
  case IROp::GT:
    emit(out, "slt", "t0, t1, t0");
    break;
  case IROp::LE:
    emit(out, "slt", "t0, t1, t0");
    emit(out, "xori", "t0, t0, 1");
    break;
  case IROp::GE:
    emit(out, "slt", "t0, t0, t1");
    emit(out, "xori", "t0, t0, 1");
    break;
  case IROp::EQ:
    emit(out, "sub", "t0, t0, t1");
    emit(out, "seqz", "t0, t0");
    break;
  case IROp::NE:
    emit(out, "sub", "t0, t0, t1");
    emit(out, "snez", "t0, t0");
    break;
  default:
    break;
  }

  storeOperand("t0", inst.dest, out);
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

void CodeGenerator::emit(std::ostream& out, std::string_view opcode, std::string_view operands) const {
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
