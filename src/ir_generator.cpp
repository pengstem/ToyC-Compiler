#include "ir_generator.h"

#include <cassert>

namespace toycc {

IRGenerator::IRGenerator(SymbolTable& symTable)
    : symTable(symTable) {}

// ============================================================
// 工具方法
// ============================================================

std::string IRGenerator::newTemp() {
  return "t" + std::to_string(tempCounter++);
}

std::string IRGenerator::newLabel() {
  return "L" + std::to_string(labelCounter++);
}

void IRGenerator::emit(IROp op, Operand dest, Operand src1, Operand src2) {
  ir.push_back(IRInst(op, std::move(dest), std::move(src1), std::move(src2)));
}

// ============================================================
// 主入口
// ============================================================

std::vector<IRInst> IRGenerator::generate(CompUnit& compUnit) {
  ir.clear();
  tempCounter = 0;
  labelCounter = 0;

  for (auto& node : compUnit.globalDecls) {
    genGlobalDecl(node.get());
  }

  return ir;
}

void IRGenerator::genGlobalDecl(ASTNode* node) {
  if (auto* funcDef = dynamic_cast<FuncDef*>(node)) {
    genFuncDef(funcDef);
  } else if (auto* decl = dynamic_cast<Decl*>(node)) {
    genDecl(decl);
  }
}

// ============================================================
// 生成函数定义
// ============================================================

void IRGenerator::genFuncDef(FuncDef* funcDef) {
  isGlobalContext = false;
  currentFuncName = funcDef->name;
  currentFuncRetType = funcDef->retType;

  emit(IROp::FUNC_BEGIN, Operand::func(funcDef->name));

  // 声明参数为局部变量
  for (auto& param : funcDef->params) {
    emit(IROp::LOCAL_VAR_DECL, Operand::localVar(param->name), Operand::param(param->name));
  }

  // 生成函数体 IR
  if (funcDef->body) {
    for (auto& stmt : funcDef->body->stmts) {
      genStmt(stmt.get());
    }
  }

  // 确保非 void 函数有默认返回
  if (currentFuncRetType == Type::INT) {
    emit(IROp::RETURN, Operand::imm(0));
  } else {
    emit(IROp::RETURN);
  }

  emit(IROp::FUNC_END, Operand::func(funcDef->name));
}

// ============================================================
// 生成声明
// ============================================================

void IRGenerator::genDecl(Decl* decl) {
  if (decl->isConst) {
    // const 变量已折叠，直接生成带有立即数值的局部变量
    Symbol* sym = symTable.lookup(decl->name);
    int initVal = (sym ? sym->constValue : 0);
    emit(IROp::LOCAL_VAR_DECL, Operand::localVar(decl->name), Operand::imm(initVal));
    return;
  }

  if (isGlobalContext) {
    // 全局变量
    int initVal = 0;
    emit(IROp::GLOBAL_VAR_DECL, Operand::globalVar(decl->name), Operand::imm(initVal));
    if (decl->initExpr) {
      Operand val = genExpr(decl->initExpr.get());
      emit(IROp::ASSIGN, Operand::globalVar(decl->name), val);
    }
    return;
  }

  // 局部变量
  emit(IROp::LOCAL_VAR_DECL, Operand::localVar(decl->name));

  if (decl->initExpr) {
    Operand val = genExpr(decl->initExpr.get());
    emit(IROp::ASSIGN, Operand::localVar(decl->name), val);
  }
}

// ============================================================
// 生成语句
// ============================================================

void IRGenerator::genStmt(Stmt* stmt) {
  if (auto* block = dynamic_cast<Block*>(stmt)) {
    for (auto& s : block->stmts) {
      genStmt(s.get());
    }
    return;
  }

  if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
    if (exprStmt->expr) {
      genExpr(exprStmt->expr.get());
    }
    return;
  }

  if (auto* declStmt = dynamic_cast<DeclStmt*>(stmt)) {
    if (declStmt->decl) {
      genDecl(declStmt->decl.get());
    }
    return;
  }

  if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
    std::string elseLabel = newLabel();
    std::string endLabel = newLabel();

    Operand cond = genExpr(ifStmt->condition.get());
    emit(IROp::BEQZ, Operand::label(elseLabel), cond);

