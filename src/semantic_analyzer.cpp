#include "semantic_analyzer.h"

#include <algorithm>
#include <iostream>

namespace toycc {

SemanticAnalyzer::SemanticAnalyzer() = default;

// ============================================================
// 错误报告
// ============================================================

void SemanticAnalyzer::error(int line, int col, const std::string& msg) {
  errors.push_back({line, col, msg});
}

// ============================================================
// 主入口
// ============================================================

bool SemanticAnalyzer::analyze(CompUnit& compUnit) {
  errors.clear();
  symTable = SymbolTable(); // 重置符号表

  // 第一遍：收集全局符号（函数和全局变量）
  collectGlobals(compUnit);

  // 第二遍：分析每个全局声明体
  for (auto& decl : compUnit.globalDecls) {
    analyzeGlobalDecl(decl.get());
  }

  return errors.empty();
}

// ============================================================
// 收集全局声明
// ============================================================

void SemanticAnalyzer::collectGlobals(CompUnit& compUnit) {
  for (auto& node : compUnit.globalDecls) {
    if (auto* funcDef = dynamic_cast<FuncDef*>(node.get())) {
      // 检查函数重名
      if (symTable.lookupFunction(funcDef->name)) {
        error(funcDef->line, funcDef->col, "function '" + funcDef->name + "' already defined");
        continue;
      }

      Symbol sym;
      sym.name = funcDef->name;
      sym.type = funcDef->retType;
      sym.isFunction = true;
      sym.isGlobal = true;
      for (auto& p : funcDef->params) {
        sym.paramTypes.push_back(p->type);
      }
      symTable.addSymbol(sym);
    }
    // 全局变量声明由 analyzeDecl 在第二遍处理，此处不预注册
  }
}

// ============================================================
// 分析全局声明
// ============================================================

void SemanticAnalyzer::analyzeGlobalDecl(ASTNode* node) {
  if (auto* funcDef = dynamic_cast<FuncDef*>(node)) {
    analyzeFuncDef(funcDef);
  } else if (auto* decl = dynamic_cast<Decl*>(node)) {
    analyzeDecl(decl);
  }
}

// ============================================================
// 分析函数定义
// ============================================================

void SemanticAnalyzer::analyzeFuncDef(FuncDef* funcDef) {
  currentFuncRetType = funcDef->retType;
  currentFuncName = funcDef->name;
  whileDepth = 0;

  // 进入函数作用域
  symTable.enterScope();

  // 添加参数到函数作用域
  for (auto& param : funcDef->params) {
    Symbol sym;
    sym.name = param->name;
    sym.type = param->type;
    sym.isParam = true;
    if (!symTable.addSymbol(sym)) {
      error(param->line, param->col, "parameter '" + param->name + "' already declared");
    }
  }

  // 分析函数体
  if (funcDef->body) {
    for (auto& stmt : funcDef->body->stmts) {
      analyzeStmt(stmt.get());
    }
  }

  // 退出函数作用域
  symTable.exitScope();
}

// ============================================================
// 分析语句
// ============================================================

void SemanticAnalyzer::analyzeStmt(Stmt* stmt) {
  if (auto* block = dynamic_cast<Block*>(stmt)) {
    symTable.enterScope();
    for (auto& s : block->stmts) {
      analyzeStmt(s.get());
    }
    symTable.exitScope();
    return;
  }

  if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
    if (exprStmt->expr) {
      analyzeExpr(exprStmt->expr.get());
    }
    return;
  }

  if (auto* declStmt = dynamic_cast<DeclStmt*>(stmt)) {
    if (declStmt->decl) {
      analyzeDecl(declStmt->decl.get());
    }
    return;
  }

