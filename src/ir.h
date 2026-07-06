#ifndef TOYCC_IR_H
#define TOYCC_IR_H

#include <ostream>
#include <string>
#include <vector>

namespace toycc {

// ============================================================
// IR 操作数
// ============================================================

enum class OperandType {
  IMM,        // 立即数
  LOCAL_VAR,  // 局部变量
  GLOBAL_VAR, // 全局变量
  LABEL,      // 跳转标签
  FUNC,       // 函数名
  PARAM,      // 函数参数引用
  NONE        // 无操作数
};

struct Operand {
  OperandType type = OperandType::NONE;
  std::string name;
  int immVal = 0;

  // 工厂方法
  static Operand imm(int val) {
    Operand o;
    o.type = OperandType::IMM;
    o.immVal = val;
    o.name = std::to_string(val);
    return o;
  }

  static Operand localVar(const std::string& name) {
    Operand o;
    o.type = OperandType::LOCAL_VAR;
    o.name = name;
    return o;
  }

  static Operand globalVar(const std::string& name) {
    Operand o;
    o.type = OperandType::GLOBAL_VAR;
    o.name = name;
    return o;
  }

  static Operand label(const std::string& name) {
    Operand o;
    o.type = OperandType::LABEL;
    o.name = name;
    return o;
  }

  static Operand func(const std::string& name) {
    Operand o;
    o.type = OperandType::FUNC;
    o.name = name;
    return o;
  }

  static Operand param(const std::string& name) {
    Operand o;
    o.type = OperandType::PARAM;
    o.name = name;
    return o;
  }

  static Operand none() {
    return Operand{};
  }

  bool isImm() const {
    return type == OperandType::IMM;
  }
  bool isLocalVar() const {
    return type == OperandType::LOCAL_VAR;
  }
  bool isGlobalVar() const {
    return type == OperandType::GLOBAL_VAR;
  }
  bool isLabel() const {
    return type == OperandType::LABEL;
  }
  bool isFunc() const {
    return type == OperandType::FUNC;
  }
  bool isNone() const {
    return type == OperandType::NONE;
  }
};

// ============================================================
// IR 操作码
// ============================================================

enum class IROp {
  // 算术
  ADD,
  SUB,
  MUL,
  DIV,
  MOD,
  // 逻辑
  NOT,
  // 关系运算（结果为 0 或 1）
  LT,
  GT,
  LE,
  GE,
  EQ,
  NE,
  // 数据移动
  ASSIGN,
  LOAD,
  STORE,
  // 函数调用与返回
  CALL,
  RETURN,
  PARAM,
  // 控制流
  LABEL,
  BRANCH,
  BEQZ,
  BNEZ,
  // 声明
  FUNC_BEGIN,
  FUNC_END,
  GLOBAL_VAR_DECL,
  LOCAL_VAR_DECL
};

// ============================================================
// IR 指令
// ============================================================

struct IRInst {
  IROp op;
  Operand dest;
  Operand src1;
  Operand src2;

  IRInst()
      : op(IROp::ADD)
      , dest(Operand::none())
      , src1(Operand::none())
      , src2(Operand::none()) {}

  IRInst(IROp o, Operand d, Operand s1, Operand s2)
      : op(o)
      , dest(std::move(d))
      , src1(std::move(s1))
      , src2(std::move(s2)) {}
};

// ============================================================
// IR 打印工具
// ============================================================

inline const char* irOpToString(IROp op) {
  switch (op) {
  case IROp::ADD:
    return "ADD";
  case IROp::SUB:
    return "SUB";
  case IROp::MUL:
    return "MUL";
  case IROp::DIV:
    return "DIV";
  case IROp::MOD:
    return "MOD";
  case IROp::NOT:
    return "NOT";
  case IROp::LT:
    return "LT";
  case IROp::GT:
    return "GT";
  case IROp::LE:
    return "LE";
  case IROp::GE:
    return "GE";
  case IROp::EQ:
    return "EQ";
  case IROp::NE:
    return "NE";
  case IROp::ASSIGN:
    return "ASSIGN";
  case IROp::LOAD:
    return "LOAD";
  case IROp::STORE:
    return "STORE";
  case IROp::CALL:
    return "CALL";
  case IROp::RETURN:
    return "RETURN";
  case IROp::PARAM:
    return "PARAM";
  case IROp::LABEL:
    return "LABEL";
  case IROp::BRANCH:
    return "BRANCH";
  case IROp::BEQZ:
    return "BEQZ";
  case IROp::BNEZ:
    return "BNEZ";
  case IROp::FUNC_BEGIN:
    return "FUNC_BEGIN";
  case IROp::FUNC_END:
    return "FUNC_END";
  case IROp::GLOBAL_VAR_DECL:
    return "GLOBAL_VAR_DECL";
  case IROp::LOCAL_VAR_DECL:
    return "LOCAL_VAR_DECL";
  }
  return "UNKNOWN";
}

inline std::string operandToString(const Operand& op) {
  switch (op.type) {
  case OperandType::IMM:
    return "#" + std::to_string(op.immVal);
  case OperandType::LOCAL_VAR:
    return "%" + op.name;
  case OperandType::GLOBAL_VAR:
    return "@" + op.name;
  case OperandType::LABEL:
    return op.name + ":";
  case OperandType::FUNC:
    return op.name;
  case OperandType::PARAM:
    return "$" + op.name;
  case OperandType::NONE:
    return "_";
  }
  return "?";
}

inline std::ostream& operator<<(std::ostream& os, const IRInst& inst) {
  os << irOpToString(inst.op);

  if (!inst.dest.isNone()) {
    os << " " << operandToString(inst.dest);
  }
  if (!inst.src1.isNone()) {
    os << ", " << operandToString(inst.src1);
  }
  if (!inst.src2.isNone()) {
    os << ", " << operandToString(inst.src2);
  }
  return os;
}

} // namespace toycc

#endif // TOYCC_IR_H