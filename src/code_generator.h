#pragma once

#include "ir.h"

#include <iosfwd>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace toycc {

class CodeGenerator {
public:
  CodeGenerator() = default;

  void generate(const std::vector<IRInst>& ir, std::ostream& out);
  void generateDefaultMain(std::ostream& out);

private:
  struct FunctionRange {
    std::string name;
    std::size_t begin = 0;
    std::size_t end = 0;
  };

  struct StackFrame {
    int frameSize = 16;
    int localBytes = 0;
    int outgoingArgumentBytes = 0;
    std::unordered_map<std::string, int> localOffsets;
    std::vector<std::string> usedCalleeSavedRegisters;
    std::string functionName;

    int frameSizeBytes() const;
  };

  std::vector<IRInst> ir_;
  std::ostream* out_ = nullptr;
  StackFrame frame_;
  std::string currentFunction_;
  int currentParamIndex_ = 0;

  void emitGlobalData(std::ostream& out);
  std::vector<FunctionRange> collectFunctions() const;
  StackFrame analyzeStackFrame(const FunctionRange& function) const;

  void generateFunction(const FunctionRange& function, std::ostream& out);
  void generateInstruction(const IRInst& inst, std::ostream& out);
  void emitPrologue(const StackFrame& frame, std::ostream& out) const;
  void emitEpilogue(const StackFrame& frame, std::ostream& out) const;

  void loadOperand(const Operand& operand, std::string_view reg, std::ostream& out);
  void storeOperand(std::string_view reg, const Operand& operand, std::ostream& out);
  void emitBinaryOp(const IRInst& inst, std::ostream& out);
  void emitCompareOp(const IRInst& inst, std::ostream& out);
  void emitCall(const IRInst& inst, std::ostream& out);

  std::string asmLabel(const std::string& label) const;
  std::string globalSymbol(const std::string& name) const;
  int localOffset(const Operand& operand) const;
  int ensureLocalOffset(const Operand& operand);

  void emit(std::ostream& out, std::string_view opcode, std::string_view operands = {}) const;
  void emitRaw(std::ostream& out, std::string_view text) const;
};

} // namespace toycc