  if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
    analyzeExpr(ifStmt->condition.get());
    analyzeStmt(ifStmt->thenBranch.get());
    if (ifStmt->elseBranch) {
      analyzeStmt(ifStmt->elseBranch.get());
    }
    return;
  }

  if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt)) {
    analyzeExpr(whileStmt->condition.get());
    whileDepth++;
    analyzeStmt(whileStmt->body.get());
    whileDepth--;
    return;
  }

  if (dynamic_cast<BreakStmt*>(stmt)) {
    if (whileDepth == 0) {
      error(stmt->line, stmt->col, "'break' statement not in loop");
    }
    return;
  }

  if (dynamic_cast<ContinueStmt*>(stmt)) {
    if (whileDepth == 0) {
      error(stmt->line, stmt->col, "'continue' statement not in loop");
    }
    return;
  }

  if (auto* returnStmt = dynamic_cast<ReturnStmt*>(stmt)) {
    if (returnStmt->value) {
      // 有返回值
      if (currentFuncRetType == Type::VOID) {
        error(returnStmt->line, returnStmt->col,
              "void function '" + currentFuncName + "' should not return a value");
      }
      Type exprType = analyzeExpr(returnStmt->value.get());
      if (exprType == Type::VOID) {
        error(returnStmt->line, returnStmt->col, "cannot return void expression");
      }
    } else {
      // 无返回值
      if (currentFuncRetType == Type::INT) {
        error(returnStmt->line, returnStmt->col,
              "non-void function '" + currentFuncName + "' must return a value");
      }
    }
    return;
  }
}

// ============================================================
// 分析声明
// ============================================================

void SemanticAnalyzer::analyzeDecl(Decl* decl) {
  // 检查当前作用域内是否已存在同名符号
  if (symTable.lookupCurrent(decl->name)) {
    error(decl->line, decl->col, "variable '" + decl->name + "' already declared in this scope");
    return;
  }

  Symbol sym;
  sym.name = decl->name;
  sym.type = decl->varType;
  sym.isConst = decl->isConst;
  sym.isGlobal = symTable.isGlobalScope();

  // 处理初始化表达式
  if (decl->initExpr) {
    Type initType = analyzeExpr(decl->initExpr.get());

    // 类型检查：不能把 void 赋给变量
    if (initType == Type::VOID) {
      error(decl->line, decl->col, "cannot initialize variable with void expression");
    }

    // 常量折叠
    if (decl->isConst) {
      int foldedValue = 0;
      if (tryConstFold(decl->initExpr.get(), foldedValue)) {
        sym.constValue = foldedValue;
      } else {
        error(decl->line, decl->col,
              "const variable '" + decl->name + "' must be initialized with a constant expression");
      }
    }
  }

  symTable.addSymbol(sym);
}

// ============================================================
// 分析表达式
// ============================================================

Type SemanticAnalyzer::analyzeExpr(Expr* expr) {
  if (dynamic_cast<IntLiteral*>(expr)) {
    expr->type = Type::INT;
    return Type::INT;
  }

  if (auto* varExpr = dynamic_cast<VarExpr*>(expr)) {
    Symbol* sym = symTable.lookup(varExpr->name);
    if (!sym) {
      error(varExpr->line, varExpr->col, "undefined variable '" + varExpr->name + "'");
      expr->type = Type::INT;
      return Type::INT;
    }
    expr->type = sym->type;
    return sym->type;
  }

  if (auto* binaryExpr = dynamic_cast<BinaryExpr*>(expr)) {
    Type lhsType = analyzeExpr(binaryExpr->lhs.get());
    Type rhsType = analyzeExpr(binaryExpr->rhs.get());

    // 赋值运算符特殊处理
    if (binaryExpr->op == BinOp::ASSIGN) {
      // 左值必须是变量
      if (auto* varExpr = dynamic_cast<VarExpr*>(binaryExpr->lhs.get())) {
        Symbol* sym = symTable.lookup(varExpr->name);
        if (sym && sym->isConst) {
          error(varExpr->line, varExpr->col,
                "cannot assign to const variable '" + varExpr->name + "'");
        }
      } else {
        error(binaryExpr->lhs->line, binaryExpr->lhs->col, "assignment target must be a variable");
      }
      expr->type = lhsType;
      return lhsType;
    }

    // 不能对 void 进行运算
    if (lhsType == Type::VOID || rhsType == Type::VOID) {
      error(binaryExpr->line, binaryExpr->col, "cannot perform binary operation on void type");
    }

    expr->type = Type::INT;
    return Type::INT;
  }

  if (auto* unaryExpr = dynamic_cast<UnaryExpr*>(expr)) {
    Type operandType = analyzeExpr(unaryExpr->operand.get());
    if (operandType == Type::VOID) {
      error(unaryExpr->line, unaryExpr->col, "cannot perform unary operation on void type");
    }
    expr->type = Type::INT;
    return Type::INT;
  }

  if (auto* callExpr = dynamic_cast<CallExpr*>(expr)) {
    Symbol* funcSym = symTable.lookupFunction(callExpr->funcName);
    if (!funcSym) {
      error(callExpr->line, callExpr->col, "undefined function '" + callExpr->funcName + "'");
      expr->type = Type::INT;
      return Type::INT;
    }

    // 检查参数数量
    if (callExpr->args.size() != funcSym->paramTypes.size()) {
      error(callExpr->line, callExpr->col,
            "function '" + callExpr->funcName + "' expects " +
                std::to_string(funcSym->paramTypes.size()) + " arguments, but " +
                std::to_string(callExpr->args.size()) + " were given");
    }

    // 分析每个参数
    for (auto& arg : callExpr->args) {
      analyzeExpr(arg.get());
    }

    expr->type = funcSym->type;
    return funcSym->type;
  }

  expr->type = Type::INT;
  return Type::INT;
}

