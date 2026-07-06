#ifndef TOYCC_AST_NODES_H
#define TOYCC_AST_NODES_H

#include <memory>
#include <string>
#include <vector>

namespace toycc {

// ============================================================
// 基础类型
// ============================================================

enum class Type { INT, VOID };

// ============================================================
// 运算符枚举
// ============================================================

enum class BinOp { ADD, SUB, MUL, DIV, MOD, LT, GT, LE, GE, EQ, NE, AND, OR, ASSIGN };

enum class UnaryOp { POS, NEG, NOT };

// ============================================================
// AST 节点基类
// ============================================================

class ASTNode {
public:
  int line = 0;
  int col = 0;
  virtual ~ASTNode() = default;
};

class Expr : public ASTNode {
public:
  Type type = Type::INT; // 语义分析阶段填充
};

class Stmt : public ASTNode {};

// ============================================================
// 表达式节点
// ============================================================

class IntLiteral : public Expr {
public:
  int value = 0;
  IntLiteral(int val)
      : value(val) {}
};

class VarExpr : public Expr {
public:
  std::string name;
  VarExpr(const std::string& n)
      : name(n) {}
};

class BinaryExpr : public Expr {
public:
  BinOp op;
  std::shared_ptr<Expr> lhs;
  std::shared_ptr<Expr> rhs;
  BinaryExpr(BinOp o, std::shared_ptr<Expr> l, std::shared_ptr<Expr> r)
      : op(o)
      , lhs(std::move(l))
      , rhs(std::move(r)) {}
};

class UnaryExpr : public Expr {
public:
  UnaryOp op;
  std::shared_ptr<Expr> operand;
  UnaryExpr(UnaryOp o, std::shared_ptr<Expr> e)
      : op(o)
      , operand(std::move(e)) {}
};

class CallExpr : public Expr {
public:
  std::string funcName;
  std::vector<std::shared_ptr<Expr>> args;
  CallExpr(const std::string& name, std::vector<std::shared_ptr<Expr>> a)
      : funcName(name)
      , args(std::move(a)) {}
};

// ============================================================
// 语句节点
// ============================================================

class Block : public Stmt {
public:
  std::vector<std::shared_ptr<Stmt>> stmts;
};

class ExprStmt : public Stmt {
public:
  std::shared_ptr<Expr> expr; // 可为空（空语句）
};

// 前向声明
class Decl;

class DeclStmt : public Stmt {
public:
  std::shared_ptr<Decl> decl;
  DeclStmt() = default;
  DeclStmt(std::shared_ptr<Decl> d)
      : decl(std::move(d)) {}
};

class IfStmt : public Stmt {
public:
  std::shared_ptr<Expr> condition;
  std::shared_ptr<Stmt> thenBranch;
  std::shared_ptr<Stmt> elseBranch; // 可为空
};

class WhileStmt : public Stmt {
public:
  std::shared_ptr<Expr> condition;
  std::shared_ptr<Stmt> body;
};

class BreakStmt : public Stmt {};

class ContinueStmt : public Stmt {};

class ReturnStmt : public Stmt {
public:
  std::shared_ptr<Expr> value; // 可为空（void 函数）
};

// ============================================================
// 声明节点
// ============================================================

class Decl : public ASTNode {
public:
  bool isConst = false;
  Type varType = Type::INT;
  std::string name;
  std::shared_ptr<Expr> initExpr; // 可为空
};

class FuncParam : public ASTNode {
public:
  Type type = Type::INT;
  std::string name;
};

class FuncDef : public ASTNode {
public:
  Type retType = Type::INT;
  std::string name;
  std::vector<std::shared_ptr<FuncParam>> params;
  std::shared_ptr<Block> body;
};

// ============================================================
// 编译单元
// ============================================================

class CompUnit : public ASTNode {
public:
  // globalDecls 包含 FuncDef 和 Decl（全局变量）
  std::vector<std::shared_ptr<ASTNode>> globalDecls;
};

} // namespace toycc

#endif // TOYCC_AST_NODES_H