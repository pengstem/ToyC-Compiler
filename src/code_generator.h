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
    bool hasCall = false;
    bool hasStackParameters = false;
    std::unordered_map<std::string, int> localOffsets;
    std::unordered_map<std::string, int> regAlloc; // 局部变量 → s寄存器编号 (2-11)
    // 无调用叶函数可安全使用 caller-saved 参数寄存器，无需在序言/尾声保存。
    std::unordered_map<std::string, std::string> leafRegAlloc;
    std::unordered_map<std::string, std::string> globalRegAlloc; // 全局变量 → a1-a7/s2-s11
    std::unordered_map<std::string, std::string> tempRegs;       // 临时变量 → t寄存器 (t4-t6)
    // 热循环常量 → 空闲寄存器。函数入口只物化一次，循环体直接复用。
    std::unordered_map<int, std::string> constantRegAlloc;
    std::vector<std::string> usedCalleeSavedRegisters;
    std::string functionName;

    int frameSizeBytes() const;
    bool needsFrame() const;
  };

  // 尾调用信息：CALL 的结果仅被紧随其后的 RETURN 使用（尾位置）
  struct TailCallInfo {
    std::size_t callIndex = 0;
    std::string target;
    bool isSelf = false; // 自递归：直接跳回函数体，跳过帧设置/恢复
  };

  std::vector<IRInst> ir_;
  std::ostream* out_ = nullptr;
  StackFrame frame_;
  std::string currentFunction_;
  int currentParamIndex_ = 0;
  std::vector<TailCallInfo> tailCalls_;
  // 每个局部变量在函数内的使用次数（用于判断比较结果是否仅被紧随分支使用，从而融合为条件分支）
  std::unordered_map<std::string, int> irUseCount_;
  // 变量在 [begin, end) 的 IR 指令索引范围内恒非负（循环计数变量、由非负
  // 操作数组成的 ADD/ASSIGN 临时等）。2 的幂常量除法/取模在该范围内可替换
  // 为单条 srai/andi（负数的 C 除/模语义不同，须按位置精确限定）。
  struct NonNegRange {
    std::size_t begin = 0;
    std::size_t end = 0;
  };
  std::unordered_map<std::string, std::vector<NonNegRange>> nonNegativeRanges_;
  // 当前正在生成的 IR 指令索引（供按位置查询非负性）
  std::size_t currentInstIndex_ = 0;

  void emitGlobalData(std::ostream& out);
  std::vector<FunctionRange> collectFunctions() const;
  StackFrame analyzeStackFrame(const FunctionRange& function) const;

  void generateFunction(const FunctionRange& function, std::ostream& out);
  // 生成第 index 条 IR 指令；若与下一条比较+分支融合，返回消耗的指令数（1 或 2）
  std::size_t generateInstruction(std::size_t index, std::size_t end, std::ostream& out);
  void emitPrologue(const StackFrame& frame, std::ostream& out) const;
  void emitEpilogue(const StackFrame& frame, std::ostream& out) const;

  // 检测函数中的尾调用（CALL 结果仅被紧随的 RETURN 使用）
  std::vector<TailCallInfo> detectTailCalls(const FunctionRange& function) const;
  // 分析函数内反转循环的计数变量及非负表达式，记录变量恒非负的 IR 索引范围
  void analyzeNonNegativeVars(const FunctionRange& function);
  // 查询变量 name 在 IR 索引 index 处是否已知非负
  bool isNonNegative(const std::string& name, std::size_t index) const;
  // 发射尾调用序列：恢复被调用者保存寄存器后直接跳转
  void emitTailCall(const TailCallInfo& tailCall, std::ostream& out) const;
  // 扫描汇编文本中实际使用的 s2-s11 寄存器
  void scanUsedSRegisters(const std::string& asmText, std::vector<std::string>& used) const;

  // 汇编级窥孔优化：合并比较+分支序列（slt+beqz -> bge 等）
  void applyPeephole(const std::string& asmText, std::ostream& out);

  void loadOperand(const Operand& operand, std::string_view reg, std::ostream& out);
  void storeOperand(std::string_view reg, const Operand& operand, std::ostream& out);
  std::string regForConstant(int value) const;
  void emitLoadImmediate(int value, std::string_view reg, std::ostream& out) const;
  void emitLoadFromAddress(std::string_view reg, std::string_view base, int offset,
                           std::ostream& out) const;
  void emitStoreToAddress(std::string_view reg, std::string_view base, int offset,
                          std::ostream& out) const;
  void adjustStackPointer(int amount, std::ostream& out) const;
  void emitBinaryOp(const IRInst& inst, std::ostream& out);
  // 比较指令：若结果临时仅被紧随其后的 BEQZ/BNEZ 使用，融合为条件分支（不落栈）
  std::size_t emitCompareOrFuse(std::size_t index, std::size_t end, std::ostream& out);
  // 将比较结果计算到指定寄存器（不落栈），供融合分支复用
  void emitCompareInto(const IRInst& inst, std::string_view destReg, std::ostream& out);
  void emitCompareOp(const IRInst& inst, std::ostream& out);
  void emitCall(const IRInst& inst, std::ostream& out);

  // 常量除数 magic 除法/取模序列；成功生成指令返回 true
  bool emitMagicDiv(int imm, const std::string& srcReg, const std::string& destReg,
                    std::ostream& out) const;
  bool emitMagicMod(int imm, const std::string& srcReg, const std::string& destReg,
                    std::ostream& out) const;

  // 若 dest 局部变量分配了寄存器则返回该寄存器名，否则返回 "t0"
  std::string destRegOrT0(const Operand& dest) const;
  bool isDestInReg(const Operand& dest) const;
  // 统一查询变量绑定的寄存器（s2-s11 或 t4-t6），未绑定返回空串
  std::string regForVar(const std::string& name) const;
  // 查询局部/全局操作数的寄存器副本，立即数或未分配值返回空串。
  std::string regForOperand(const Operand& operand) const;
  bool varHasReg(const std::string& name) const;

  std::string asmLabel(const std::string& label) const;
  std::string globalSymbol(const std::string& name) const;
  int localOffset(const Operand& operand) const;
  int ensureLocalOffset(const Operand& operand);

  void emit(std::ostream& out, std::string_view opcode, std::string_view operands = {}) const;
  void emitRaw(std::ostream& out, std::string_view text) const;
};

} // namespace toycc