// ============================================================
// 常量折叠
// ============================================================

bool SemanticAnalyzer::isConstExpr(Expr* expr) {
  if (dynamic_cast<IntLiteral*>(expr)) {
    return true;
  }

  if (auto* varExpr = dynamic_cast<VarExpr*>(expr)) {
    Symbol* sym = symTable.lookup(varExpr->name);
    // 只有已定义的 const 变量才可折叠
    return sym && sym->isConst;
  }

  if (auto* binaryExpr = dynamic_cast<BinaryExpr*>(expr)) {
    return isConstExpr(binaryExpr->lhs.get()) && isConstExpr(binaryExpr->rhs.get());
  }

  if (auto* unaryExpr = dynamic_cast<UnaryExpr*>(expr)) {
    return isConstExpr(unaryExpr->operand.get());
  }

  return false;
}

bool SemanticAnalyzer::tryConstFold(Expr* expr, int& result) {
  if (auto* intLit = dynamic_cast<IntLiteral*>(expr)) {
    result = intLit->value;
    return true;
  }

  if (auto* varExpr = dynamic_cast<VarExpr*>(expr)) {
    Symbol* sym = symTable.lookup(varExpr->name);
    if (sym && sym->isConst) {
      result = sym->constValue;
      return true;
    }
    return false;
  }

  if (auto* binaryExpr = dynamic_cast<BinaryExpr*>(expr)) {
    int lhsVal = 0, rhsVal = 0;
    if (!tryConstFold(binaryExpr->lhs.get(), lhsVal))
      return false;
    if (!tryConstFold(binaryExpr->rhs.get(), rhsVal))
      return false;

    switch (binaryExpr->op) {
    case BinOp::ADD:
      result = lhsVal + rhsVal;
      break;
    case BinOp::SUB:
      result = lhsVal - rhsVal;
      break;
    case BinOp::MUL:
      result = lhsVal * rhsVal;
      break;
    case BinOp::DIV:
      if (rhsVal == 0)
        return false; // 除以零
      result = lhsVal / rhsVal;
      break;
    case BinOp::MOD:
      if (rhsVal == 0)
        return false;
      result = lhsVal % rhsVal;
      break;
    case BinOp::LT:
      result = (lhsVal < rhsVal) ? 1 : 0;
      break;
    case BinOp::GT:
      result = (lhsVal > rhsVal) ? 1 : 0;
      break;
    case BinOp::LE:
      result = (lhsVal <= rhsVal) ? 1 : 0;
      break;
    case BinOp::GE:
      result = (lhsVal >= rhsVal) ? 1 : 0;
      break;
    case BinOp::EQ:
      result = (lhsVal == rhsVal) ? 1 : 0;
      break;
    case BinOp::NE:
      result = (lhsVal != rhsVal) ? 1 : 0;
      break;
    case BinOp::AND:
      result = (lhsVal && rhsVal) ? 1 : 0;
      break;
    case BinOp::OR:
      result = (lhsVal || rhsVal) ? 1 : 0;
      break;
    default:
      return false;
    }
    return true;
  }

  if (auto* unaryExpr = dynamic_cast<UnaryExpr*>(expr)) {
    int operandVal = 0;
    if (!tryConstFold(unaryExpr->operand.get(), operandVal))
      return false;

    switch (unaryExpr->op) {
    case UnaryOp::POS:
      result = operandVal;
      break;
    case UnaryOp::NEG:
      result = -operandVal;
      break;
    case UnaryOp::NOT:
      result = (operandVal == 0) ? 1 : 0;
      break;
    }
    return true;
  }

  return false;
}

} // namespace toycc