#include "ir_generator.h"

#include <cassert>
#include <cctype>
#include <iostream>
#include <unordered_set>

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
    for (const auto& decl : declStmt->decls) {
      genDecl(decl.get());
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
      // 自赋值（如 `t = t;`）是纯复制无操作，跳过发射避免产生
      // `ASSIGN x, x` 与后续 pass 交互产生悬垂引用（fuzz FAIL_80）
      const bool selfAssign = lhs.isLocalVar() && rhs.isLocalVar() && lhs.name == rhs.name;
      if (!selfAssign) {
        emit(IROp::ASSIGN, lhs, rhs);
      }
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
    // 普通指令的 dest 是定义而非使用，不计数（否则会阻止 OP+ASSIGN 合并）
    if (inst.op == IROp::RETURN || inst.op == IROp::PARAM) {
      if (inst.dest.isLocalVar()) {
        useCount[inst.dest.name]++;
      }
    }
  }
}

// 判断变量名是否为 IR 临时变量（t0, t1, ...）
bool isTempName(const std::string& name) {
  if (name.empty() || name[0] != 't') {
    return false;
  }
  for (std::size_t i = 1; i < name.size(); ++i) {
    if (name[i] < '0' || name[i] > '9') {
      return false;
    }
  }
  return !name.empty();
}

// 判断指令是否适合出现在循环条件块中（无副作用，且只写临时变量）
// 这样的条件块可以被安全地移动到循环体末尾（loop inversion）
bool isPureCondInst(const IRInst& inst) {
  switch (inst.op) {
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
    return inst.dest.isLocalVar() && isTempName(inst.dest.name);
  case IROp::ASSIGN:
    // 只允许给临时变量赋值（循环条件中的中间计算结果）
    return inst.dest.isLocalVar() && isTempName(inst.dest.name);
  default:
    return false;
  }
}

} // namespace

