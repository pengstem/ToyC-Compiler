#include "ir_generator.h"

#include <cassert>

namespace toycc {

IRGenerator::IRGenerator(SymbolTable& st)
    : symTable(st) {}

// ============================================================
// 工具方法
// ============================================================

std::string IRGenerator::newTemp() {
  return "t" + std::to_string(tempCounter++);
}

std::string IRGenerator::newLabel() {
  return "L" + std::to_string(labelCounter++);
}

void IRGenerator::enterVarScope() {
  varScopeStack_.push_back({});
}

void IRGenerator::exitVarScope() {
  if (!varScopeStack_.empty()) {
    varScopeStack_.pop_back();
  }
}

std::string IRGenerator::declareVar(const std::string& name) {
  std::string irName = name + "." + std::to_string(varCounter_++);
  varScopeStack_.back()[name] = irName;
  return irName;
}

std::string IRGenerator::resolveVar(const std::string& name) {
  for (auto it = varScopeStack_.rbegin(); it != varScopeStack_.rend(); ++it) {
    auto found = it->find(name);
    if (found != it->end()) {
      return found->second;
    }
  }
  return name;
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

  optimizePass();

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

  // 进入函数作用域
  enterVarScope();

  // 声明参数为局部变量（使用唯一 IR 名称）
  for (auto& param : funcDef->params) {
    std::string irName = declareVar(param->name);
    emit(IROp::LOCAL_VAR_DECL, Operand::localVar(irName), Operand::param(param->name));
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

  // 退出函数作用域
  exitVarScope();
}

// ============================================================
// 生成声明
// ============================================================

void IRGenerator::genDecl(Decl* decl) {
  if (decl->isConst) {
    // const 变量已在语义分析阶段折叠，引用处直接使用立即数，无需声明
    return;
  }

  if (decl->isGlobalDecl) {
    // 全局变量：初值已在语义分析阶段常量折叠到 decl->constValue，
    // 直接作为 GLOBAL_VAR_DECL 的立即数携带，由代码生成器写入 .data 段
    emit(IROp::GLOBAL_VAR_DECL, Operand::globalVar(decl->name), Operand::imm(decl->constValue));
    return;
  }

  // 局部变量：使用唯一 IR 名称避免遮蔽冲突
  std::string irName = declareVar(decl->name);
  emit(IROp::LOCAL_VAR_DECL, Operand::localVar(irName));

  if (decl->initExpr) {
    Operand val = genExpr(decl->initExpr.get());
    emit(IROp::ASSIGN, Operand::localVar(irName), val);
  }
}

// ============================================================
// 生成语句
// ============================================================

void IRGenerator::genStmt(Stmt* stmt) {
  if (auto* block = dynamic_cast<Block*>(stmt)) {
    enterVarScope();
    for (auto& s : block->stmts) {
      genStmt(s.get());
    }
    exitVarScope();
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
    if (varExpr->resolvedIsConst) {
      return Operand::imm(varExpr->resolvedConstValue);
    }
    if (varExpr->resolvedIsGlobal) {
      return Operand::globalVar(varExpr->name);
    }
    std::string irName = resolveVar(varExpr->name);
    return Operand::localVar(irName);
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
        if (varExpr->resolvedIsGlobal) {
          lhs = Operand::globalVar(varExpr->name);
        } else {
          std::string irName = resolveVar(varExpr->name);
          lhs = Operand::localVar(irName);
        }
      }
      emit(IROp::ASSIGN, lhs, rhs);
      return lhs;
    }

    // 普通二元运算
    Operand lhsVal = genExpr(binaryExpr->lhs.get());
    Operand rhsVal = genExpr(binaryExpr->rhs.get());

    // 常量折叠：两个操作数都是立即数时直接编译期计算
    if (lhsVal.isImm() && rhsVal.isImm()) {
      const int a = lhsVal.immVal;
      const int b = rhsVal.immVal;
      int result = 0;
      bool folded = true;
      switch (binaryExpr->op) {
      case BinOp::ADD:
        result = a + b;
        break;
      case BinOp::SUB:
        result = a - b;
        break;
      case BinOp::MUL:
        result = a * b;
        break;
      case BinOp::DIV:
        if (b == 0) {
          folded = false;
        } else {
          result = a / b;
        }
        break;
      case BinOp::MOD:
        if (b == 0) {
          folded = false;
        } else {
          result = a % b;
        }
        break;
      case BinOp::LT:
        result = (a < b) ? 1 : 0;
        break;
      case BinOp::GT:
        result = (a > b) ? 1 : 0;
        break;
      case BinOp::LE:
        result = (a <= b) ? 1 : 0;
        break;
      case BinOp::GE:
        result = (a >= b) ? 1 : 0;
        break;
      case BinOp::EQ:
        result = (a == b) ? 1 : 0;
        break;
      case BinOp::NE:
        result = (a != b) ? 1 : 0;
        break;
      default:
        folded = false;
        break;
      }
      if (folded) {
        return Operand::imm(result);
      }
    }

    // 代数简化：避免无意义的运行时运算
    switch (binaryExpr->op) {
    case BinOp::ADD:
      // x + 0 = x, 0 + x = x
      if (rhsVal.isImm() && rhsVal.immVal == 0)
        return lhsVal;
      if (lhsVal.isImm() && lhsVal.immVal == 0)
        return rhsVal;
      break;
    case BinOp::SUB:
      // x - 0 = x
      if (rhsVal.isImm() && rhsVal.immVal == 0)
        return lhsVal;
      break;
    case BinOp::MUL:
      // x * 0 = 0
      if (rhsVal.isImm() && rhsVal.immVal == 0)
        return Operand::imm(0);
      if (lhsVal.isImm() && lhsVal.immVal == 0)
        return Operand::imm(0);
      // x * 1 = x, 1 * x = x
      if (rhsVal.isImm() && rhsVal.immVal == 1)
        return lhsVal;
      if (lhsVal.isImm() && lhsVal.immVal == 1)
        return rhsVal;
      break;
    case BinOp::DIV:
      // x / 1 = x
      if (rhsVal.isImm() && rhsVal.immVal == 1)
        return lhsVal;
      break;
    case BinOp::MOD:
      // x % 1 = 0
      if (rhsVal.isImm() && rhsVal.immVal == 1)
        return Operand::imm(0);
      break;
    default:
      break;
    }

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

    // 常量折叠：操作数为立即数时直接编译期计算
    if (operandVal.isImm()) {
      switch (unaryExpr->op) {
      case UnaryOp::POS:
        return operandVal;
      case UnaryOp::NEG:
        return Operand::imm(-operandVal.immVal);
      case UnaryOp::NOT:
        return Operand::imm(operandVal.immVal == 0 ? 1 : 0);
      }
    }

    // 代数简化：+x = x
    if (unaryExpr->op == UnaryOp::POS) {
      return operandVal;
    }

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

// ============================================================
// IR 优化 pass
// ============================================================

namespace {

bool isCombinableOp(IROp op) {
  switch (op) {
  case IROp::ADD:
  case IROp::SUB:
  case IROp::MUL:
  case IROp::DIV:
  case IROp::MOD:
  case IROp::NOT:
  case IROp::LT:
  case IROp::GT:
  case IROp::LE:
  case IROp::GE:
  case IROp::EQ:
  case IROp::NE:
    return true;
  default:
    return false;
  }
}

void countUses(const std::vector<IRInst>& ir, std::unordered_map<std::string, int>& useCount) {
  useCount.clear();
  for (const auto& inst : ir) {
    if (inst.src1.isLocalVar()) {
      useCount[inst.src1.name]++;
    }
    if (inst.src2.isLocalVar()) {
      useCount[inst.src2.name]++;
    }
    // RETURN 和 PARAM 的 dest 实际上是源操作数（被使用的值）
    if (inst.op == IROp::RETURN || inst.op == IROp::PARAM) {
      if (inst.dest.isLocalVar()) {
        useCount[inst.dest.name]++;
      }
    }
  }
}

} // namespace

void IRGenerator::optimizePass() {
  // Pass 1: 合并 OP+ASSIGN 模式
  // 当 OP %tmp, ... 紧跟 ASSIGN %dest, %tmp，且 %tmp 只被使用一次（就是这个 ASSIGN），
  // 合并为 OP %dest, ...，消除临时变量的 store/load 往返
  {
    std::unordered_map<std::string, int> useCount;
    countUses(ir, useCount);

    std::vector<IRInst> optimized;
    std::size_t i = 0;
    while (i < ir.size()) {
      if (i + 1 < ir.size() && isCombinableOp(ir[i].op) && ir[i].dest.isLocalVar() &&
          ir[i + 1].op == IROp::ASSIGN && ir[i + 1].src1.isLocalVar() &&
          ir[i + 1].src1.name == ir[i].dest.name && useCount[ir[i].dest.name] == 1 &&
          (ir[i + 1].dest.isLocalVar() || ir[i + 1].dest.isGlobalVar())) {
        // 合并：运算结果直接写入 ASSIGN 的目标
        IRInst merged = ir[i];
        merged.dest = ir[i + 1].dest;
        optimized.push_back(merged);
        i += 2;
      } else {
        optimized.push_back(ir[i]);
        i += 1;
      }
    }
    ir = std::move(optimized);
  }

  // Pass 2: 死代码消除 — 删除结果从未被使用的局部变量运算/赋值
  // 注意：不删除对 GLOBAL_VAR 的赋值（可能有可观察副作用）
  {
    std::unordered_map<std::string, int> useCount;
    countUses(ir, useCount);

    std::vector<IRInst> optimized;
    for (const auto& inst : ir) {
      if (isCombinableOp(inst.op) && inst.dest.isLocalVar() && useCount[inst.dest.name] == 0) {
        continue; // 运算结果从未被使用，删除
      }
      if (inst.op == IROp::ASSIGN && inst.dest.isLocalVar() && useCount[inst.dest.name] == 0) {
        continue; // 赋值结果从未被使用，删除
      }
      optimized.push_back(inst);
    }
    ir = std::move(optimized);
  }
}

} // namespace toycc