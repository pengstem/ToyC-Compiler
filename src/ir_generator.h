#ifndef TOYCC_IR_GENERATOR_H
#define TOYCC_IR_GENERATOR_H

#include "ast_nodes.h"
#include "ir.h"
#include "symbol_table.h"

#include <stack>
#include <string>
#include <vector>

namespace toycc {

// ============================================================
// IR 生成上下文
// ============================================================

struct LoopContext {
  std::string breakLabel;    // 跳出循环的目标标签
  std::string continueLabel; // 跳回条件判断的标签
};

// ============================================================
// IR 生成器
// ============================================================

class IRGenerator {
public:
  IRGenerator(SymbolTable& symTable);

  // 生成 IR，返回指令序列
  std::vector<IRInst> generate(CompUnit& compUnit);

  // 获取生成的 IR
  const std::vector<IRInst>& getIR() const {
    return ir;
  }

private:
  SymbolTable& symTable;
  std::vector<IRInst> ir;

  // 临时变量与标签计数器
  int tempCounter = 0;
  int labelCounter = 0;

  // 循环上下文栈
  std::stack<LoopContext> loopStack;

  // 当前上下文
  bool isGlobalContext = true;
  std::string currentFuncName;
  Type currentFuncRetType = Type::INT;

  // 工具方法
  std::string newTemp();
  std::string newLabel();

  // 生成 IR 指令
  void emit(IROp op, Operand dest = Operand::none(), Operand src1 = Operand::none(),
            Operand src2 = Operand::none());

  // 生成表达式 IR，返回存放结果的 Operand
  Operand genExpr(Expr* expr);

  // 生成短路求值表达式 IR（用于 && 和 ||）
  Operand genShortCircuit(BinaryExpr* expr);

  // 生成语句 IR
  void genStmt(Stmt* stmt);

  // 生成声明 IR
  void genDecl(Decl* decl);

  // 生成函数定义 IR
  void genFuncDef(FuncDef* funcDef);

  // 生成全局声明 IR
  void genGlobalDecl(ASTNode* node);
};

} // namespace toycc

#endif // TOYCC_IR_GENERATOR_H