    genStmt(ifStmt->thenBranch.get());

    if (ifStmt->elseBranch) {
      emit(IROp::BRANCH, Operand::label(endLabel));
    }

    emit(IROp::LABEL, Operand::label(elseLabel));

    if (ifStmt->elseBranch) {
      genStmt(ifStmt->elseBranch.get());
      emit(IROp::LABEL, Operand::label(endLabel));
    }
    return;
  }

  if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt)) {
    std::string condLabel = newLabel();
    std::string bodyLabel = newLabel();
    std::string endLabel = newLabel();

    // 压入循环上下文
    loopStack.push({endLabel, condLabel});

    // 条件判断
    emit(IROp::LABEL, Operand::label(condLabel));
    Operand cond = genExpr(whileStmt->condition.get());
    emit(IROp::BEQZ, Operand::label(endLabel), cond);

    // 循环体
    emit(IROp::LABEL, Operand::label(bodyLabel));
    genStmt(whileStmt->body.get());
    emit(IROp::BRANCH, Operand::label(condLabel));

    // 循环结束
    emit(IROp::LABEL, Operand::label(endLabel));

    loopStack.pop();
    return;
  }

  if (dynamic_cast<BreakStmt*>(stmt)) {
    if (!loopStack.empty()) {
      emit(IROp::BRANCH, Operand::label(loopStack.top().breakLabel));
    }
    return;
  }

  if (dynamic_cast<ContinueStmt*>(stmt)) {
    if (!loopStack.empty()) {
      emit(IROp::BRANCH, Operand::label(loopStack.top().continueLabel));
    }
    return;
  }

  if (auto* returnStmt = dynamic_cast<ReturnStmt*>(stmt)) {
    if (returnStmt->value) {
      Operand val = genExpr(returnStmt->value.get());
      emit(IROp::RETURN, val);
    } else {
      emit(IROp::RETURN);
    }
    return;
  }
}

// ============================================================
// 生成表达式
// ============================================================