void IRGenerator::optimizePass() {
  // 迭代优化直到 IR 不再变化（常量传播 → 折叠 → 合并 → DCE 可能级联触发）
  bool changed = true;
  int maxIterations = 8;
  while (changed && maxIterations-- > 0) {
    changed = false;

    // Pass 0: 局部常量传播 + 复制传播
    // constMap: 跟踪 ASSIGN x, #imm，在后续使用 x 的地方替换为 #imm
    // copyMap:  跟踪 ASSIGN x, y(局部变量)，在后续使用 x 的地方替换为 y
    // 遇到 LABEL/CALL/BRANCH 等控制流变化时保守清除所有映射
    {
      std::unordered_map<std::string, int> constMap;
      std::unordered_map<std::string, std::string> copyMap;

      auto resolveOperand = [&](Operand& op) {
        if (!op.isLocalVar()) {
          return;
        }
        // 跟随复制链（最多 4 跳，避免潜在环路）
        for (int hop = 0; hop < 4; ++hop) {
          auto cit = constMap.find(op.name);
          if (cit != constMap.end()) {
            op = Operand::imm(cit->second);
            changed = true;
            return;
          }
          auto it = copyMap.find(op.name);
          if (it == copyMap.end()) {
            return;
          }
          op = Operand::localVar(it->second);
          changed = true;
        }
      };

      auto invalidateDest = [&](const Operand& dest) {
        if (!dest.isLocalVar()) {
          return;
        }
        constMap.erase(dest.name);
        copyMap.erase(dest.name);
        // 指向 dest 的复制也失效（dest 值即将改变）
        for (auto it = copyMap.begin(); it != copyMap.end();) {
          if (it->second == dest.name) {
            it = copyMap.erase(it);
          } else {
            ++it;
          }
        }
      };

      for (auto& inst : ir) {
        // 控制流边界：清除所有映射（保守处理）
        if (inst.op == IROp::LABEL || inst.op == IROp::BRANCH || inst.op == IROp::BEQZ ||
            inst.op == IROp::BNEZ || inst.op == IROp::CALL) {
          constMap.clear();
          copyMap.clear();
          if (inst.op != IROp::CALL) {
            continue;
          }
        }

        // 传播：将 src 中引用常量/复制变量的地方替换
        resolveOperand(inst.src1);
        resolveOperand(inst.src2);
        if (inst.op == IROp::RETURN || inst.op == IROp::PARAM) {
          resolveOperand(inst.dest);
        }

        // 失效 dest 的旧映射（在记录新映射之前）
        invalidateDest(inst.dest);

        // 记录新映射
        if (inst.op == IROp::ASSIGN && inst.dest.isLocalVar()) {
          if (inst.src1.isImm()) {
            constMap[inst.dest.name] = inst.src1.immVal;
          } else if (inst.src1.isLocalVar() && inst.src1.name != inst.dest.name) {
            copyMap[inst.dest.name] = inst.src1.name;
          }
        }
      }
    }

    // Pass 0b: 常量折叠（传播后两个操作数可能都变成立即数）
    {
      for (auto& inst : ir) {
        if (!isCombinableOp(inst.op)) {
          continue;
        }
        if (inst.op == IROp::NOT) {
          if (inst.src1.isImm()) {
            const int val = inst.src1.immVal == 0 ? 1 : 0;
            inst.op = IROp::ASSIGN;
            inst.src1 = Operand::imm(val);
            inst.src2 = Operand::none();
            changed = true;
          }
          continue;
        }
        if (inst.src1.isImm() && inst.src2.isImm()) {
          const int a = inst.src1.immVal;
          const int b = inst.src2.immVal;
          int result = 0;
          bool folded = true;
          switch (inst.op) {
          case IROp::ADD:
            result = a + b;
            break;
          case IROp::SUB:
            result = a - b;
            break;
          case IROp::MUL:
            result = a * b;
            break;
          case IROp::DIV:
            if (b == 0) {
              folded = false;
            } else {
              result = a / b;
            }
            break;
          case IROp::MOD:
            if (b == 0) {
              folded = false;
            } else {
              result = a % b;
            }
            break;
          case IROp::LT:
            result = (a < b) ? 1 : 0;
            break;
          case IROp::GT:
            result = (a > b) ? 1 : 0;
            break;
          case IROp::LE:
            result = (a <= b) ? 1 : 0;
            break;
          case IROp::GE:
            result = (a >= b) ? 1 : 0;
            break;
          case IROp::EQ:
            result = (a == b) ? 1 : 0;
            break;
          case IROp::NE:
            result = (a != b) ? 1 : 0;
            break;
          default:
            folded = false;
            break;
          }
          if (folded) {
            inst.op = IROp::ASSIGN;
            inst.src1 = Operand::imm(result);
            inst.src2 = Operand::none();
            changed = true;
          }
        }
      }
    }

    // Pass 0c: 代数简化
    {
      for (auto& inst : ir) {
        if (!isCombinableOp(inst.op) || inst.op == IROp::NOT) {
          continue;
        }
        // x + 0 = x, 0 + x = x
        if (inst.op == IROp::ADD) {
          if (inst.src2.isImm() && inst.src2.immVal == 0) {
            inst.op = IROp::ASSIGN;
            inst.src2 = Operand::none();
            changed = true;
          } else if (inst.src1.isImm() && inst.src1.immVal == 0) {
            inst.op = IROp::ASSIGN;
            inst.src1 = inst.src2;
            inst.src2 = Operand::none();
            changed = true;
          }
        }
        // x - 0 = x
        if (inst.op == IROp::SUB && inst.src2.isImm() && inst.src2.immVal == 0) {
          inst.op = IROp::ASSIGN;
          inst.src2 = Operand::none();
          changed = true;
        }
        // x * 0 = 0
        if (inst.op == IROp::MUL) {
          if ((inst.src1.isImm() && inst.src1.immVal == 0) ||
              (inst.src2.isImm() && inst.src2.immVal == 0)) {
            inst.op = IROp::ASSIGN;
            inst.src1 = Operand::imm(0);
            inst.src2 = Operand::none();
            changed = true;
          } else if (inst.src2.isImm() && inst.src2.immVal == 1) {
            inst.op = IROp::ASSIGN;
            inst.src2 = Operand::none();
            changed = true;
          } else if (inst.src1.isImm() && inst.src1.immVal == 1) {
            inst.op = IROp::ASSIGN;
            inst.src1 = inst.src2;
            inst.src2 = Operand::none();
            changed = true;
          }
        }
        // x / 1 = x
        if (inst.op == IROp::DIV && inst.src2.isImm() && inst.src2.immVal == 1) {
          inst.op = IROp::ASSIGN;
          inst.src2 = Operand::none();
          changed = true;
        }
        // x % 1 = 0
        if (inst.op == IROp::MOD && inst.src2.isImm() && inst.src2.immVal == 1) {
          inst.op = IROp::ASSIGN;
          inst.src1 = Operand::imm(0);
          inst.src2 = Operand::none();
          changed = true;
        }
        // x % -1 = 0
        if (inst.op == IROp::MOD && inst.src2.isImm() && inst.src2.immVal == -1) {
          inst.op = IROp::ASSIGN;
          inst.src1 = Operand::imm(0);
          inst.src2 = Operand::none();
          changed = true;
        }
      }
    }

    // Pass 0d: 取模重写 x % c -> x - (x / c) * c（c 为常量且 |c| > 1）
    // 语义等价（C 的取模定义 x % c == x - (x/c)*c）。改写后除法指令可被
    // Pass 1.5 CSE 共享：如 `i / 7 + i % 7` 只需计算一次 magic 除法，
    // 消除代码生成阶段重复的 mulh 序列。
    {
      std::vector<IRInst> optimized;
      bool modRewritten = false;
      for (const auto& inst : ir) {
        if (inst.op == IROp::MOD && inst.src2.isImm() &&
            (inst.src2.immVal > 1 || inst.src2.immVal < -1)) {
          const int c = inst.src2.immVal;
          const std::string q = newTemp(); // x / c
          const std::string m = newTemp(); // (x/c) * c
          optimized.push_back(IRInst(IROp::DIV, Operand::localVar(q), inst.src1, Operand::imm(c)));
          optimized.push_back(
              IRInst(IROp::MUL, Operand::localVar(m), Operand::localVar(q), Operand::imm(c)));
          optimized.push_back(IRInst(IROp::SUB, inst.dest, inst.src1, Operand::localVar(m)));
          modRewritten = true;
        } else {
          optimized.push_back(inst);
        }
      }
      if (modRewritten) {
        ir = std::move(optimized);
        changed = true;
      }
    }

    // Pass 1: 合并 OP+ASSIGN 模式
    // "OP t, a, b; ASSIGN x, t" -> "OP x, a, b"
    // 同时把后续对 t 的引用替换为 x（仅限 x 未被重新赋值之前），
    // 消除 sum = sum op expr 时多余的临时寄存器往返
    {
      // tmp 全 IR 使用次数：若 tmp 在替换窗口之外仍有使用（如复制传播
      // 产生的跨长距离引用），合并会删掉 tmp 的定义留下悬垂引用，必须跳过
      std::unordered_map<std::string, int> mergeUseCount;
      countUses(ir, mergeUseCount);
      std::vector<IRInst> optimized;
      std::size_t i = 0;
      while (i < ir.size()) {
        bool mergedHere = false;
        if (i + 1 < ir.size() && isCombinableOp(ir[i].op) && ir[i].dest.isLocalVar() &&
            ir[i + 1].op == IROp::ASSIGN && ir[i + 1].src1.isLocalVar() &&
            ir[i + 1].src1.name == ir[i].dest.name && ir[i].dest.name != ir[i + 1].dest.name &&
            (ir[i + 1].dest.isLocalVar() || ir[i + 1].dest.isGlobalVar())) {
          const std::string tmp = ir[i].dest.name;
          const std::string target = ir[i + 1].dest.name;
          // 向后扫描：收集在 target 被重新赋值之前对 tmp 的引用
          std::vector<std::size_t> refsToReplace;
          bool ok = true;
          for (std::size_t j = i + 2; j < ir.size(); ++j) {
            const auto& inst = ir[j];
            // 控制流/调用边界：保守停止
            if (inst.op == IROp::LABEL || inst.op == IROp::BRANCH || inst.op == IROp::BEQZ ||
                inst.op == IROp::BNEZ || inst.op == IROp::CALL) {
              break;
            }
            // 记录 tmp 引用（必须先于重定义检查：`ASSIGN target, tmp` 这类指令
            // 同时使用 tmp 并重新定义 target，若先 break 会漏替换，导致悬垂引用；
            // 例如 `t = expr; t = t;` 经复制传播后变成 `ASSIGN t, tmp`，
            // 其 tmp 定义随后被本次合并消除）
            const bool usesTmp = (inst.src1.isLocalVar() && inst.src1.name == tmp) ||
                                 (inst.src2.isLocalVar() && inst.src2.name == tmp) ||
                                 ((inst.op == IROp::RETURN || inst.op == IROp::PARAM) &&
                                  inst.dest.isLocalVar() && inst.dest.name == tmp);
            if (usesTmp) {
              refsToReplace.push_back(j);
            }
            // target 被重新赋值：之后的 tmp 引用不能再替换
            // （RETURN/PARAM 的 dest 是读取而非赋值，须排除）
            if (inst.op != IROp::RETURN && inst.op != IROp::PARAM) {
              if (inst.dest.isLocalVar() && inst.dest.name == target) {
                break;
              }
              if (inst.dest.isGlobalVar() && inst.dest.name == target) {
                break;
              }
              // tmp 被重新定义（不再是 target 的值）
              if (inst.dest.isLocalVar() && inst.dest.name == tmp) {
                break;
              }
            }
          }
          // 仅当 tmp 的所有使用都位于可替换窗口内（含 ASSIGN 对自身的一处使用）
          // 才可合并；窗口外仍引用 tmp 则合并会产生悬垂引用
          if (ok && mergeUseCount[tmp] == static_cast<int>(refsToReplace.size() + 1)) {
            IRInst merged = ir[i];
            merged.dest = ir[i + 1].dest;
            optimized.push_back(merged);
            // 将收集到的 tmp 引用替换为 target
            for (const std::size_t idx : refsToReplace) {
              if (ir[idx].src1.isLocalVar() && ir[idx].src1.name == tmp) {
                ir[idx].src1 = Operand::localVar(target);
              }
              if (ir[idx].src2.isLocalVar() && ir[idx].src2.name == tmp) {
                ir[idx].src2 = Operand::localVar(target);
              }
              if ((ir[idx].op == IROp::RETURN || ir[idx].op == IROp::PARAM) &&
                  ir[idx].dest.isLocalVar() && ir[idx].dest.name == tmp) {
                ir[idx].dest = Operand::localVar(target);
              }
            }
            i += 2;
            changed = true;
            mergedHere = true;
          }
        }
        if (!mergedHere) {
          optimized.push_back(ir[i]);
          i += 1;
        }
      }
      if (ir.size() != optimized.size()) {
        ir = std::move(optimized);
      }
    }

    // Pass 1.5: 公共子表达式消除（基本块内）
    // 在无控制流的直线代码段内，若 (op, src1, src2) 已计算过且操作数未在块内被重新定义，
    // 则复用之前的结果。用户变量可被重新赋值，须跟踪重定义并使相关条目失效。
    {
      std::unordered_map<std::string, std::string> rename; // 原名 → 复用名（仅临时变量）
      std::unordered_map<std::string, std::string> valueMap; // (op,src1,src2) → 结果变量
      std::unordered_map<std::string, std::vector<std::string>> varKeys; // 变量 → 引用它的 key
      std::vector<IRInst> optimized;
      auto invalidateVar = [&](const std::string& name) {
        auto it = varKeys.find(name);
        if (it == varKeys.end()) {
          return;
        }
        for (const auto& key : it->second) {
          valueMap.erase(key);
        }
        varKeys.erase(it);
      };
      for (const auto& inst : ir) {
        // 控制流/调用/内存边界：重置所有映射（保守处理）
        if (inst.op == IROp::LABEL || inst.op == IROp::BRANCH || inst.op == IROp::BEQZ ||
            inst.op == IROp::BNEZ || inst.op == IROp::CALL || inst.op == IROp::LOAD ||
            inst.op == IROp::STORE || inst.op == IROp::RETURN || inst.op == IROp::PARAM) {
          valueMap.clear();
          varKeys.clear();
        }
        IRInst cur = inst;
        // 源操作数重命名
        if (cur.src1.isLocalVar()) {
          auto it = rename.find(cur.src1.name);
          if (it != rename.end()) {
            cur.src1 = Operand::localVar(it->second);
          }
        }
        if (cur.src2.isLocalVar()) {
          auto it = rename.find(cur.src2.name);
          if (it != rename.end()) {
            cur.src2 = Operand::localVar(it->second);
          }
        }
        // 本指令定义变量：使引用该变量的 CSE 条目失效（其值已变化）
        if (cur.dest.isLocalVar()) {
          invalidateVar(cur.dest.name);
        }
        if (isCombinableOp(cur.op) && cur.dest.isLocalVar() && isTempName(cur.dest.name) &&
            cur.op != IROp::NOT && cur.src1.type != OperandType::NONE &&
            cur.src2.type != OperandType::NONE && (cur.src1.isLocalVar() || cur.src1.isImm()) &&
            (cur.src2.isLocalVar() || cur.src2.isImm())) {
          const std::string key =
              std::string(irOpToString(cur.op)) + "|" +
              (cur.src1.isImm() ? std::to_string(cur.src1.immVal) : cur.src1.name) + "|" +
              (cur.src2.isImm() ? std::to_string(cur.src2.immVal) : cur.src2.name);
          auto it = valueMap.find(key);
          if (it != valueMap.end()) {
            rename[cur.dest.name] = it->second;
            changed = true;
            continue; // 冗余指令不再发射
          }
          valueMap[key] = cur.dest.name;
          if (cur.src1.isLocalVar()) {
            varKeys[cur.src1.name].push_back(key);
          }
          if (cur.src2.isLocalVar()) {
            varKeys[cur.src2.name].push_back(key);
          }
        }
        optimized.push_back(cur);
      }
      if (ir.size() != optimized.size()) {
        ir = std::move(optimized);
      }
    }

    // Pass 2: 死代码消除
    {
      std::unordered_map<std::string, int> useCount;
      countUses(ir, useCount);

      std::vector<IRInst> optimized;
      for (const auto& inst : ir) {
        if (isCombinableOp(inst.op) && inst.dest.isLocalVar() && useCount[inst.dest.name] == 0) {
          changed = true;
          continue;
        }
        if (inst.op == IROp::ASSIGN && inst.dest.isLocalVar() && useCount[inst.dest.name] == 0) {
          changed = true;
          continue;
        }
        optimized.push_back(inst);
      }
      if (ir.size() != optimized.size()) {
        ir = std::move(optimized);
      }
    }

    // Pass 3: 循环反转（loop inversion）
    // 将 "LABEL c; <cond>; BEQZ t,e; LABEL b; <body>; BRANCH c; LABEL e" 转换为
    // "BRANCH c; LABEL b; <body>; LABEL c; <cond>; BNEZ t,b; LABEL e"
    // 消除循环体末尾的 j 跳转，每次迭代少执行 1 条指令
    {
      std::vector<IRInst> optimized;
      std::size_t i = 0;
      while (i < ir.size()) {
        // 查找: LABEL c
        if (ir[i].op == IROp::LABEL && i + 2 < ir.size()) {
          const std::string condLabel = ir[i].dest.name;
          std::size_t j = i + 1;
          // 条件块：连续纯指令，最后一条必须是 BEQZ t, e
          while (j < ir.size() && isPureCondInst(ir[j])) {
            ++j;
          }
          if (j < ir.size() && ir[j].op == IROp::BEQZ && j + 1 < ir.size()) {
            const std::string endLabel = ir[j].dest.name;
            // BEQZ 之后必须是 LABEL b（循环体开始）
            if (ir[j + 1].op == IROp::LABEL) {
              const std::string bodyLabel = ir[j + 1].dest.name;
              // 向后扫描到 LABEL e，记录最后一个 BRANCH c（循环尾跳转）
              std::size_t k = j + 1;
              std::size_t lastBranchC = 0;
              bool foundEnd = false;
              for (; k < ir.size(); ++k) {
                if (ir[k].op == IROp::BRANCH && ir[k].dest.name == condLabel) {
                  lastBranchC = k;
                } else if (ir[k].op == IROp::LABEL && ir[k].dest.name == endLabel) {
                  foundEnd = true;
                  break;
                }
              }
              // 要求循环尾跳转紧邻 LABEL e（标准 while 结构）
              if (foundEnd && lastBranchC != 0 && lastBranchC + 1 == k) {
                // 常量提升：若条件块最后一条指令是 src2 为立即数的比较，
                // 将该立即数提升到循环外的临时变量，使比较变成寄存器比较，
                // 便于代码生成阶段将 slt+bnez 合并为单条条件分支 blt/bge
                if (j > i + 1) {
                  IRInst& lastCond = ir[j - 1];
                  const bool isCmpOp = lastCond.op == IROp::LT || lastCond.op == IROp::LE ||
                                       lastCond.op == IROp::GT || lastCond.op == IROp::GE ||
                                       lastCond.op == IROp::EQ || lastCond.op == IROp::NE;
                  if (isCmpOp && lastCond.src2.isImm()) {
                    // 使用 'k' 前缀标记循环常量提升变量，代码生成阶段优先分配寄存器
                    const std::string constVar = "k" + std::to_string(tempCounter++);
                    optimized.push_back(IRInst(IROp::ASSIGN, Operand::localVar(constVar),
                                               Operand::imm(lastCond.src2.immVal),
                                               Operand::none()));
                    lastCond.src2 = Operand::localVar(constVar);
                  }
                }
                // 变换:
                // 1. 在 LABEL b 之前插入 BRANCH c（首次进入先测试）
                // 2. BEQZ t,e -> BNEZ t,b（条件满足跳回循环体）
                // 3. 循环尾 BRANCH c -> LABEL c（测试点移到循环体末尾）
                // 4. 条件块移动到测试点（循环体末尾）重新执行
                optimized.push_back(IRInst(IROp::BRANCH, Operand::label(condLabel), Operand::none(),
                                           Operand::none()));
                // 循环体（LABEL b 到循环尾跳转之前）
                for (std::size_t m = j + 1; m < lastBranchC; ++m) {
                  optimized.push_back(ir[m]);
                }
                // 循环尾: BRANCH c -> LABEL c（测试点）
                IRInst testLabel = ir[lastBranchC];
                testLabel.op = IROp::LABEL;
                optimized.push_back(testLabel);
                // 条件块移动到测试点
                for (std::size_t m = i + 1; m < j; ++m) {
                  optimized.push_back(ir[m]);
                }
                // BEQZ t,e -> BNEZ t,b
                IRInst bnez = ir[j];
                bnez.op = IROp::BNEZ;
                bnez.dest = Operand::label(bodyLabel);
                optimized.push_back(bnez);
                i = lastBranchC + 1;
                changed = true;
                continue;
              }
            }
          }
        }
        optimized.push_back(ir[i]);
        ++i;
      }
      // 循环反转前后指令数量不变（LABEL/BRANCH 互换），必须无条件更新
      if (changed) {
        ir = std::move(optimized);
      } else {
        ir = optimized;
      }
    }

    // Pass 4: 循环不变代码外提（LICM）
    // 针对反转后的循环结构：
    //   BRANCH Lc; LABEL Lb; <body>; LABEL Lc; <cond>; BNEZ Lb
    // 将 body 中循环不变（操作数不在循环内被定义）且无副作用（不含 DIV/MOD，避免除零陷阱）的
    // 纯运算指令外提到循环入口之前。循环可能执行 0 次，但纯指令无副作用，外提安全。
    {
      std::vector<IRInst> optimized;
      std::size_t i = 0;
      bool licmChanged = false;
      while (i < ir.size()) {
        // 查找反转循环: BRANCH Lc
        if (ir[i].op == IROp::BRANCH && i + 1 < ir.size() && ir[i + 1].op == IROp::LABEL) {
          const std::string condLabel = ir[i].dest.name;
          const std::string bodyLabel = ir[i + 1].dest.name;
          // 向后扫描: LABEL condLabel 之后是 cond + BNEZ bodyLabel
          std::size_t condIdx = i + 2;
          for (; condIdx < ir.size(); ++condIdx) {
            if (ir[condIdx].op == IROp::LABEL && ir[condIdx].dest.name == condLabel) {
              break;
            }
          }
          std::size_t bnezIdx = condIdx + 1;
          for (; bnezIdx < ir.size(); ++bnezIdx) {
            if (ir[bnezIdx].op == IROp::BNEZ && ir[bnezIdx].dest.name == bodyLabel) {
              break;
            }
          }
          if (condIdx < ir.size() && bnezIdx < ir.size()) {
            // 收集循环范围内被定义的局部变量（body + cond）
            std::unordered_map<std::string, bool> definedInLoop;
            // 循环内被写入的全局变量：读取它们的表达式不能视为循环不变
            std::unordered_map<std::string, bool> storedGlobals;
            // 循环内每个局部变量被定义的次数：外提指令的目标在循环内只能定义
            // 一次（即本次）。链式表达式（如 %t15 = c; %t15 = %t15 % 15; ...）复用
            // 同一临时名，若把链首的常量赋值外提，循环体后续再定义 %t15 会覆盖
            // 外提值，第二次迭代起读到陈旧结果，破坏语义。
            std::unordered_map<std::string, int> defCount;
            for (std::size_t k = i + 2; k < bnezIdx; ++k) {
              if (ir[k].dest.isLocalVar()) {
                definedInLoop[ir[k].dest.name] = true;
                ++defCount[ir[k].dest.name];
              } else if (ir[k].dest.isGlobalVar()) {
                storedGlobals[ir[k].dest.name] = true;
              }
            }
            // 操作数必须在循环内不被定义
            auto operandInvariant = [&](const Operand& op) {
              if (op.isImm()) {
                return true;
              }
              if (op.isGlobalVar()) {
                // 全局变量可被循环内的 STORE/ASSIGN 修改，只有未被写入时才视为不变
                return storedGlobals.count(op.name) == 0;
              }
              if (op.isLocalVar()) {
                return definedInLoop.count(op.name) == 0;
              }
              return false;
            };
            // 待外提指令与原序保留指令
            // 注意范围从 i+1 开始：LABEL Lb 属于循环体，必须保留在原位（hoisted 之后），
            // 否则 BNEZ Lb 会跳回已外提的指令，导致不变计算被重复执行
            std::vector<IRInst> hoisted;
            std::vector<IRInst> kept;
            for (std::size_t k = i + 1; k < condIdx; ++k) {
              const auto& inst = ir[k];
              const bool pure =
                  inst.op == IROp::ADD || inst.op == IROp::SUB || inst.op == IROp::MUL ||
                  inst.op == IROp::NOT || inst.op == IROp::LT || inst.op == IROp::GT ||
                  inst.op == IROp::LE || inst.op == IROp::GE || inst.op == IROp::EQ ||
                  inst.op == IROp::NE ||
                  (inst.op == IROp::ASSIGN && inst.dest.isLocalVar() && isTempName(inst.dest.name));
              if (pure && inst.dest.isLocalVar() && isTempName(inst.dest.name) &&
                  defCount[inst.dest.name] == 1 && operandInvariant(inst.src1) &&
                  operandInvariant(inst.src2)) {
                hoisted.push_back(inst);
                licmChanged = true;
              } else {
                kept.push_back(inst);
              }
            }
            if (!hoisted.empty()) {
              // 外提指令必须放在首跳 BRANCH Lc 之前，否则首跳会跳过它们，
              // 导致外提计算成为不可达死代码（循环体永远用不到外提结果）
              for (auto& h : hoisted) {
                optimized.push_back(h);
              }
              optimized.push_back(ir[i]); // BRANCH Lc
              for (auto& ke : kept) {
                optimized.push_back(ke);
              }
              i = condIdx; // 已处理 [i+2, condIdx)，跳到 LABEL Lc
              continue;
            }
          }
        }
        optimized.push_back(ir[i]);
        ++i;
      }
      if (licmChanged) {
        ir = std::move(optimized);
        changed = true;
      }
    }
  }

  // Pass 5: 单次使用临时变量复用（single-use temp recycling）。
  // 必须在 pass 0-4 全部收敛后运行一次：长表达式链的每个中间结果都是单次使用
  // 的临时变量，变量总数 O(链长)，超出寄存器池后全部落栈，每轮循环栈读写往返。
  // 该 Pass 在线性扫描中维护"已消费可复用"的名字池，让这些临时复用一个名字，
  // 使循环体变量总数降到寄存器池以内，彻底消除栈往返。注意该 Pass 不能参与
  // 上面的迭代循环：改名后若让 pass 0-4 重新分析，会把条件块内指令误判为
  // 循环不变量外提（LICM），破坏语义。
  {
    std::unordered_map<std::string, int> useCount;
    for (const auto& inst : ir) {
      if (inst.src1.isLocalVar()) {
        ++useCount[inst.src1.name];
      }
      if (inst.src2.isLocalVar()) {
        ++useCount[inst.src2.name];
      }
      if ((inst.op == IROp::RETURN || inst.op == IROp::PARAM) && inst.dest.isLocalVar()) {
        ++useCount[inst.dest.name];
      }
    }

    // 仅复用编译器生成的临时（newTemp 命名：t 后跟纯数字），用户变量与循环提升
    // 变量（k 前缀）保持原名。
    const auto isCompilerTemp = [](const std::string& name) {
      if (name.empty() || name[0] != 't') {
        return false;
      }
      for (std::size_t k = 1; k < name.size(); ++k) {
        if (!std::isdigit(static_cast<unsigned char>(name[k]))) {
          return false;
        }
      }
      return true;
    };

    std::unordered_map<std::string, std::string> renameMap; // old -> new
    std::unordered_map<std::string, int> activeCount;       // new 名字的活跃引用数
    std::vector<std::string> freePool;                      // activeCount==0 的可复用名字
    // 当前基本块内被定义的名字。只有块内定义的名字才能进入 freePool 复用：
    // 循环不变提升（LICM 外提）的临时定义在循环前的基本块，却在循环体内被消费；
    // 若将其名字复用给循环体内新临时，会在循环内重新定义该名字，覆盖外提值，
    // 第二次迭代起读到陈旧结果（fuzz FAIL_17 根因）。基本块以 LABEL 为界，
    // 跨块复用无法保证跨回边安全，故在 LABEL 处清空 freePool 与 blockDefined。
    std::unordered_set<std::string> blockDefined;

    const auto resolveOperand = [&](Operand& op) {
      if (!op.isLocalVar()) {
        return;
      }
      auto it = renameMap.find(op.name);
      if (it != renameMap.end()) {
        op = Operand::localVar(it->second);
      }
    };

    for (auto& inst : ir) {
      // 基本块边界：前块释放的名字不可复用（可能被回边重新进入，且可能是
      // 从循环前提升进块的 live-in 值）
      if (inst.op == IROp::LABEL) {
        freePool.clear();
        blockDefined.clear();
      }
      // 记录本指令消费的旧名字（resolve 前快照，release 在 resolve 之后用）
      const std::string oldSrc1 = inst.src1.isLocalVar() ? inst.src1.name : std::string();
      const std::string oldSrc2 = inst.src2.isLocalVar() ? inst.src2.name : std::string();
      const std::string oldDest =
          ((inst.op == IROp::RETURN || inst.op == IROp::PARAM) && inst.dest.isLocalVar())
              ? inst.dest.name
              : std::string();

      // 先改名（renameMap 只读，不擦除），再释放被消费的单次使用临时
      resolveOperand(inst.src1);
      resolveOperand(inst.src2);
      if (inst.op == IROp::RETURN || inst.op == IROp::PARAM) {
        resolveOperand(inst.dest);
      }

      const auto releaseIfSingleUse = [&](const std::string& oldName) {
        if (oldName.empty()) {
          return;
        }
        auto cit = useCount.find(oldName);
        if (cit == useCount.end() || cit->second != 1) {
          return;
        }
        auto rit = renameMap.find(oldName);
        if (rit != renameMap.end()) {
          const std::string newName = rit->second;
          renameMap.erase(rit);
          // 只有块内定义的名字才可回收：跨块（如循环前提升）的名字即使线性扫描
          // 中已无引用，跨迭代仍可能被回边重新读取，复用其名字会破坏语义
          if (--activeCount[newName] == 0 && blockDefined.count(newName) > 0) {
            freePool.push_back(newName);
          }
        }
      };
      releaseIfSingleUse(oldSrc1);
      releaseIfSingleUse(oldSrc2);
      releaseIfSingleUse(oldDest);

      // 为新定义的临时分配名字：优先复用池中已释放且不活跃的名字
      // 允许 dest 与 src 同名：RISC-V 指令原子读操作数再写 dest（read-before-write），
      // 且池中名字都来自已消费的临时（值已无引用），覆盖安全。
      // 注意：短路求值的 result 临时会被写两次（true/false 两路径），第二次定义
      // 必须沿用第一次的映射，否则 false 路径会写回未定义的名字。
      if (inst.dest.isLocalVar() && isCompilerTemp(inst.dest.name)) {
        auto existing = renameMap.find(inst.dest.name);
        if (existing != renameMap.end()) {
          if (inst.dest.name != existing->second) {
            inst.dest = Operand::localVar(existing->second);
          }
        } else {
          std::string newName;
          if (!freePool.empty()) {
            newName = freePool.back();
            freePool.pop_back();
          } else {
            newName = inst.dest.name;
          }
          renameMap[inst.dest.name] = newName;
          ++activeCount[newName];
          blockDefined.insert(newName);
          if (newName != inst.dest.name) {
            inst.dest = Operand::localVar(newName);
          }
        }
      }
    }
  }
}

} // namespace toycc