Operand IRGenerator::genExpr(Expr* expr) {
  if (auto* intLit = dynamic_cast<IntLiteral*>(expr)) {
    return Operand::imm(intLit->value);
  }

  if (auto* varExpr = dynamic_cast<VarExpr*>(expr)) {
    Symbol* sym = symTable.lookup(varExpr->name);
    if (sym && sym->isConst) {
      // 常量直接返回立即数
      return Operand::imm(sym->constValue);
    }
    if (sym && sym->isGlobal) {
      return Operand::globalVar(varExpr->name);
    }
    return Operand::localVar(varExpr->name);
  }

  if (auto* binaryExpr = dynamic_cast<BinaryExpr*>(expr)) {
    // 短路求值：&& 和 ||
    if (binaryExpr->op == BinOp::AND || binaryExpr->op == BinOp::OR) {
      return genShortCircuit(binaryExpr);
    }

    // 赋值
    if (binaryExpr->op == BinOp::ASSIGN) {
      Operand rhs = genExpr(binaryExpr->rhs.get());
      Operand lhs;
      if (auto* varExpr = dynamic_cast<VarExpr*>(binaryExpr->lhs.get())) {
        Symbol* sym = symTable.lookup(varExpr->name);
        if (sym && sym->isGlobal) {
          lhs = Operand::globalVar(varExpr->name);
        } else {
          lhs = Operand::localVar(varExpr->name);
        }
      }
      emit(IROp::ASSIGN, lhs, rhs);
      return lhs;
    }

    // 普通二元运算
    Operand lhsVal = genExpr(binaryExpr->lhs.get());
    Operand rhsVal = genExpr(binaryExpr->rhs.get());
    Operand result = Operand::localVar(newTemp());

    IROp irOp;
    switch (binaryExpr->op) {
    case BinOp::ADD:
      irOp = IROp::ADD;
      break;
    case BinOp::SUB:
      irOp = IROp::SUB;
      break;
    case BinOp::MUL:
      irOp = IROp::MUL;
      break;
    case BinOp::DIV:
      irOp = IROp::DIV;
      break;
    case BinOp::MOD:
      irOp = IROp::MOD;
      break;
    case BinOp::LT:
      irOp = IROp::LT;
      break;
    case BinOp::GT:
      irOp = IROp::GT;
      break;
    case BinOp::LE:
      irOp = IROp::LE;
      break;
    case BinOp::GE:
      irOp = IROp::GE;
      break;
    case BinOp::EQ:
      irOp = IROp::EQ;
      break;
    case BinOp::NE:
      irOp = IROp::NE;
      break;
    default:
      irOp = IROp::ADD;
      break;
    }

    emit(irOp, result, lhsVal, rhsVal);
    return result;
  }

  if (auto* unaryExpr = dynamic_cast<UnaryExpr*>(expr)) {
    Operand operandVal = genExpr(unaryExpr->operand.get());
    Operand result = Operand::localVar(newTemp());

    IROp irOp;
    switch (unaryExpr->op) {
    case UnaryOp::POS:
      irOp = IROp::ASSIGN;
      break; // 正号直接赋值
    case UnaryOp::NEG:
      irOp = IROp::SUB;
      break; // 取负 = 0 - x
    case UnaryOp::NOT:
      irOp = IROp::NOT;
      break;
    }

    if (unaryExpr->op == UnaryOp::POS) {
      emit(irOp, result, operandVal);
    } else if (unaryExpr->op == UnaryOp::NEG) {
      emit(irOp, result, Operand::imm(0), operandVal);
    } else {
      emit(irOp, result, operandVal);
    }
    return result;
  }

  if (auto* callExpr = dynamic_cast<CallExpr*>(expr)) {
    // 生成参数传递
    for (size_t i = 0; i < callExpr->args.size(); ++i) {
      Operand argVal = genExpr(callExpr->args[i].get());
      emit(IROp::PARAM, argVal, Operand::imm(static_cast<int>(i)));
    }

    Symbol* funcSym = symTable.lookupFunction(callExpr->funcName);
    if (funcSym && funcSym->type != Type::VOID) {
      Operand result = Operand::localVar(newTemp());
      emit(IROp::CALL, result, Operand::func(callExpr->funcName));
      return result;
    }

    emit(IROp::CALL, Operand::none(), Operand::func(callExpr->funcName));
    return Operand::none();
  }

  return Operand::imm(0);
}

// ============================================================
// 短路求值
// ============================================================

Operand IRGenerator::genShortCircuit(BinaryExpr* expr) {
  std::string trueLabel = newLabel();
  std::string falseLabel = newLabel();
  std::string endLabel = newLabel();
  Operand result = Operand::localVar(newTemp());

  if (expr->op == BinOp::AND) {
    // a && b:
    //   if !a goto L_false
    //   if !b goto L_false
    //   result = 1
    //   goto L_end
    // L_false:
    //   result = 0
    // L_end:

    Operand lhs = genExpr(expr->lhs.get());
    emit(IROp::BEQZ, Operand::label(falseLabel), lhs);

    Operand rhs = genExpr(expr->rhs.get());
    emit(IROp::BEQZ, Operand::label(falseLabel), rhs);

    emit(IROp::ASSIGN, result, Operand::imm(1));
    emit(IROp::BRANCH, Operand::label(endLabel));

    emit(IROp::LABEL, Operand::label(falseLabel));
    emit(IROp::ASSIGN, result, Operand::imm(0));

    emit(IROp::LABEL, Operand::label(endLabel));
  } else {
    // a || b:
    //   if a goto L_true
    //   if b goto L_true
    //   result = 0
    //   goto L_end
    // L_true:
    //   result = 1
    // L_end:

    Operand lhs = genExpr(expr->lhs.get());
    emit(IROp::BNEZ, Operand::label(trueLabel), lhs);

    Operand rhs = genExpr(expr->rhs.get());
    emit(IROp::BNEZ, Operand::label(trueLabel), rhs);

    emit(IROp::ASSIGN, result, Operand::imm(0));
    emit(IROp::BRANCH, Operand::label(endLabel));

    emit(IROp::LABEL, Operand::label(trueLabel));
    emit(IROp::ASSIGN, result, Operand::imm(1));

    emit(IROp::LABEL, Operand::label(endLabel));
  }

  return result;
}

} // namespace toycc