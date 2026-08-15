#include "ir_generator.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <functional>
#include <iostream>
#include <optional>
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

std::vector<IRInst> IRGenerator::generate(CompUnit& compUnit, IRStage stage) {
  ir.clear();
  tempCounter = 0;
  labelCounter = 0;

  for (auto& node : compUnit.globalDecls) {
    genGlobalDecl(node.get());
  }

  if (stage != IRStage::RAW) {
    inlinePass();
  }
  if (stage == IRStage::OPTIMIZED) {
    optimizePass();
    // 首轮传播会把 `int n = 1000000; helper(n)` 的 PARAM 化成立即数；再做
    // 一轮受预算内联，可让多个常量调用点获得专门化后的循环摘要机会。
    inlinePass();
    optimizePass();
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
    const bool sameValue =
        lhsVal.type == rhsVal.type && ((lhsVal.isLocalVar() && lhsVal.name == rhsVal.name) ||
                                       (lhsVal.isGlobalVar() && lhsVal.name == rhsVal.name));
    if (sameValue) {
      switch (binaryExpr->op) {
      case BinOp::SUB:
      case BinOp::MOD:
      case BinOp::LT:
      case BinOp::GT:
      case BinOp::NE:
        return Operand::imm(0);
      case BinOp::DIV:
      case BinOp::LE:
      case BinOp::GE:
      case BinOp::EQ:
        // ToyC 性能用例保证没有除零等未定义行为，因此执行到 x/x 时 x != 0。
        return Operand::imm(1);
      default:
        break;
      }
    }
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
      // x * -1 = -x, -1 * x = -x
      if (rhsVal.isImm() && rhsVal.immVal == -1) {
        Operand result = Operand::localVar(newTemp());
        emit(IROp::SUB, result, Operand::imm(0), lhsVal);
        return result;
      }
      if (lhsVal.isImm() && lhsVal.immVal == -1) {
        Operand result = Operand::localVar(newTemp());
        emit(IROp::SUB, result, Operand::imm(0), rhsVal);
        return result;
      }
      break;
    case BinOp::DIV:
      // x / 1 = x
      if (rhsVal.isImm() && rhsVal.immVal == 1)
        return lhsVal;
      // 0 / x = 0（执行到此处时由 ToyC 的无未定义行为约束知 x != 0）
      if (lhsVal.isImm() && lhsVal.immVal == 0)
        return Operand::imm(0);
      break;
    case BinOp::MOD:
      // x % ±1 = 0；0 % x = 0（后者同样依赖除数非零约束）
      if ((rhsVal.isImm() && (rhsVal.immVal == 1 || rhsVal.immVal == -1)) ||
          (lhsVal.isImm() && lhsVal.immVal == 0))
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
    // 先求值全部实参，再按序发射 PARAM。
    // 若边求值边发射 PARAM：外层实参先载入 a0-a7，内层嵌套调用的
    // PARAM 会覆盖它们（caller-saved），导致外层实参丢失（正确性 bug）。
    std::vector<Operand> argVals;
    argVals.reserve(callExpr->args.size());
    for (auto& arg : callExpr->args) {
      argVals.push_back(genExpr(arg.get()));
    }
    for (size_t i = 0; i < argVals.size(); ++i) {
      emit(IROp::PARAM, std::move(argVals[i]), Operand::imm(static_cast<int>(i)));
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

// ============================================================
// 函数内联 pass（在 optimizePass 之前运行）
// ============================================================

void IRGenerator::inlinePass() {
  constexpr std::size_t kInlineLimit = 24;
  // 循环中的调用会按迭代次数重复支付调用、参数搬运和被调函数帧开销。
  // 对热调用点使用更高但仍有上限的预算；循环外仍保持保守阈值，避免无谓膨胀。
  constexpr std::size_t kBaseLoopInlineLimit = 96;
  constexpr std::size_t kLoopInlineLimit = 160;
  // 单一调用点的非递归 helper 内联后不会复制代码，却能把 main 中的常量
  // 实参送进其循环条件，继而触发循环摘要和其它跨调用优化。
  constexpr std::size_t kSingleCallInlineLimit = 2048;
  constexpr std::size_t kConstantCallInlineLimit = 2048;

  // 多轮迭代：内联可能引入新的可内联调用（如 f(g(x))）。
  // 每轮重新收集函数区间，因为前一轮的内联替换会使旧下标失效。
  bool any = true;
  for (int rounds = 0; any && rounds < 64; ++rounds) {
    any = false;

    // 函数定义区间
    struct FuncInfo {
      std::size_t begin = 0;
      std::size_t end = 0;
      int paramCount = 0;
      std::vector<std::string> paramVars; // 按声明顺序的 IR 参数名
    };
    std::unordered_map<std::string, FuncInfo> funcs;
    for (std::size_t i = 0; i < ir.size(); ++i) {
      if (ir[i].op == IROp::FUNC_BEGIN && ir[i].dest.isFunc()) {
        FuncInfo info;
        info.begin = i;
        info.end = i + 1;
        while (info.end < ir.size() && ir[info.end].op != IROp::FUNC_END)
          ++info.end;
        for (std::size_t k = info.begin + 1; k < info.end; ++k) {
          if (ir[k].op == IROp::LOCAL_VAR_DECL && ir[k].src1.isParam()) {
            ++info.paramCount;
            info.paramVars.push_back(ir[k].dest.name);
          }
        }
        funcs[ir[i].dest.name] = std::move(info);
      }
    }
    if (funcs.empty())
      break;

    std::unordered_map<std::string, std::size_t> callCounts;
    for (const IRInst& inst : ir) {
      if (inst.op == IROp::CALL && inst.src1.isFunc() && funcs.count(inst.src1.name) != 0) {
        ++callCounts[inst.src1.name];
      }
    }

    // 由回边构造自然循环的线性区间。while 在反转前后分别以 BRANCH/BNEZ
    // 跳向前方 LABEL，调用点落在 [target, backedge] 内即为热调用。
    std::unordered_map<std::string, std::size_t> labelPositions;
    for (std::size_t i = 0; i < ir.size(); ++i) {
      if (ir[i].op == IROp::LABEL && ir[i].dest.isLabel()) {
        labelPositions[ir[i].dest.name] = i;
      }
    }
    std::vector<std::pair<std::size_t, std::size_t>> loopRanges;
    for (std::size_t i = 0; i < ir.size(); ++i) {
      const auto& inst = ir[i];
      if ((inst.op != IROp::BRANCH && inst.op != IROp::BEQZ && inst.op != IROp::BNEZ) ||
          !inst.dest.isLabel()) {
        continue;
      }
      auto target = labelPositions.find(inst.dest.name);
      if (target != labelPositions.end() && target->second < i) {
        loopRanges.emplace_back(target->second, i);
      }
    }
    const auto isLoopCall = [&](std::size_t index) {
      return std::any_of(loopRanges.begin(), loopRanges.end(), [&](const auto& range) {
        return index >= range.first && index <= range.second;
      });
    };

    // 调用图可达性：reach[f] = f 经定义函数（传递）调用的函数集合
    std::unordered_map<std::string, std::unordered_set<std::string>> reach;
    for (const auto& [fname, finfo] : funcs) {
      auto& r = reach[fname];
      std::vector<std::string> stack{fname};
      while (!stack.empty()) {
        std::string cur = std::move(stack.back());
        stack.pop_back();
        auto fit = funcs.find(cur);
        if (fit == funcs.end())
          continue;
        for (std::size_t k = fit->second.begin + 1; k < fit->second.end; ++k) {
          if (ir[k].op == IROp::CALL && ir[k].src1.isFunc() && r.insert(ir[k].src1.name).second) {
            stack.push_back(ir[k].src1.name);
          }
        }
      }
    }

    // 参数是否在函数体中被写（被写则实参必须物化到临时变量）
    std::unordered_map<std::string, std::unordered_set<std::string>> writtenParams;
    for (const auto& [fname, finfo] : funcs) {
      for (std::size_t k = finfo.begin + 1; k < finfo.end; ++k) {
        // 参数声明建立初始绑定，不是函数体对参数的写入。把它误计为写入会让
        // 每个内联参数都先物化到新临时，热循环中平白增加一次 mv。
        const bool isParameterDecl = ir[k].op == IROp::LOCAL_VAR_DECL && ir[k].src1.isParam();
        const bool destIsUse = ir[k].op == IROp::RETURN || ir[k].op == IROp::PARAM;
        if (!isParameterDecl && !destIsUse && ir[k].dest.isLocalVar()) {
          writtenParams[fname].insert(ir[k].dest.name);
        }
      }
    }

    // 函数体大小：排除参数声明、无初始化局部声明、标签、FUNC_END
    auto bodySize = [&](const FuncInfo& f) {
      std::size_t n = 0;
      for (std::size_t k = f.begin + 1; k < f.end; ++k) {
        const auto& inst = ir[k];
        if (inst.op == IROp::LABEL || inst.op == IROp::FUNC_END)
          continue;
        if (inst.op == IROp::LOCAL_VAR_DECL && (inst.src1.isParam() || inst.src1.isNone()))
          continue;
        ++n;
      }
      return n;
    };

    struct Candidate {
      std::size_t callIndex;    // CALL 指令下标
      std::size_t paramStart;   // 该调用点第一个 PARAM 的下标
      std::vector<IRInst> body; // 变换后的内联体
    };
    std::vector<Candidate> cands;

    std::string curFunc;
    for (std::size_t ci = 0; ci < ir.size(); ++ci) {
      if (ir[ci].op == IROp::FUNC_BEGIN) {
        curFunc = ir[ci].dest.name;
        continue;
      }
      if (ir[ci].op == IROp::FUNC_END) {
        curFunc.clear();
        continue;
      }
      if (ir[ci].op != IROp::CALL || !ir[ci].src1.isFunc())
        continue;

      const std::string& callee = ir[ci].src1.name;
      auto fit = funcs.find(callee);
      if (fit == funcs.end())
        continue; // 未定义（库函数）不内联
      const FuncInfo& f = fit->second;
      // 递归（含相互递归）不内联；经 callee 可达 caller（会产生调用环）不内联
      if (reach[callee].count(callee) || reach[callee].count(curFunc))
        continue;
      const bool hotCall = isLoopCall(ci);
      const bool singleCall = callCounts[callee] == 1;
      std::size_t argumentStart = ci;
      while (argumentStart > 0 && ir[argumentStart - 1].op == IROp::PARAM) {
        --argumentStart;
      }
      bool constantCall = ci - argumentStart == static_cast<std::size_t>(f.paramCount);
      for (std::size_t k = argumentStart; k < ci && constantCall; ++k) {
        constantCall = ir[k].dest.isImm();
      }
      std::size_t inlineLimit = singleCall ? kSingleCallInlineLimit : kInlineLimit;
      if (constantCall) {
        inlineLimit = std::max(inlineLimit, kConstantCallInlineLimit);
      }
      if (hotCall) {
        inlineLimit = std::max(inlineLimit, kLoopInlineLimit);
      }
      const std::size_t calleeBodySize = bodySize(f);
      bool hasUnconditionalBackedge = false;
      for (std::size_t k = f.begin + 1; k < f.end; ++k) {
        if (ir[k].op != IROp::BRANCH || !ir[k].dest.isLabel()) {
          continue;
        }
        const auto target = labelPositions.find(ir[k].dest.name);
        if (target != labelPositions.end() && target->second < k) {
          hasUnconditionalBackedge = true;
          break;
        }
      }
      const bool expandedInlining =
          calleeBodySize > kInlineLimit && (singleCall || constantCall || hotCall);
      if (expandedInlining && hasUnconditionalBackedge) {
        continue;
      }
      const auto callResultIsUsed = [&]() {
        if (!ir[ci].dest.isLocalVar()) {
          return true;
        }
        std::string value = ir[ci].dest.name;
        for (std::size_t k = ci + 1; k < ir.size(); ++k) {
          const IRInst& later = ir[k];
          if (later.op == IROp::FUNC_END) {
            break;
          }
          const bool usedAsSource = (later.src1.isLocalVar() && later.src1.name == value) ||
                                    (later.src2.isLocalVar() && later.src2.name == value) ||
                                    ((later.op == IROp::RETURN || later.op == IROp::PARAM) &&
                                     later.dest.isLocalVar() && later.dest.name == value);
          if (usedAsSource) {
            if (later.op == IROp::ASSIGN && later.dest.isLocalVar() && later.src1.isLocalVar() &&
                later.src1.name == value) {
              value = later.dest.name;
              continue;
            }
            return true;
          }
          const bool definesValue = later.dest.isLocalVar() && later.dest.name == value &&
                                    later.op != IROp::RETURN && later.op != IROp::PARAM;
          if (definesValue) {
            break;
          }
        }
        return false;
      };
      if (expandedInlining && !callResultIsUsed()) {
        continue;
      }
      if (calleeBodySize > inlineLimit)
        continue;

      // The wider hot-call budget is for values that contribute to observable
      // loop state.  If the result is merely copied into a local that is never
      // read, keep the call intact: the later purity/DCE pass can remove the
      // entire call, whereas inlining a large helper would hide that opportunity
      // inside a much larger loop body.
      if (hotCall && calleeBodySize > kBaseLoopInlineLimit && ir[ci].dest.isLocalVar() &&
          ci + 1 < ir.size()) {
        const IRInst& copy = ir[ci + 1];
        if (copy.op == IROp::ASSIGN && copy.dest.isLocalVar() && copy.src1.isLocalVar() &&
            copy.src1.name == ir[ci].dest.name) {
          bool copiedValueUsed = false;
          for (std::size_t k = ci + 2; k < ir.size(); ++k) {
            const IRInst& later = ir[k];
            if (later.op == IROp::FUNC_END) {
              break;
            }
            copiedValueUsed = (later.src1.isLocalVar() && later.src1.name == copy.dest.name) ||
                              (later.src2.isLocalVar() && later.src2.name == copy.dest.name) ||
                              ((later.op == IROp::RETURN || later.op == IROp::PARAM) &&
                               later.dest.isLocalVar() && later.dest.name == copy.dest.name);
            if (copiedValueUsed) {
              break;
            }
            if (later.dest.isLocalVar() && later.dest.name == copy.dest.name &&
                later.op != IROp::RETURN && later.op != IROp::PARAM) {
              break;
            }
          }
          if (!copiedValueUsed) {
            continue;
          }
        }
      }

      // 收集紧邻 CALL 之前的 PARAM 指令（应连续，索引 0..n-1）
      std::vector<Operand> argVals;
      std::size_t p = ci;
      while (p > 0 && ir[p - 1].op == IROp::PARAM)
        --p;
      bool ok = true;
      for (std::size_t k = p; k < ci; ++k) {
        if (ir[k].src1.isImm() && ir[k].src1.immVal == static_cast<int>(argVals.size())) {
          argVals.push_back(ir[k].dest);
        } else {
          ok = false;
          break;
        }
      }
      if (!ok || argVals.size() != static_cast<std::size_t>(f.paramCount))
        continue;

      // 变换函数体：参数映射、标签重命名、RETURN 改写
      const std::unordered_set<std::string>& wp = writtenParams[callee];
      std::unordered_map<std::string, Operand> paramMap;
      std::vector<IRInst> body;
      for (std::size_t i = 0; i < static_cast<std::size_t>(f.paramCount); ++i) {
        const std::string& pv = f.paramVars[i];
        if (wp.count(pv)) {
          // 形参被写：实参物化到新鲜临时变量。
          // 使用独立 p 前缀，避免与被调函数 t%d 临时命名空间混淆。
          Operand fresh = Operand::localVar("p" + std::to_string(tempCounter++));
          paramMap[pv] = fresh;
          body.emplace_back(IROp::ASSIGN, fresh, argVals[i], Operand::none());
        } else {
          // 形参只读：直接复用实参操作数（立即数/变量均可）
          paramMap[pv] = argVals[i];
        }
      }

      auto mapOp = [&](const Operand& op) -> Operand {
        if (op.isLocalVar() && paramMap.count(op.name))
          return paramMap[op.name];
        return op;
      };

      // 临时变量重命名：t%d 由 tempCounter 生成、跨函数重名（generate() 每函数
      // 重置计数），内联复制进调用者后必须换成唯一名，否则复制传播/合并会
      // 把不同函数同名临时混为一谈（use 计数错误 → 优化失效）
      std::unordered_map<std::string, std::string> tempRename;
      auto mapTmp = [&](const Operand& op) -> Operand {
        if (op.isLocalVar() && isTempName(op.name)) {
          auto it = tempRename.find(op.name);
          if (it == tempRename.end()) {
            it = tempRename.emplace(op.name, newTemp()).first;
          }
          return Operand::localVar(it->second);
        }
        return op;
      };

      std::unordered_map<std::string, std::string> labelMap;
      auto mapLabel = [&](const std::string& l) {
        auto it = labelMap.find(l);
        if (it == labelMap.end())
          it = labelMap.emplace(l, newLabel()).first;
        return it->second;
      };

      const Operand retTemp = ir[ci].dest.isNone() ? Operand::none() : Operand::localVar(newTemp());
      const std::string endLabel = newLabel();
      bool hasReturnBranch = false; // 已有中间返回跳转 → 末尾默认返回不可达，可跳过
      for (std::size_t k = f.begin + 1; k < f.end; ++k) {
        const IRInst& inst = ir[k];
        if (inst.op == IROp::FUNC_END)
          continue;
        if (inst.op == IROp::LOCAL_VAR_DECL && inst.src1.isParam())
          continue; // 参数声明已替换
        if (inst.op == IROp::RETURN) {
          const bool isLast = (k + 1 >= f.end);
          if (isLast && hasReturnBranch)
            continue; // 不可达（前有 return 跳转）
          if (!retTemp.isNone()) {
            Operand rv = inst.dest.isNone() ? Operand::imm(0) : mapOp(mapTmp(inst.dest));
            body.emplace_back(IROp::ASSIGN, retTemp, std::move(rv), Operand::none());
          }
          if (!isLast) {
            body.emplace_back(IROp::BRANCH, Operand::label(endLabel), Operand::none(),
                              Operand::none());
            hasReturnBranch = true;
          }
          continue;
        }
        // 必须先重命名被调函数自身的 t%d，再替换形参。若顺序相反，调用者
        // 作为实参传入的 t%d 会被误当成被调函数临时并改成没有定义的新名字。
        IRInst copy(inst.op, mapOp(mapTmp(inst.dest)), mapOp(mapTmp(inst.src1)),
                    mapOp(mapTmp(inst.src2)));
        if (copy.op == IROp::LABEL && copy.dest.isLabel()) {
          copy.dest = Operand::label(mapLabel(copy.dest.name));
        }
        if ((copy.op == IROp::BRANCH || copy.op == IROp::BEQZ || copy.op == IROp::BNEZ) &&
            copy.dest.isLabel()) {
          copy.dest = Operand::label(mapLabel(copy.dest.name));
        }
        body.push_back(std::move(copy));
      }
      // 返回汇聚点 + 结果回传
      body.emplace_back(IROp::LABEL, Operand::label(endLabel), Operand::none(), Operand::none());
      if (!ir[ci].dest.isNone()) {
        body.emplace_back(IROp::ASSIGN, ir[ci].dest, retTemp, Operand::none());
      }
      // 清理紧邻目标标签的跳转（j L; L: 是无操作，会打断后续循环分析）
      for (std::size_t k = 0; k + 1 < body.size();) {
        if (body[k].op == IROp::BRANCH && body[k].dest.isLabel() && body[k + 1].op == IROp::LABEL &&
            body[k + 1].dest.name == body[k].dest.name) {
          body.erase(body.begin() + static_cast<std::ptrdiff_t>(k));
        } else {
          ++k;
        }
      }
      // 移除无引用的标签（内联后多余的 endLabel 会阻断块内复制传播/合并）
      std::unordered_set<std::string> usedLabels;
      for (const auto& inst : body) {
        if ((inst.op == IROp::BRANCH || inst.op == IROp::BEQZ || inst.op == IROp::BNEZ) &&
            inst.dest.isLabel()) {
          usedLabels.insert(inst.dest.name);
        }
      }
      for (std::size_t k = 0; k < body.size();) {
        if (body[k].op == IROp::LABEL && !usedLabels.count(body[k].dest.name)) {
          body.erase(body.begin() + static_cast<std::ptrdiff_t>(k));
        } else {
          ++k;
        }
      }
      cands.push_back({ci, p, std::move(body)});
    }

    // 倒序应用替换，保证未处理调用点下标有效
    std::sort(cands.begin(), cands.end(),
              [](const Candidate& a, const Candidate& b) { return a.callIndex > b.callIndex; });
    for (auto& c : cands) {
      ir.erase(ir.begin() + static_cast<std::ptrdiff_t>(c.paramStart),
               ir.begin() + static_cast<std::ptrdiff_t>(c.callIndex) + 1);
      ir.insert(ir.begin() + static_cast<std::ptrdiff_t>(c.paramStart), c.body.begin(),
                c.body.end());
      any = true;
    }
  }
}

void IRGenerator::optimizePass() {
  // 迭代优化直到 IR 不再变化（常量传播 → 折叠 → 合并 → DCE 可能级联触发）
  bool changed = true;
  int maxIterations = 8;
  while (changed && maxIterations-- > 0) {
    changed = false;

    // Pass 0: 全局常量传播（基于基本块数据流）+ 块内复制传播
    // 旧版单遍传播遇到任何 LABEL/BRANCH 就清空映射，导致循环之前的常量赋值
    // 无法传播进循环体（如矩阵乘法中循环不变系数的计算被 LICM 外提后仍不能
    // 折叠）。本 Pass 构造基本块 CFG，以格值（未定/常量/非常数）迭代收敛每个
    // 块的入口状态，使常量赋值跨控制流边界传播，随后按入口状态重写指令。
    {
      // 只读全局变量（声明后从未被写入）：任何位置都可用其初值替换
      std::unordered_set<std::string> writtenGlobals;
      std::unordered_map<std::string, int> readOnlyGlobals;
      std::unordered_set<std::string> allGlobals;
      for (const auto& inst : ir) {
        if (inst.dest.isGlobalVar()) {
          allGlobals.insert(inst.dest.name);
          // 任何以全局变量为目标的指令（除声明本身）都会修改它，
          // 使该全局不再"只读"，不能按初值替换
          if (inst.op != IROp::GLOBAL_VAR_DECL) {
            writtenGlobals.insert(inst.dest.name);
          }
        }
      }
      for (const auto& inst : ir) {
        if (inst.op == IROp::GLOBAL_VAR_DECL && inst.dest.isGlobalVar() &&
            writtenGlobals.count(inst.dest.name) == 0) {
          readOnlyGlobals[inst.dest.name] = inst.src1.immVal;
        }
      }

      // ---- 构建基本块：块内无分支，块以 LABEL/FUNC_BEGIN 开始 ----
      std::vector<std::pair<std::size_t, std::size_t>> blocks;
      {
        std::size_t b = 0;
        for (std::size_t i = 0; i < ir.size(); ++i) {
          const bool terminator = ir[i].op == IROp::BRANCH || ir[i].op == IROp::BEQZ ||
                                  ir[i].op == IROp::BNEZ || ir[i].op == IROp::RETURN ||
                                  ir[i].op == IROp::FUNC_END;
          const bool nextStarts = (i + 1 >= ir.size()) || ir[i + 1].op == IROp::LABEL ||
                                  ir[i + 1].op == IROp::FUNC_BEGIN;
          if (terminator || nextStarts) {
            blocks.push_back({b, i});
            b = i + 1;
          }
        }
        if (b < ir.size()) {
          blocks.push_back({b, ir.size() - 1});
        }
      }
      if (blocks.empty()) {
        blocks.push_back({0, ir.empty() ? 0 : ir.size() - 1});
      }
      // 标签 → 块索引
      std::unordered_map<std::string, std::size_t> labelBlock;
      for (std::size_t k = 0; k < blocks.size(); ++k) {
        if (ir[blocks[k].first].op == IROp::LABEL) {
          labelBlock[ir[blocks[k].first].dest.name] = k;
        }
      }
      // 后继
      std::vector<std::vector<std::size_t>> succs(blocks.size());
      for (std::size_t k = 0; k < blocks.size(); ++k) {
        const IRInst& last = ir[blocks[k].second];
        const bool nextIsFunc =
            blocks[k].second + 1 < ir.size() && ir[blocks[k].second + 1].op == IROp::FUNC_BEGIN;
        if (last.op == IROp::BRANCH) {
          if (last.dest.isLabel()) {
            auto it = labelBlock.find(last.dest.name);
            if (it != labelBlock.end()) {
              succs[k].push_back(it->second);
            }
          }
        } else if (last.op == IROp::BEQZ || last.op == IROp::BNEZ) {
          if (last.dest.isLabel()) {
            auto it = labelBlock.find(last.dest.name);
            if (it != labelBlock.end()) {
              succs[k].push_back(it->second);
            }
          }
          if (k + 1 < blocks.size() && !nextIsFunc) {
            succs[k].push_back(k + 1);
          }
        } else if (last.op != IROp::RETURN && last.op != IROp::FUNC_END) {
          if (k + 1 < blocks.size() && !nextIsFunc) {
            succs[k].push_back(k + 1);
          }
        }
      }
      // 前驱
      std::vector<std::vector<std::size_t>> preds(blocks.size());
      for (std::size_t k = 0; k < blocks.size(); ++k) {
        for (const std::size_t s : succs[k]) {
          preds[s].push_back(k);
        }
      }

      // ---- 格值与数据流 ----
      struct Lattice {
        bool isTop = false;
        bool isConst = false;
        int val = 0;
      };
      using State = std::unordered_map<std::string, Lattice>;
      const auto meetLattice = [](const Lattice& a, const Lattice& b) -> Lattice {
        if (!a.isTop && !a.isConst) {
          return b; // a 是 BOTTOM
        }
        if (!b.isTop && !b.isConst) {
          return a; // b 是 BOTTOM
        }
        if (a.isTop || b.isTop) {
          return Lattice{true, false, 0}; // TOP
        }
        if (a.val == b.val) {
          return a;
        }
        return Lattice{true, false, 0};
      };
      const auto statesEqual = [](const State& a, const State& b) {
        if (a.size() != b.size()) {
          return false;
        }
        for (const auto& entry : a) {
          auto it = b.find(entry.first);
          if (it == b.end()) {
            return false;
          }
          const Lattice& x = entry.second;
          const Lattice& y = it->second;
          if (x.isTop != y.isTop || x.isConst != y.isConst || x.val != y.val) {
            return false;
          }
        }
        return true;
      };

      // 二元运算常量折叠（与 Pass 0b 语义一致）
      const auto foldConst = [](IROp op, int a, int b, int& out) -> bool {
        switch (op) {
        case IROp::ADD:
          out = a + b;
          return true;
        case IROp::SUB:
          out = a - b;
          return true;
        case IROp::MUL:
          out = a * b;
          return true;
        case IROp::DIV:
          if (b == 0) {
            return false;
          }
          out = a / b;
          return true;
        case IROp::MOD:
          if (b == 0) {
            return false;
          }
          out = a % b;
          return true;
        case IROp::LT:
          out = (a < b) ? 1 : 0;
          return true;
        case IROp::GT:
          out = (a > b) ? 1 : 0;
          return true;
        case IROp::LE:
          out = (a <= b) ? 1 : 0;
          return true;
        case IROp::GE:
          out = (a >= b) ? 1 : 0;
          return true;
        case IROp::EQ:
          out = (a == b) ? 1 : 0;
          return true;
        case IROp::NE:
          out = (a != b) ? 1 : 0;
          return true;
        default:
          return false;
        }
      };

      // 单指令转移：更新状态 st（定义侧）
      const auto transferInst = [&](State& st, const IRInst& inst) {
        const auto resolveConst = [&](const Operand& op) -> std::optional<int> {
          if (op.isImm()) {
            return op.immVal;
          }
          if (op.isLocalVar()) {
            auto it = st.find(op.name);
            if (it != st.end() && it->second.isConst) {
              return it->second.val;
            }
          }
          return std::nullopt;
        };
        const auto setConst = [&](const Operand& op, int v) {
          if (op.isLocalVar() || op.isGlobalVar()) {
            st[op.name] = Lattice{false, true, v};
          }
        };
        const auto setTop = [&](const Operand& op) {
          if (op.isLocalVar() || op.isGlobalVar()) {
            st[op.name] = Lattice{true, false, 0};
          }
        };
        if (inst.op == IROp::ASSIGN || inst.op == IROp::LOCAL_VAR_DECL) {
          if (inst.op == IROp::LOCAL_VAR_DECL && inst.src1.isNone()) {
            return; // 无初始化：保持未定
          }
          if (auto v = resolveConst(inst.src1)) {
            setConst(inst.dest, *v);
          } else {
            setTop(inst.dest);
          }
          return;
        }
        if (inst.op == IROp::GLOBAL_VAR_DECL) {
          setConst(inst.dest, inst.src1.immVal);
          return;
        }
        if (isCombinableOp(inst.op)) {
          if (inst.op == IROp::NOT) {
            if (auto a = resolveConst(inst.src1)) {
              setConst(inst.dest, (*a == 0) ? 1 : 0);
            } else {
              setTop(inst.dest);
            }
            return;
          }
          const auto a = resolveConst(inst.src1);
          const auto b = resolveConst(inst.src2);
          if (a && b) {
            int folded = 0;
            if (foldConst(inst.op, *a, *b, folded)) {
              setConst(inst.dest, folded);
              return;
            }
          }
          setTop(inst.dest);
          return;
        }
        if (inst.op == IROp::CALL || inst.op == IROp::LOAD) {
          setTop(inst.dest);
          // 调用/内存读取可能改变全局变量：所有全局置 TOP。
          // 局部变量不会被调用修改（无指针/数组按引用传递），保持原状态。
          for (const auto& g : allGlobals) {
            st[g] = Lattice{true, false, 0};
          }
          return;
        }
        if (inst.op == IROp::STORE) {
          if (auto v = resolveConst(inst.src1)) {
            setConst(inst.dest, *v);
          } else {
            setTop(inst.dest);
          }
          return;
        }
        // 其他指令（LABEL/分支/RETURN/PARAM/FUNC_*）：状态不变
      };

      // ---- 迭代收敛块入口状态 ----
      std::vector<State> in(blocks.size()), out(blocks.size());
      bool converged = false;
      while (!converged) {
        converged = true;
        for (std::size_t k = 0; k < blocks.size(); ++k) {
          State newIn;
          if (!preds[k].empty()) {
            newIn = out[preds[k][0]];
            for (std::size_t p = 1; p < preds[k].size(); ++p) {
              State merged;
              // meet：逐变量求交
              std::unordered_set<std::string> names;
              for (const auto& e : newIn) {
                names.insert(e.first);
              }
              for (const auto& e : out[preds[k][p]]) {
                names.insert(e.first);
              }
              for (const auto& n : names) {
                Lattice a;
                Lattice b;
                auto ita = newIn.find(n);
                if (ita != newIn.end()) {
                  a = ita->second;
                }
                auto itb = out[preds[k][p]].find(n);
                if (itb != out[preds[k][p]].end()) {
                  b = itb->second;
                }
                merged[n] = meetLattice(a, b);
              }
              newIn = std::move(merged);
            }
          }
          if (!statesEqual(newIn, in[k])) {
            in[k] = std::move(newIn);
            converged = false;
          }
          State newOut = in[k];
          for (std::size_t idx = blocks[k].first; idx <= blocks[k].second; ++idx) {
            transferInst(newOut, ir[idx]);
          }
          if (!statesEqual(newOut, out[k])) {
            out[k] = std::move(newOut);
            converged = false;
          }
        }
      }

      // ---- 按入口状态重写指令 ----
      for (std::size_t k = 0; k < blocks.size(); ++k) {
        State st = in[k];
        std::unordered_map<std::string, std::string> copyMap;
        const auto resolveOperand = [&](Operand& op) {
          if (op.isLocalVar()) {
            auto it = st.find(op.name);
            if (it != st.end() && it->second.isConst) {
              // k 前缀是有意物化到寄存器的循环常量。保留其所有使用，既让
              // 比较收敛为 blt/bge，也允许 `x = k - x` 复用同一寄存器；若
              // 重新传播成立即数，热循环会退化为每轮 li/slti。
              const bool keepK = !op.name.empty() && op.name[0] == 'k';
              if (!keepK) {
                op = Operand::imm(it->second.val);
                changed = true;
              }
              return;
            }
            // 跟随复制链（最多 4 跳）
            for (int hop = 0; hop < 4; ++hop) {
              auto cit = copyMap.find(op.name);
              if (cit == copyMap.end()) {
                return;
              }
              op = Operand::localVar(cit->second);
              changed = true;
            }
            return;
          }
          if (op.isGlobalVar()) {
            auto git = readOnlyGlobals.find(op.name);
            if (git != readOnlyGlobals.end()) {
              op = Operand::imm(git->second);
              changed = true;
            }
          }
        };
        const auto invalidateCopies = [&](const std::string& name) {
          copyMap.erase(name);
          for (auto it = copyMap.begin(); it != copyMap.end();) {
            if (it->second == name) {
              it = copyMap.erase(it);
            } else {
              ++it;
            }
          }
        };
        for (std::size_t idx = blocks[k].first; idx <= blocks[k].second; ++idx) {
          IRInst& inst = ir[idx];
          // 分支条件参与 CFG 后继选择；当前格值分析同时合并两条边，不能
          // 据此把条件临时量改成立即数，否则循环/汇合处可能选错路径。
          if (inst.op != IROp::BEQZ && inst.op != IROp::BNEZ) {
            resolveOperand(inst.src1);
            resolveOperand(inst.src2);
          }
          if (inst.op == IROp::RETURN || inst.op == IROp::PARAM) {
            resolveOperand(inst.dest);
          }
          invalidateCopies(inst.dest.name);
          if (inst.op == IROp::CALL) {
            // 调用可能修改全局变量：值为全局变量的复制映射失效
            for (auto it = copyMap.begin(); it != copyMap.end();) {
              if (allGlobals.count(it->second) > 0) {
                it = copyMap.erase(it);
              } else {
                ++it;
              }
            }
          }
          transferInst(st, inst);
          if (inst.op == IROp::ASSIGN && inst.dest.isLocalVar() && inst.src1.isLocalVar() &&
              inst.src1.name != inst.dest.name) {
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
        const bool sameValue = inst.src1.type == inst.src2.type &&
                               ((inst.src1.isLocalVar() && inst.src1.name == inst.src2.name) ||
                                (inst.src1.isGlobalVar() && inst.src1.name == inst.src2.name));
        if (sameValue) {
          int result = 0;
          bool foldIdentity = true;
          switch (inst.op) {
          case IROp::SUB:
          case IROp::MOD:
          case IROp::LT:
          case IROp::GT:
          case IROp::NE:
            result = 0;
            break;
          case IROp::DIV:
          case IROp::LE:
          case IROp::GE:
          case IROp::EQ:
            result = 1;
            break;
          default:
            foldIdentity = false;
            break;
          }
          if (foldIdentity) {
            inst.op = IROp::ASSIGN;
            inst.src1 = Operand::imm(result);
            inst.src2 = Operand::none();
            changed = true;
            continue;
          }
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
          } else if (inst.src2.isImm() && inst.src2.immVal == -1) {
            inst.op = IROp::SUB;
            inst.src2 = inst.src1;
            inst.src1 = Operand::imm(0);
            changed = true;
          } else if (inst.src1.isImm() && inst.src1.immVal == -1) {
            inst.op = IROp::SUB;
            inst.src1 = Operand::imm(0);
            changed = true;
          }
        }
        // x / 1 = x
        if (inst.op == IROp::DIV && inst.src2.isImm() && inst.src2.immVal == 1) {
          inst.op = IROp::ASSIGN;
          inst.src2 = Operand::none();
          changed = true;
        }
        // 0 / x = 0；输入程序保证执行路径上除数非零。
        if (inst.op == IROp::DIV && inst.src1.isImm() && inst.src1.immVal == 0) {
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
        if (inst.op == IROp::MOD && inst.src1.isImm() && inst.src1.immVal == 0) {
          inst.op = IROp::ASSIGN;
          inst.src2 = Operand::none();
          changed = true;
        }
      }
    }

    // Pass 0c2: 常量加法链合并
    // ADD t, a, #c1; ADD d, t, #c2 → ADD d, a, #(c1+c2)（t 为临时变量，两条指令相邻）
    // 将 `s = s + c1; s = s + c2; ...` 之类的累加链收敛为单条 addi，减少循环体指令数。
    // - 若 q 仍以 t 为目标（ADD t, t, #c2）：改写后 t 继续有效，无需使用计数检查；
    // - 否则（ADD d, t, #c2，d != t）：要求 t 除 q 外无其他使用，否则删除定义会悬垂。
    {
      std::unordered_map<std::string, int> chainUseCount;
      countUses(ir, chainUseCount);
      std::vector<IRInst> optimized;
      std::size_t ci = 0;
      bool chainMerged = false;
      while (ci < ir.size()) {
        bool mergedHere = false;
        if (ci + 1 < ir.size() && ir[ci].op == IROp::ADD && ir[ci].src2.isImm() &&
            ir[ci].dest.isLocalVar() && isTempName(ir[ci].dest.name)) {
          const std::string t = ir[ci].dest.name;
          const IRInst& q = ir[ci + 1];
          if (q.op == IROp::ADD && q.src1.isLocalVar() && q.src1.name == t && q.src2.isImm()) {
            const int combined = ir[ci].src2.immVal + q.src2.immVal;
            if (q.dest.isLocalVar() && q.dest.name == t) {
              // 情况 A：q 改写后仍定义 t，t 的后继使用读到的新值与改写前等价
              IRInst merged = ir[ci];
              merged.src2 = Operand::imm(combined);
              optimized.push_back(merged);
              --chainUseCount[t]; // q 的 src1 使用被消除
              ci += 2;
              mergedHere = true;
              chainMerged = true;
            } else if (chainUseCount[t] == 1) {
              // 情况 B：q 不再定义 t，t 必须无其他使用
              IRInst merged = q;
              merged.src1 = ir[ci].src1;
              merged.src2 = Operand::imm(combined);
              optimized.push_back(merged);
              --chainUseCount[t];
              ci += 2;
              mergedHere = true;
              chainMerged = true;
            }
          }
        }
        if (!mergedHere) {
          optimized.push_back(ir[ci]);
          ++ci;
        }
      }
      if (chainMerged) {
        ir = std::move(optimized);
        changed = true;
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
        // 正 2 的幂取模跳过重写：代码生成阶段对已知非负操作数可直接用 andi（1 条），
        // 即使符号未知的通用序列（约 6 条）也不比重写后的 div+mul+sub 差；
        // 保留 MOD 还能配合非负分析省去整段序列。
        const bool powerOfTwo = inst.src2.isImm() && inst.src2.immVal > 0 &&
                                (inst.src2.immVal & (inst.src2.immVal - 1)) == 0;
        if (inst.op == IROp::MOD && inst.src2.isImm() && !powerOfTwo &&
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
    // 则复用之前的结果。Pass 1 会把 `OP tmp; ASSIGN local, tmp` 合并为直接写
    // local，因此这里不能只缓存 t%d 临时；用户局部变量同样可以承载可复用值。
    // 对 ADD/MUL/EQ/NE 规范化操作数顺序，使 `x+y` 与 `y+x` 共享同一值编号。
    {
      std::unordered_map<std::string, std::string> rename;   // 原名 → 复用名（仅临时变量）
      std::unordered_map<std::string, std::string> valueMap; // (op,src1,src2) → 结果变量
      std::unordered_map<std::string, std::vector<std::string>> varKeys; // 变量 → 引用它的 key
      std::vector<IRInst> optimized;
      bool cseChanged = false;
      auto invalidateVar = [&](const std::string& name) {
        auto it = varKeys.find(name);
        if (it != varKeys.end()) {
          for (const auto& key : it->second) {
            valueMap.erase(key);
          }
          varKeys.erase(it);
        }

        // name 也可能是某个已缓存表达式的结果。结果寄存位置被覆盖后，
        // 即使表达式的输入都没变，也不能继续从 name 取旧值。
        for (auto value = valueMap.begin(); value != valueMap.end();) {
          if (value->second == name) {
            value = valueMap.erase(value);
          } else {
            ++value;
          }
        }
      };
      for (const auto& inst : ir) {
        // 控制流/调用/内存边界：重置所有映射（保守处理）
        if (inst.op == IROp::LABEL || inst.op == IROp::BRANCH || inst.op == IROp::BEQZ ||
            inst.op == IROp::BNEZ || inst.op == IROp::CALL || inst.op == IROp::LOAD ||
            inst.op == IROp::STORE || inst.op == IROp::RETURN || inst.op == IROp::PARAM) {
          valueMap.clear();
          varKeys.clear();
          rename.clear();
        }
        IRInst cur = inst;
        // 源操作数重命名
        if (cur.src1.isLocalVar()) {
          auto it = rename.find(cur.src1.name);
          if (it != rename.end()) {
            cur.src1 = Operand::localVar(it->second);
            cseChanged = true;
          }
        }
        if (cur.src2.isLocalVar()) {
          auto it = rename.find(cur.src2.name);
          if (it != rename.end()) {
            cur.src2 = Operand::localVar(it->second);
            cseChanged = true;
          }
        }
        // 本指令定义变量：使引用该变量的 CSE 条目失效（其值已变化）
        const bool definesLocal =
            cur.dest.isLocalVar() && cur.op != IROp::RETURN && cur.op != IROp::PARAM;
        if (definesLocal) {
          rename.erase(cur.dest.name);
          for (auto alias = rename.begin(); alias != rename.end();) {
            if (alias->second == cur.dest.name) {
              alias = rename.erase(alias);
            } else {
              ++alias;
            }
          }
          invalidateVar(cur.dest.name);
        }
        const bool readsDestination =
            cur.dest.isLocalVar() && ((cur.src1.isLocalVar() && cur.src1.name == cur.dest.name) ||
                                      (cur.src2.isLocalVar() && cur.src2.name == cur.dest.name));
        if (isCombinableOp(cur.op) && cur.dest.isLocalVar() && !readsDestination &&
            cur.op != IROp::NOT && cur.src1.type != OperandType::NONE &&
            cur.src2.type != OperandType::NONE && (cur.src1.isLocalVar() || cur.src1.isImm()) &&
            (cur.src2.isLocalVar() || cur.src2.isImm())) {
          const auto operandKey = [](const Operand& operand) {
            return operand.isImm() ? "#" + std::to_string(operand.immVal) : "%" + operand.name;
          };
          std::string lhsKey = operandKey(cur.src1);
          std::string rhsKey = operandKey(cur.src2);
          const bool commutative = cur.op == IROp::ADD || cur.op == IROp::MUL ||
                                   cur.op == IROp::EQ || cur.op == IROp::NE;
          if (commutative && rhsKey < lhsKey) {
            std::swap(lhsKey, rhsKey);
          }
          const std::string key = std::string(irOpToString(cur.op)) + "|" + lhsKey + "|" + rhsKey;
          auto it = valueMap.find(key);
          if (it != valueMap.end()) {
            // 保留一条显式复制，避免已有结果变量在冗余表达式的使用点之前
            // 被覆盖时留下悬垂别名。下一轮块内复制传播会把紧随的使用直接
            // 改到已有结果，并由 DCE 删除这条 ASSIGN。
            cur.op = IROp::ASSIGN;
            cur.src1 = Operand::localVar(it->second);
            cur.src2 = Operand::none();
            if (isTempName(cur.dest.name)) {
              rename[cur.dest.name] = it->second;
            }
            optimized.push_back(std::move(cur));
            changed = true;
            cseChanged = true;
            continue;
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
      if (cseChanged || ir.size() != optimized.size()) {
        ir = std::move(optimized);
      }
    }

    // Pass 2: 死代码消除
    {
      // 指令级 CFG 活跃性：相比全函数 useCount，能够识别同一变量后续被
      // 覆盖的死存储，例如 `x = expensive; x = 5; return x;`。
      std::unordered_map<std::string, std::size_t> labelPositions;
      for (std::size_t index = 0; index < ir.size(); ++index) {
        if (ir[index].op == IROp::LABEL && ir[index].dest.isLabel()) {
          labelPositions[ir[index].dest.name] = index;
        }
      }

      std::vector<std::vector<std::size_t>> successors(ir.size());
      for (std::size_t index = 0; index < ir.size(); ++index) {
        const auto& inst = ir[index];
        const auto addTarget = [&](const Operand& target) {
          if (!target.isLabel()) {
            return;
          }
          const auto found = labelPositions.find(target.name);
          if (found != labelPositions.end()) {
            successors[index].push_back(found->second);
          }
        };
        if (inst.op == IROp::BRANCH) {
          addTarget(inst.dest);
        } else if (inst.op == IROp::BEQZ || inst.op == IROp::BNEZ) {
          addTarget(inst.dest);
          if (index + 1 < ir.size()) {
            successors[index].push_back(index + 1);
          }
        } else if (inst.op != IROp::RETURN && inst.op != IROp::FUNC_END && index + 1 < ir.size()) {
          successors[index].push_back(index + 1);
        }
      }

      std::vector<std::unordered_set<std::string>> liveIn(ir.size());
      std::vector<std::unordered_set<std::string>> liveOut(ir.size());
      bool livenessChanged = true;
      while (livenessChanged) {
        livenessChanged = false;
        for (std::size_t reverse = ir.size(); reverse-- > 0;) {
          std::unordered_set<std::string> nextOut;
          for (const std::size_t successor : successors[reverse]) {
            nextOut.insert(liveIn[successor].begin(), liveIn[successor].end());
          }
          std::unordered_set<std::string> nextIn = nextOut;
          const auto& inst = ir[reverse];
          const bool definesLocal = inst.dest.isLocalVar() && inst.op != IROp::RETURN &&
                                    inst.op != IROp::PARAM && inst.op != IROp::BRANCH &&
                                    inst.op != IROp::BEQZ && inst.op != IROp::BNEZ;
          if (definesLocal) {
            nextIn.erase(inst.dest.name);
          }
          if (inst.src1.isLocalVar()) {
            nextIn.insert(inst.src1.name);
          }
          if (inst.src2.isLocalVar()) {
            nextIn.insert(inst.src2.name);
          }
          if ((inst.op == IROp::RETURN || inst.op == IROp::PARAM) && inst.dest.isLocalVar()) {
            nextIn.insert(inst.dest.name);
          }
          if (nextOut != liveOut[reverse] || nextIn != liveIn[reverse]) {
            liveOut[reverse] = std::move(nextOut);
            liveIn[reverse] = std::move(nextIn);
            livenessChanged = true;
          }
        }
      }

      std::vector<IRInst> optimized;
      for (std::size_t index = 0; index < ir.size(); ++index) {
        const auto& inst = ir[index];
        if (isCombinableOp(inst.op) && inst.dest.isLocalVar() &&
            liveOut[index].count(inst.dest.name) == 0) {
          changed = true;
          continue;
        }
        if (inst.op == IROp::ASSIGN && inst.dest.isLocalVar() &&
            liveOut[index].count(inst.dest.name) == 0) {
          changed = true;
          continue;
        }
        optimized.push_back(inst);
      }
      if (ir.size() != optimized.size()) {
        ir = std::move(optimized);
      }
    }

    // Pass 2.5: 删除无法到达可观察行为的局部/全局递推。
    //
    // 普通活跃性会把 `dead = f(dead)` 保留下来：本轮定义被下一轮自引用，
    // 因而形成一个没有出口的活跃环。全局变量也可能只在死 store 之间互相
    // 供值；ToyC 没有指针、volatile 或外部可见内存，因此这种全局写同样不
    // 是可观察行为。这里从 RETURN、分支条件、实参和真正的副作用指令反向
    // 传播“有用变量”，只保留能够到达这些根的计算。局部/全局 key 显式区分，
    // 按变量而非单次定义分析会保守保留同一变量的所有可达赋值。
    {
      struct StructuredIf {
        std::size_t branchIndex;
        std::size_t joinIndex;
        std::unordered_set<std::string> conditionVars;
        std::unordered_set<std::string> controlledDefs;
        std::unordered_set<std::size_t> controlledBranches;
      };

      const auto variableKey = [](const Operand& operand) -> std::optional<std::string> {
        if (operand.isLocalVar()) {
          return "L:" + operand.name;
        }
        if (operand.isGlobalVar()) {
          return "G:" + operand.name;
        }
        return std::nullopt;
      };
      const auto addVariableUse = [&](const Operand& operand,
                                      std::unordered_set<std::string>& variables) {
        if (const auto key = variableKey(operand)) {
          variables.insert(*key);
        }
      };

      std::unordered_map<std::string, std::size_t> labelPositions;
      std::unordered_map<std::string, std::vector<std::size_t>> labelReferences;
      for (std::size_t index = 0; index < ir.size(); ++index) {
        const IRInst& inst = ir[index];
        if (inst.op == IROp::LABEL && inst.dest.isLabel()) {
          labelPositions[inst.dest.name] = index;
        }
        if ((inst.op == IROp::BRANCH || inst.op == IROp::BEQZ || inst.op == IROp::BNEZ) &&
            inst.dest.isLabel()) {
          labelReferences[inst.dest.name].push_back(index);
        }
      }

      // 识别生成器产生的前向 if/if-else 区域。只有区域内全部为局部纯计算和
      // 前向内部边、且不存在外部入口时，分支条件才不必先验地视为可观察根。
      std::vector<StructuredIf> structuredIfs;
      std::unordered_set<std::size_t> suppressBranchRoots;
      for (std::size_t branchIndex = 0; branchIndex < ir.size(); ++branchIndex) {
        const IRInst& branch = ir[branchIndex];
        if (branch.op != IROp::BEQZ || !branch.dest.isLabel()) {
          continue;
        }
        const auto elseFound = labelPositions.find(branch.dest.name);
        if (elseFound == labelPositions.end() || elseFound->second <= branchIndex) {
          continue;
        }

        const std::size_t elseIndex = elseFound->second;
        std::size_t joinIndex = elseIndex;
        if (elseIndex > branchIndex + 1 && ir[elseIndex - 1].op == IROp::BRANCH &&
            ir[elseIndex - 1].dest.isLabel()) {
          const auto endFound = labelPositions.find(ir[elseIndex - 1].dest.name);
          if (endFound != labelPositions.end() && endFound->second > elseIndex) {
            joinIndex = endFound->second;
          }
        }

        bool removableControl = true;
        std::unordered_set<std::string> controlledLabels;
        std::unordered_set<std::string> controlledDefs;
        std::unordered_set<std::string> conditionVars;
        std::unordered_set<std::size_t> controlledBranches;
        for (std::size_t index = branchIndex + 1; index < joinIndex && removableControl; ++index) {
          const IRInst& inst = ir[index];
          const bool pureVariableDefinition =
              (inst.dest.isLocalVar() || inst.dest.isGlobalVar()) &&
              (isCombinableOp(inst.op) || inst.op == IROp::ASSIGN ||
               (inst.op == IROp::LOCAL_VAR_DECL && inst.dest.isLocalVar()));
          if (pureVariableDefinition) {
            controlledDefs.insert(*variableKey(inst.dest));
            continue;
          }
          if (inst.op == IROp::LABEL && inst.dest.isLabel()) {
            controlledLabels.insert(inst.dest.name);
            continue;
          }
          if ((inst.op == IROp::BRANCH || inst.op == IROp::BEQZ || inst.op == IROp::BNEZ) &&
              inst.dest.isLabel()) {
            const auto target = labelPositions.find(inst.dest.name);
            // 内部控制流也必须严格向前；嵌套循环或 continue/break 会改变终止性。
            removableControl = target != labelPositions.end() && target->second > index &&
                               target->second <= joinIndex;
            if (removableControl && (inst.op == IROp::BEQZ || inst.op == IROp::BNEZ)) {
              controlledBranches.insert(index);
              addVariableUse(inst.src1, conditionVars);
              addVariableUse(inst.src2, conditionVars);
            }
            continue;
          }
          removableControl = false;
        }
        if (!removableControl || ir[joinIndex].op != IROp::LABEL || !ir[joinIndex].dest.isLabel()) {
          continue;
        }
        controlledLabels.insert(ir[joinIndex].dest.name);

        // 所有受控标签只能从待删除区域内进入；否则删除会留下悬空外部跳转。
        for (const auto& label : controlledLabels) {
          const auto references = labelReferences.find(label);
          if (references == labelReferences.end()) {
            continue;
          }
          for (const std::size_t reference : references->second) {
            if (reference < branchIndex || reference >= joinIndex) {
              removableControl = false;
              break;
            }
          }
          if (!removableControl) {
            break;
          }
        }
        if (!removableControl) {
          continue;
        }

        StructuredIf candidate{branchIndex, joinIndex, std::move(conditionVars),
                               std::move(controlledDefs), std::move(controlledBranches)};
        addVariableUse(branch.src1, candidate.conditionVars);
        addVariableUse(branch.src2, candidate.conditionVars);
        candidate.controlledBranches.insert(branchIndex);
        suppressBranchRoots.insert(candidate.controlledBranches.begin(),
                                   candidate.controlledBranches.end());
        structuredIfs.push_back(std::move(candidate));
      }

      std::unordered_map<std::string, std::unordered_set<std::string>> dependencies;
      std::unordered_set<std::string> useful;

      for (std::size_t index = 0; index < ir.size(); ++index) {
        const IRInst& inst = ir[index];
        const bool pureVariableDefinition =
            (inst.dest.isLocalVar() || inst.dest.isGlobalVar()) &&
            (isCombinableOp(inst.op) || inst.op == IROp::ASSIGN ||
             (inst.op == IROp::LOCAL_VAR_DECL && inst.dest.isLocalVar()));
        if (pureVariableDefinition) {
          auto& definitionDependencies = dependencies[*variableKey(inst.dest)];
          addVariableUse(inst.src1, definitionDependencies);
          addVariableUse(inst.src2, definitionDependencies);
          continue;
        }

        if (suppressBranchRoots.count(index) != 0) {
          continue;
        }

        // 对不可删除指令，只把真正读取的变量操作数作为根；普通 dest 是定义，
        // RETURN/PARAM 的 dest 则按 IR 约定是被读取的值。
        addVariableUse(inst.src1, useful);
        addVariableUse(inst.src2, useful);
        if (inst.op == IROp::RETURN || inst.op == IROp::PARAM) {
          addVariableUse(inst.dest, useful);
        }
      }

      // 若受控区域定义的变量最终有用，则分支条件也有用。把控制依赖编码成
      // `defined variable -> condition` 的依赖边；无出口的数据/控制互相递推 SCC
      // 不会凭空成为根，因此整棵死 if 可以一起消失。
      for (const auto& candidate : structuredIfs) {
        for (const auto& definition : candidate.controlledDefs) {
          auto& deps = dependencies[definition];
          deps.insert(candidate.conditionVars.begin(), candidate.conditionVars.end());
        }
      }

      std::vector<std::string> worklist(useful.begin(), useful.end());
      while (!worklist.empty()) {
        const std::string variable = std::move(worklist.back());
        worklist.pop_back();
        const auto found = dependencies.find(variable);
        if (found == dependencies.end()) {
          continue;
        }
        for (const auto& dependency : found->second) {
          if (useful.insert(dependency).second) {
            worklist.push_back(dependency);
          }
        }
      }

      std::vector<std::pair<std::size_t, std::size_t>> deadIntervals;
      for (const auto& candidate : structuredIfs) {
        bool controlsUsefulDefinition = false;
        for (const auto& definition : candidate.controlledDefs) {
          if (useful.count(definition) != 0) {
            controlsUsefulDefinition = true;
            break;
          }
        }
        bool usefulCondition = false;
        for (const auto& condition : candidate.conditionVars) {
          if (useful.count(condition) != 0) {
            usefulCondition = true;
            break;
          }
        }
        if (!controlsUsefulDefinition && !usefulCondition) {
          deadIntervals.emplace_back(candidate.branchIndex, candidate.joinIndex + 1);
        }
      }
      std::sort(deadIntervals.begin(), deadIntervals.end());
      std::vector<std::pair<std::size_t, std::size_t>> mergedIntervals;
      for (const auto& interval : deadIntervals) {
        if (mergedIntervals.empty() || interval.first > mergedIntervals.back().second) {
          mergedIntervals.push_back(interval);
        } else {
          mergedIntervals.back().second = std::max(mergedIntervals.back().second, interval.second);
        }
      }

      std::vector<IRInst> optimized;
      optimized.reserve(ir.size());
      bool rootedDceChanged = false;
      std::size_t intervalIndex = 0;
      for (std::size_t index = 0; index < ir.size(); ++index) {
        while (intervalIndex < mergedIntervals.size() &&
               index >= mergedIntervals[intervalIndex].second) {
          ++intervalIndex;
        }
        if (intervalIndex < mergedIntervals.size() &&
            index >= mergedIntervals[intervalIndex].first &&
            index < mergedIntervals[intervalIndex].second) {
          rootedDceChanged = true;
          continue;
        }

        const IRInst& inst = ir[index];
        const bool deadVariableDefinition = (inst.dest.isLocalVar() || inst.dest.isGlobalVar()) &&
                                            useful.count(*variableKey(inst.dest)) == 0 &&
                                            (isCombinableOp(inst.op) || inst.op == IROp::ASSIGN);
        if (deadVariableDefinition) {
          rootedDceChanged = true;
          continue;
        }
        optimized.push_back(inst);
      }
      if (rootedDceChanged) {
        ir = std::move(optimized);
        changed = true;
      }
    }

    // Pass 2.75: 删除返回值无用的、已证明纯且必然终止的函数调用。
    //
    // 内联预算会保留较大的函数调用，而普通 DCE 必须把任意 CALL 当成副作用根。
    // 这里仅接受无全局写/STORE/未知调用、调用图无环的已定义函数。CFG 可以
    // 包含规范的单位步进单调循环，但每条回边都必须独立证明终止；这让返回值
    // 无用的有限纯 helper 不再因为含有一个 while 就永久成为副作用根。
    {
      struct FunctionEffects {
        std::size_t begin = 0;
        std::size_t end = 0;
        std::size_t paramCount = 0;
        bool locallyPureAndTerminatingShape = true;
        std::unordered_set<std::string> callees;
      };

      std::unordered_map<std::string, FunctionEffects> functions;
      for (std::size_t begin = 0; begin < ir.size(); ++begin) {
        if (ir[begin].op != IROp::FUNC_BEGIN || !ir[begin].dest.isFunc()) {
          continue;
        }
        FunctionEffects effects;
        effects.begin = begin;
        effects.end = begin + 1;
        while (effects.end < ir.size() && ir[effects.end].op != IROp::FUNC_END) {
          ++effects.end;
        }

        std::unordered_map<std::string, std::size_t> labels;
        for (std::size_t index = effects.begin + 1; index < effects.end; ++index) {
          const IRInst& inst = ir[index];
          if (inst.op == IROp::LOCAL_VAR_DECL && inst.src1.isParam()) {
            ++effects.paramCount;
          }
          if (inst.op == IROp::LABEL && inst.dest.isLabel()) {
            labels[inst.dest.name] = index;
          }
        }

        // 回边按 IR 顺序从内到外出现；先证明的内层循环可作为有限区域参与
        // 外层循环的终止性证明，而不是让任意嵌套 while 都否决整个纯函数。
        std::unordered_set<std::size_t> provenBackedges;

        const auto provenTerminatingBackedge = [&](std::size_t backedgeIndex,
                                                   std::size_t bodyLabelIndex) {
          if (backedgeIndex < 2 || bodyLabelIndex >= backedgeIndex ||
              ir[backedgeIndex].op != IROp::BNEZ) {
            return false;
          }
          const IRInst& condition = ir[backedgeIndex - 1];
          if ((condition.op != IROp::LT && condition.op != IROp::LE && condition.op != IROp::GT &&
               condition.op != IROp::GE) ||
              !condition.dest.isLocalVar() || !ir[backedgeIndex].src1.isLocalVar() ||
              ir[backedgeIndex].src1.name != condition.dest.name) {
            return false;
          }

          std::size_t conditionLabelIndex = backedgeIndex - 1;
          while (conditionLabelIndex > bodyLabelIndex &&
                 ir[conditionLabelIndex].op != IROp::LABEL) {
            --conditionLabelIndex;
          }
          if (conditionLabelIndex <= bodyLabelIndex || ir[conditionLabelIndex].op != IROp::LABEL) {
            return false;
          }

          const auto writtenInLoop = [&](const Operand& operand) {
            if (!operand.isLocalVar()) {
              return false;
            }
            for (std::size_t position = bodyLabelIndex + 1; position < conditionLabelIndex;
                 ++position) {
              const IRInst& candidate = ir[position];
              if (candidate.dest.isLocalVar() && candidate.dest.name == operand.name &&
                  candidate.op != IROp::RETURN && candidate.op != IROp::PARAM) {
                return true;
              }
            }
            return false;
          };
          IROp relation = condition.op;
          Operand inductionOperand = condition.src1;
          Operand boundOperand = condition.src2;
          if (!writtenInLoop(inductionOperand) && writtenInLoop(boundOperand)) {
            std::swap(inductionOperand, boundOperand);
            switch (relation) {
            case IROp::LT:
              relation = IROp::GT;
              break;
            case IROp::LE:
              relation = IROp::GE;
              break;
            case IROp::GT:
              relation = IROp::LT;
              break;
            case IROp::GE:
              relation = IROp::LE;
              break;
            default:
              break;
            }
          }
          if (!inductionOperand.isLocalVar() || !writtenInLoop(inductionOperand)) {
            return false;
          }

          const bool increasing = relation == IROp::LT || relation == IROp::LE;
          if ((relation == IROp::LE &&
               (!boundOperand.isImm() || boundOperand.immVal == INT32_MAX)) ||
              (relation == IROp::GE &&
               (!boundOperand.isImm() || boundOperand.immVal == INT32_MIN))) {
            return false;
          }

          const std::string induction = inductionOperand.name;
          const std::optional<std::string> bound =
              boundOperand.isLocalVar() ? std::optional<std::string>{boundOperand.name}
                                        : std::nullopt;
          int inductionWrites = 0;
          std::size_t inductionWriteIndex = 0;
          std::size_t lastInternalControl = bodyLabelIndex;
          for (std::size_t position = bodyLabelIndex + 1; position < conditionLabelIndex;
               ++position) {
            const IRInst& bodyInst = ir[position];
            if (bound && bodyInst.dest.isLocalVar() && bodyInst.dest.name == *bound) {
              return false;
            }
            if (bodyInst.dest.isLocalVar() && bodyInst.dest.name == induction &&
                bodyInst.op != IROp::RETURN && bodyInst.op != IROp::PARAM) {
              ++inductionWrites;
              inductionWriteIndex = position;
              const bool unitStep = ((increasing && bodyInst.op == IROp::ADD) ||
                                     (!increasing && bodyInst.op == IROp::SUB)) &&
                                    bodyInst.src1.isLocalVar() && bodyInst.src1.name == induction &&
                                    bodyInst.src2.isImm() && bodyInst.src2.immVal == 1;
              if (!unitStep) {
                return false;
              }
            }
            if ((bodyInst.op == IROp::BRANCH || bodyInst.op == IROp::BEQZ ||
                 bodyInst.op == IROp::BNEZ) &&
                bodyInst.dest.isLabel()) {
              const auto target = labels.find(bodyInst.dest.name);
              if (target == labels.end()) {
                return false;
              }
              if (target->second <= position) {
                if (provenBackedges.count(position) == 0) {
                  return false;
                }
                lastInternalControl = position;
                continue;
              }
              // 向循环退出标签的 break 只会提早终止；循环体内部的前向边
              // 则必须位于归纳更新之前，确保继续迭代的每条路径都执行更新。
              if (target->second <= conditionLabelIndex) {
                lastInternalControl = position;
              }
            }
          }
          return inductionWrites == 1 && inductionWriteIndex > lastInternalControl;
        };

        for (std::size_t index = effects.begin + 1; index < effects.end; ++index) {
          const IRInst& inst = ir[index];
          const bool globalWrite =
              inst.dest.isGlobalVar() && inst.op != IROp::PARAM && inst.op != IROp::RETURN;
          if (globalWrite || inst.op == IROp::STORE) {
            effects.locallyPureAndTerminatingShape = false;
          }
          if (inst.op == IROp::CALL) {
            if (!inst.src1.isFunc()) {
              effects.locallyPureAndTerminatingShape = false;
            } else {
              effects.callees.insert(inst.src1.name);
            }
          }
          if ((inst.op == IROp::BRANCH || inst.op == IROp::BEQZ || inst.op == IROp::BNEZ) &&
              inst.dest.isLabel()) {
            const auto target = labels.find(inst.dest.name);
            if (target == labels.end()) {
              effects.locallyPureAndTerminatingShape = false;
            } else if (target->second <= index) {
              if (provenTerminatingBackedge(index, target->second)) {
                provenBackedges.insert(index);
              } else {
                effects.locallyPureAndTerminatingShape = false;
              }
            }
          }
        }
        const std::string functionName = ir[begin].dest.name;
        const std::size_t functionEnd = effects.end;
        functions[functionName] = std::move(effects);
        begin = functionEnd;
      }

      std::unordered_set<std::string> pureTerminating;
      bool discoveredFunction = true;
      while (discoveredFunction) {
        discoveredFunction = false;
        for (const auto& entry : functions) {
          const std::string& name = entry.first;
          const FunctionEffects& effects = entry.second;
          if (!effects.locallyPureAndTerminatingShape || pureTerminating.count(name) != 0) {
            continue;
          }
          const bool calleesProven = std::all_of(
              effects.callees.begin(), effects.callees.end(), [&](const std::string& callee) {
                return functions.count(callee) != 0 && pureTerminating.count(callee) != 0;
              });
          if (calleesProven) {
            pureTerminating.insert(name);
            discoveredFunction = true;
          }
        }
      }

      std::unordered_map<std::string, std::unordered_map<std::string, int>> functionUses;
      std::string currentFunction;
      for (const auto& inst : ir) {
        if (inst.op == IROp::FUNC_BEGIN && inst.dest.isFunc()) {
          currentFunction = inst.dest.name;
          continue;
        }
        if (inst.op == IROp::FUNC_END) {
          currentFunction.clear();
          continue;
        }
        if (currentFunction.empty()) {
          continue;
        }
        auto& uses = functionUses[currentFunction];
        if (inst.src1.isLocalVar()) {
          ++uses[inst.src1.name];
        }
        if (inst.src2.isLocalVar()) {
          ++uses[inst.src2.name];
        }
        if ((inst.op == IROp::RETURN || inst.op == IROp::PARAM) && inst.dest.isLocalVar()) {
          ++uses[inst.dest.name];
        }
      }

      std::vector<IRInst> optimized;
      optimized.reserve(ir.size());
      currentFunction.clear();
      bool removedCall = false;
      for (const auto& inst : ir) {
        if (inst.op == IROp::FUNC_BEGIN && inst.dest.isFunc()) {
          currentFunction = inst.dest.name;
        } else if (inst.op == IROp::FUNC_END) {
          currentFunction.clear();
        }

        bool eraseCall = false;
        std::size_t paramCount = 0;
        if (inst.op == IROp::CALL && inst.src1.isFunc() &&
            pureTerminating.count(inst.src1.name) != 0) {
          const bool resultUnused =
              inst.dest.isNone() ||
              (inst.dest.isLocalVar() && functionUses[currentFunction][inst.dest.name] == 0);
          const auto callee = functions.find(inst.src1.name);
          if (resultUnused && callee != functions.end()) {
            paramCount = callee->second.paramCount;
            eraseCall = optimized.size() >= paramCount;
            for (std::size_t parameter = 0; parameter < paramCount && eraseCall; ++parameter) {
              const IRInst& argument = optimized[optimized.size() - paramCount + parameter];
              eraseCall = argument.op == IROp::PARAM && argument.src1.isImm() &&
                          argument.src1.immVal == static_cast<int>(parameter);
            }
          }
        }
        if (eraseCall) {
          optimized.resize(optimized.size() - paramCount);
          removedCall = true;
          continue;
        }
        optimized.push_back(inst);
      }
      if (removedCall) {
        ir = std::move(optimized);
        changed = true;
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
            // 原始 IR 在 BEQZ 后有 LABEL b。内联后若该标签没有其它引用，标签清理
            // 会把它删除；循环语义不变，但后续反转/摘要会失去规范入口。此时创建
            // 一个只供新回边使用的 body 标签，使内联循环也进入同一优化链。
            const bool hasBodyLabel = ir[j + 1].op == IROp::LABEL;
            const std::string bodyLabel = hasBodyLabel ? ir[j + 1].dest.name : newLabel();
            const std::size_t bodyStart = j + 1;
            {
              // 向后扫描到 LABEL e，记录最后一个 BRANCH c（循环尾跳转）
              std::size_t k = bodyStart;
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
                // 将循环内关系比较的大多数立即数提升并共享到循环外寄存器。
                // 这样 `slti + beqz` 可收敛为单条 bge/blt；0/1 的 LT/GE 已能
                // 由窥孔变成 bgez/bgtz 等单指令分支，保留立即数可节省寄存器。
                std::unordered_map<int, std::string> loopConstants;
                std::vector<std::pair<int, std::string>> constantOrder;
                std::unordered_set<int> reverseSubConstants;
                for (std::size_t m = j + 1; m < lastBranchC; ++m) {
                  const auto& inst = ir[m];
                  if (inst.op == IROp::SUB && inst.src1.isImm() && inst.dest.isLocalVar() &&
                      inst.src2.isLocalVar() && inst.dest.name == inst.src2.name) {
                    reverseSubConstants.insert(inst.src1.immVal);
                  }
                }
                const auto hoistCompareConstant = [&](IRInst& compare) {
                  const bool isRelational = compare.op == IROp::LT || compare.op == IROp::LE ||
                                            compare.op == IROp::GT || compare.op == IROp::GE;
                  if (!isRelational || !compare.src2.isImm()) {
                    return;
                  }
                  const int value = compare.src2.immVal;
                  const bool hasDirectZeroOneBranch =
                      (compare.op == IROp::LT || compare.op == IROp::GE) &&
                      (value == 0 || value == 1) && reverseSubConstants.count(value) == 0;
                  if (hasDirectZeroOneBranch) {
                    return;
                  }
                  auto found = loopConstants.find(value);
                  if (found == loopConstants.end()) {
                    const std::string name = "k" + std::to_string(tempCounter++);
                    found = loopConstants.emplace(value, name).first;
                    constantOrder.emplace_back(value, name);
                  }
                  compare.src2 = Operand::localVar(found->second);
                };
                for (std::size_t m = j + 1; m < lastBranchC; ++m) {
                  hoistCompareConstant(ir[m]);
                }
                for (std::size_t m = i + 1; m < j; ++m) {
                  hoistCompareConstant(ir[m]);
                }
                // 若同一常量还用于 `x = c - x`，复用已提升寄存器；后端可直接
                // 发射 `sub x, kc, x`，不再每轮 li 常量。
                for (std::size_t m = j + 1; m < lastBranchC; ++m) {
                  auto& inst = ir[m];
                  if (inst.op != IROp::SUB || !inst.src1.isImm() || !inst.dest.isLocalVar() ||
                      !inst.src2.isLocalVar() || inst.dest.name != inst.src2.name) {
                    continue;
                  }
                  auto constant = loopConstants.find(inst.src1.immVal);
                  if (constant != loopConstants.end()) {
                    inst.src1 = Operand::localVar(constant->second);
                  }
                }
                for (const auto& entry : constantOrder) {
                  optimized.push_back(IRInst(IROp::ASSIGN, Operand::localVar(entry.second),
                                             Operand::imm(entry.first), Operand::none()));
                }
                // 变换:
                // 1. 在 LABEL b 之前插入 BRANCH c（首次进入先测试）
                // 2. BEQZ t,e -> BNEZ t,b（条件满足跳回循环体）
                // 3. 循环尾 BRANCH c -> LABEL c（测试点移到循环体末尾）
                // 4. 条件块移动到测试点（循环体末尾）重新执行
                optimized.push_back(IRInst(IROp::BRANCH, Operand::label(condLabel), Operand::none(),
                                           Operand::none()));
                if (!hasBodyLabel) {
                  optimized.push_back(IRInst(IROp::LABEL, Operand::label(bodyLabel),
                                             Operand::none(), Operand::none()));
                }
                // 循环体（LABEL b 到循环尾跳转之前）
                for (std::size_t m = bodyStart; m < lastBranchC; ++m) {
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
            // 用户变量外提安全条件：
            //  1. 赋值位于循环体顶层的直线段内（无条件执行，且位于任何控制流之前），
            //     保证该赋值"支配"循环内对它的所有读取，循环体外的读取不受影响；
            //  2. 赋值之前没有对该变量的读取（避免把"读到循环前旧值"改为"读到新值"）。
            // 满足后，外提等价于把不变值提前计算一次，循环内每次读取到的值不变。
            bool straightLine = true;
            std::unordered_set<std::string> readBeforeDef;
            kept.push_back(ir[i + 1]); // LABEL Lb：循环体入口标签必须保留在原位
            for (std::size_t k = i + 2; k < condIdx; ++k) {
              const auto& inst = ir[k];
              // 控制流/调用/内存屏障：之后不再处于顶层直线段，停止用户变量外提
              if (inst.op == IROp::LABEL || inst.op == IROp::BRANCH || inst.op == IROp::BEQZ ||
                  inst.op == IROp::BNEZ || inst.op == IROp::CALL || inst.op == IROp::LOAD ||
                  inst.op == IROp::STORE || inst.op == IROp::RETURN || inst.op == IROp::PARAM) {
                straightLine = false;
                kept.push_back(inst);
                continue;
              }
              const bool pure =
                  inst.op == IROp::ADD || inst.op == IROp::SUB || inst.op == IROp::MUL ||
                  inst.op == IROp::NOT || inst.op == IROp::LT || inst.op == IROp::GT ||
                  inst.op == IROp::LE || inst.op == IROp::GE || inst.op == IROp::EQ ||
                  inst.op == IROp::NE ||
                  (inst.op == IROp::ASSIGN && inst.dest.isLocalVar() && isTempName(inst.dest.name));
              const bool operandsInv = operandInvariant(inst.src1) && operandInvariant(inst.src2);
              const bool canHoist = pure && inst.dest.isLocalVar() &&
                                    defCount[inst.dest.name] == 1 && operandsInv &&
                                    (isTempName(inst.dest.name) ||
                                     (straightLine && readBeforeDef.count(inst.dest.name) == 0));
              if (canHoist) {
                hoisted.push_back(inst);
                licmChanged = true;
              } else {
                kept.push_back(inst);
              }
              // 记录直线段内被读取的变量（供后续用户变量赋值外提检查）
              if (straightLine) {
                if (inst.src1.isLocalVar()) {
                  readBeforeDef.insert(inst.src1.name);
                }
                if (inst.src2.isLocalVar()) {
                  readBeforeDef.insert(inst.src2.name);
                }
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

  // Pass 4.25: 把循环体开头的单调 break guard 收紧为循环上界。
  //
  //   while (i < U) { if (i >= B) break; body; i = i + 1; }
  //
  // 在 U/B 均为当前路径上的常量、非 break 路径无其它控制流且归纳变量严格
  // +1 时，等价于 `while (i < min(U, B))`。规范化后，后续仿射/矩阵摘要无需
  // 分别理解 break CFG。动态阈值、额外入口、非单位步进与后置 guard 均回退。
  {
    bool normalizedBreak = true;
    while (normalizedBreak) {
      normalizedBreak = false;
      std::unordered_map<std::string, std::size_t> labelPositions;
      std::unordered_map<std::string, int> labelReferences;
      for (std::size_t position = 0; position < ir.size(); ++position) {
        const IRInst& inst = ir[position];
        if (inst.op == IROp::LABEL && inst.dest.isLabel()) {
          labelPositions[inst.dest.name] = position;
        }
        if ((inst.op == IROp::BRANCH || inst.op == IROp::BEQZ || inst.op == IROp::BNEZ) &&
            inst.dest.isLabel()) {
          ++labelReferences[inst.dest.name];
        }
      }

      for (std::size_t loopStart = 0; loopStart + 1 < ir.size() && !normalizedBreak; ++loopStart) {
        if (ir[loopStart].op != IROp::BRANCH || !ir[loopStart].dest.isLabel() ||
            ir[loopStart + 1].op != IROp::LABEL || !ir[loopStart + 1].dest.isLabel()) {
          continue;
        }
        const std::string condLabel = ir[loopStart].dest.name;
        const std::string bodyLabel = ir[loopStart + 1].dest.name;
        if (labelReferences[condLabel] != 1 || labelReferences[bodyLabel] != 1) {
          continue;
        }
        const auto condFound = labelPositions.find(condLabel);
        if (condFound == labelPositions.end() || condFound->second <= loopStart + 5) {
          continue;
        }
        const std::size_t condIndex = condFound->second;
        if (condIndex + 3 >= ir.size() || ir[condIndex + 1].op != IROp::LT ||
            !ir[condIndex + 1].dest.isLocalVar() || !ir[condIndex + 1].src1.isLocalVar() ||
            ir[condIndex + 2].op != IROp::BNEZ || !ir[condIndex + 2].dest.isLabel() ||
            ir[condIndex + 2].dest.name != bodyLabel || !ir[condIndex + 2].src1.isLocalVar() ||
            ir[condIndex + 2].src1.name != ir[condIndex + 1].dest.name ||
            ir[condIndex + 3].op != IROp::LABEL || !ir[condIndex + 3].dest.isLabel()) {
          continue;
        }
        const std::string induction = ir[condIndex + 1].src1.name;
        const std::string exitLabel = ir[condIndex + 3].dest.name;
        if (labelReferences[exitLabel] != 1) {
          continue;
        }

        const std::size_t guardIndex = loopStart + 2;
        const IRInst& guard = ir[guardIndex];
        const IRInst& skipBranch = ir[guardIndex + 1];
        const IRInst& breakBranch = ir[guardIndex + 2];
        const IRInst& skipLabel = ir[guardIndex + 3];
        if ((guard.op != IROp::LT && guard.op != IROp::LE && guard.op != IROp::GT &&
             guard.op != IROp::GE) ||
            !guard.dest.isLocalVar() || skipBranch.op != IROp::BEQZ || !skipBranch.dest.isLabel() ||
            !skipBranch.src1.isLocalVar() || skipBranch.src1.name != guard.dest.name ||
            breakBranch.op != IROp::BRANCH || !breakBranch.dest.isLabel() ||
            breakBranch.dest.name != exitLabel || skipLabel.op != IROp::LABEL ||
            !skipLabel.dest.isLabel() || skipLabel.dest.name != skipBranch.dest.name ||
            labelReferences[skipLabel.dest.name] != 1) {
          continue;
        }

        const auto nearbyConstant = [&](const Operand& operand) -> std::optional<int> {
          if (operand.isImm()) {
            return operand.immVal;
          }
          if (!operand.isLocalVar()) {
            return std::nullopt;
          }
          for (std::size_t position = loopStart; position > 0; --position) {
            const IRInst& candidate = ir[position - 1];
            if (candidate.dest.isLocalVar() && candidate.dest.name == operand.name) {
              if ((candidate.op == IROp::ASSIGN || candidate.op == IROp::LOCAL_VAR_DECL) &&
                  candidate.src1.isImm()) {
                return candidate.src1.immVal;
              }
              return std::nullopt;
            }
            if (candidate.op == IROp::LABEL || candidate.op == IROp::BRANCH ||
                candidate.op == IROp::BEQZ || candidate.op == IROp::BNEZ ||
                candidate.op == IROp::CALL || candidate.op == IROp::FUNC_BEGIN) {
              break;
            }
          }
          return std::nullopt;
        };

        IROp guardRelation = guard.op;
        Operand guardInduction = guard.src1;
        Operand guardThreshold = guard.src2;
        if ((!guardInduction.isLocalVar() || guardInduction.name != induction) &&
            guardThreshold.isLocalVar() && guardThreshold.name == induction) {
          std::swap(guardInduction, guardThreshold);
          switch (guardRelation) {
          case IROp::LT:
            guardRelation = IROp::GT;
            break;
          case IROp::LE:
            guardRelation = IROp::GE;
            break;
          case IROp::GT:
            guardRelation = IROp::LT;
            break;
          case IROp::GE:
            guardRelation = IROp::LE;
            break;
          default:
            break;
          }
        }
        if (!guardInduction.isLocalVar() || guardInduction.name != induction ||
            (guardRelation != IROp::GE && guardRelation != IROp::GT)) {
          continue;
        }
        const auto loopBound = nearbyConstant(ir[condIndex + 1].src2);
        const auto breakThreshold = nearbyConstant(guardThreshold);
        if (!loopBound || !breakThreshold) {
          continue;
        }

        bool bodySupported = true;
        int inductionWrites = 0;
        for (std::size_t position = guardIndex + 4; position < condIndex && bodySupported;
             ++position) {
          const IRInst& inst = ir[position];
          if (inst.op == IROp::LABEL || inst.op == IROp::BRANCH || inst.op == IROp::BEQZ ||
              inst.op == IROp::BNEZ || inst.op == IROp::RETURN ||
              (inst.src1.isLocalVar() && inst.src1.name == guard.dest.name) ||
              (inst.src2.isLocalVar() && inst.src2.name == guard.dest.name)) {
            bodySupported = false;
            break;
          }
          if (inst.dest.isLocalVar() &&
              ((guardThreshold.isLocalVar() && inst.dest.name == guardThreshold.name) ||
               (ir[condIndex + 1].src2.isLocalVar() &&
                inst.dest.name == ir[condIndex + 1].src2.name))) {
            bodySupported = false;
            break;
          }
          if (inst.dest.isLocalVar() && inst.dest.name == induction) {
            ++inductionWrites;
            bodySupported = inst.op == IROp::ADD && inst.src1.isLocalVar() &&
                            inst.src1.name == induction && inst.src2.isImm() &&
                            inst.src2.immVal == 1 && position + 1 == condIndex;
          }
        }
        if (!bodySupported || inductionWrites != 1) {
          continue;
        }

        const std::int64_t breakExclusive =
            static_cast<std::int64_t>(*breakThreshold) + (guardRelation == IROp::GT ? 1 : 0);
        const std::int64_t effectiveBound =
            std::min<std::int64_t>(static_cast<std::int64_t>(*loopBound), breakExclusive);
        if (effectiveBound < INT32_MIN || effectiveBound > INT32_MAX) {
          continue;
        }
        ir[condIndex + 1].src2 = Operand::imm(static_cast<int>(effectiveBound));
        ir.erase(ir.begin() + static_cast<std::ptrdiff_t>(guardIndex),
                 ir.begin() + static_cast<std::ptrdiff_t>(guardIndex + 4));
        normalizedBreak = true;
        changed = true;
      }
    }
  }

  // Pass 4.5: 删除结果完全不可观察的有限局部循环。
  //
  // `if/else` 中的死状态会反过来保持条件、分支和归纳变量活跃，使普通 DCE
  // 无法拆掉整个控制流环。这里接受直线循环体、只有前向边的局部 CFG，以及
  // 直接跳到本循环唯一退出标签的 break 边，但同时
  // 要求可证明终止的单调归纳变量、无调用/全局状态，并证明循环写入的所有
  // 局部量在退出后均不再使用。严格 `<`/`>` 循环即使边界是运行期不变量也会
  // 在恰好到达边界时退出；非严格比较还要排除 INT32 边界处的步进溢出。
  {
    bool removedLoop = true;
    while (removedLoop) {
      removedLoop = false;
      std::unordered_map<std::string, std::size_t> labelPositions;
      std::unordered_map<std::string, int> labelReferences;
      for (std::size_t position = 0; position < ir.size(); ++position) {
        const IRInst& inst = ir[position];
        if (inst.op == IROp::LABEL && inst.dest.isLabel()) {
          labelPositions[inst.dest.name] = position;
        }
        if ((inst.op == IROp::BRANCH || inst.op == IROp::BEQZ || inst.op == IROp::BNEZ) &&
            inst.dest.isLabel()) {
          ++labelReferences[inst.dest.name];
        }
      }

      for (std::size_t loopStart = 0; loopStart + 1 < ir.size() && !removedLoop; ++loopStart) {
        if (ir[loopStart].op != IROp::BRANCH || !ir[loopStart].dest.isLabel() ||
            ir[loopStart + 1].op != IROp::LABEL || !ir[loopStart + 1].dest.isLabel()) {
          continue;
        }
        const std::string condLabel = ir[loopStart].dest.name;
        const std::string bodyLabel = ir[loopStart + 1].dest.name;
        // continue 会增加条件标签引用；其路径在下面单独验证。循环体仍只能由
        // 规范回边进入，外部入口也会在删除前再次检查。
        if (labelReferences[condLabel] < 1 || labelReferences[bodyLabel] != 1) {
          continue;
        }

        const auto condFound = labelPositions.find(condLabel);
        if (condFound == labelPositions.end() || condFound->second <= loopStart + 1) {
          continue;
        }
        const std::size_t condIndex = condFound->second;
        if (condIndex + 2 >= ir.size()) {
          continue;
        }
        const IRInst& condition = ir[condIndex + 1];
        const IRInst& backedge = ir[condIndex + 2];
        if ((condition.op != IROp::LT && condition.op != IROp::LE && condition.op != IROp::GT &&
             condition.op != IROp::GE) ||
            !condition.dest.isLocalVar() || backedge.op != IROp::BNEZ || !backedge.dest.isLabel() ||
            backedge.dest.name != bodyLabel || !backedge.src1.isLocalVar() ||
            backedge.src1.name != condition.dest.name) {
          continue;
        }

        const auto writtenInBody = [&](const Operand& operand) {
          if (!operand.isLocalVar()) {
            return false;
          }
          for (std::size_t position = loopStart + 2; position < condIndex; ++position) {
            const IRInst& candidate = ir[position];
            if (candidate.dest.isLocalVar() && candidate.dest.name == operand.name &&
                candidate.op != IROp::RETURN && candidate.op != IROp::PARAM) {
              return true;
            }
          }
          return false;
        };
        IROp relation = condition.op;
        Operand inductionOperand = condition.src1;
        Operand boundOperand = condition.src2;
        if (!writtenInBody(inductionOperand) && writtenInBody(boundOperand)) {
          std::swap(inductionOperand, boundOperand);
          switch (relation) {
          case IROp::LT:
            relation = IROp::GT;
            break;
          case IROp::LE:
            relation = IROp::GE;
            break;
          case IROp::GT:
            relation = IROp::LT;
            break;
          case IROp::GE:
            relation = IROp::LE;
            break;
          default:
            break;
          }
        }
        if (!inductionOperand.isLocalVar() || !writtenInBody(inductionOperand)) {
          continue;
        }

        const std::string induction = inductionOperand.name;
        const std::optional<std::string> boundVariable =
            boundOperand.isLocalVar() ? std::optional<std::string>{boundOperand.name}
                                      : std::nullopt;
        const auto findNearbyConstant = [&](const std::string& name) -> std::optional<int> {
          for (std::size_t position = loopStart; position > 0; --position) {
            const IRInst& candidate = ir[position - 1];
            if (candidate.dest.isLocalVar() && candidate.dest.name == name) {
              if ((candidate.op == IROp::ASSIGN || candidate.op == IROp::LOCAL_VAR_DECL) &&
                  candidate.src1.isImm()) {
                return candidate.src1.immVal;
              }
              return std::nullopt;
            }
            if (candidate.op == IROp::LABEL || candidate.op == IROp::BRANCH ||
                candidate.op == IROp::BEQZ || candidate.op == IROp::BNEZ ||
                candidate.op == IROp::CALL || candidate.op == IROp::FUNC_BEGIN) {
              break;
            }
          }
          return std::nullopt;
        };
        std::optional<int> bound;
        if (boundOperand.isImm()) {
          bound = boundOperand.immVal;
        } else if (boundOperand.isLocalVar()) {
          bound = findNearbyConstant(boundOperand.name);
        }
        const bool increasing = relation == IROp::LT || relation == IROp::LE;

        bool bodySupported = true;
        std::unordered_set<std::string> definitions;
        int inductionWrites = 0;
        std::optional<int> inductionStep;
        std::size_t inductionWriteIndex = 0;
        std::size_t firstControlIndex = condIndex;
        std::size_t lastControlIndex = loopStart + 1;
        bool hasContinue = false;
        for (std::size_t position = loopStart + 2; position < condIndex && bodySupported;
             ++position) {
          const IRInst& inst = ir[position];
          if (inst.dest.isGlobalVar() || inst.src1.isGlobalVar() || inst.src2.isGlobalVar()) {
            bodySupported = false;
            break;
          }
          switch (inst.op) {
          case IROp::LOCAL_VAR_DECL:
          case IROp::ASSIGN:
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
            if (!inst.dest.isLocalVar()) {
              bodySupported = false;
              break;
            }
            definitions.insert(inst.dest.name);
            // 迭代次数证明依赖循环界保持不变；即便界值最终不可观察，修改它也
            // 可能改变循环是否终止，不能因为结果为死值就删除。
            if (boundVariable && inst.dest.name == *boundVariable) {
              bodySupported = false;
              break;
            }
            if (inst.dest.name == induction) {
              ++inductionWrites;
              inductionWriteIndex = position;
              std::optional<std::int64_t> candidateStep;
              if (inst.op == IROp::ADD) {
                if (inst.src1.isLocalVar() && inst.src1.name == induction && inst.src2.isImm()) {
                  candidateStep = inst.src2.immVal;
                } else if (inst.src2.isLocalVar() && inst.src2.name == induction &&
                           inst.src1.isImm()) {
                  candidateStep = inst.src1.immVal;
                }
              } else if (inst.op == IROp::SUB && inst.src1.isLocalVar() &&
                         inst.src1.name == induction && inst.src2.isImm()) {
                candidateStep = -static_cast<std::int64_t>(inst.src2.immVal);
              }
              if (!candidateStep || *candidateStep == 0 || *candidateStep < INT32_MIN ||
                  *candidateStep > INT32_MAX ||
                  (inductionStep && *inductionStep != static_cast<int>(*candidateStep))) {
                bodySupported = false;
              } else {
                inductionStep = static_cast<int>(*candidateStep);
              }
            }
            break;
          case IROp::LABEL:
            if (!inst.dest.isLabel()) {
              bodySupported = false;
            }
            firstControlIndex = std::min(firstControlIndex, position);
            lastControlIndex = position;
            break;
          case IROp::BRANCH:
          case IROp::BEQZ:
          case IROp::BNEZ: {
            if (!inst.dest.isLabel()) {
              bodySupported = false;
              break;
            }
            const auto target = labelPositions.find(inst.dest.name);
            // 循环体内部的前向边不影响单位归纳更新。跳到规范退出标签的 break
            // 只会缩短一个已经证明有限的循环；跳到条件标签的 continue 则要求
            // 归纳更新支配循环体内全部控制流。其它外跳与回边保守回退。
            const bool internalForward = target != labelPositions.end() &&
                                         target->second > position && target->second < condIndex;
            const bool exitsLoop = target != labelPositions.end() &&
                                   target->second == condIndex + 3 &&
                                   ir[target->second].op == IROp::LABEL;
            const bool continuesLoop =
                target != labelPositions.end() && target->second == condIndex;
            if (!internalForward && !exitsLoop && !continuesLoop) {
              bodySupported = false;
              break;
            }
            hasContinue = hasContinue || continuesLoop;
            firstControlIndex = std::min(firstControlIndex, position);
            lastControlIndex = position;
            break;
          }
          default:
            bodySupported = false;
            break;
          }
        }
        const bool incrementDominatesControl =
            hasContinue && inductionWriteIndex < firstControlIndex;
        const bool incrementFollowsControl = !hasContinue && inductionWriteIndex > lastControlIndex;
        if (!bodySupported || inductionWrites != 1 || !inductionStep ||
            (!incrementDominatesControl && !incrementFollowsControl)) {
          continue;
        }

        // 常量跨步也能证明终止，但必须保证越过边界的最后一次更新仍在 int32
        // 范围内。未知边界只接受严格比较的单位步进；它会恰好命中边界，不会
        // 先回绕。已知边界则用最坏的最后一个循环内值证明跨越更新不溢出。
        const std::int64_t step = *inductionStep;
        bool terminatingStep = false;
        if (increasing && step > 0) {
          if (bound) {
            const std::int64_t lastUpdateUpper =
                static_cast<std::int64_t>(*bound) + step - (relation == IROp::LT ? 1 : 0);
            terminatingStep = lastUpdateUpper <= INT32_MAX;
          } else {
            terminatingStep = relation == IROp::LT && step == 1;
          }
        } else if (!increasing && step < 0) {
          if (bound) {
            const std::int64_t lastUpdateLower =
                static_cast<std::int64_t>(*bound) + step + (relation == IROp::GT ? 1 : 0);
            terminatingStep = lastUpdateLower >= INT32_MIN;
          } else {
            terminatingStep = relation == IROp::GT && step == -1;
          }
        }
        if (!terminatingStep) {
          continue;
        }

        // 循环区域不能有来自外部的入口；否则直接删除会悬空外部跳转。
        bool externalEntry = false;
        for (std::size_t position = 0; position < ir.size() && !externalEntry; ++position) {
          if (position >= loopStart && position <= condIndex + 2) {
            continue;
          }
          const IRInst& inst = ir[position];
          if ((inst.op != IROp::BRANCH && inst.op != IROp::BEQZ && inst.op != IROp::BNEZ) ||
              !inst.dest.isLabel()) {
            continue;
          }
          const auto target = labelPositions.find(inst.dest.name);
          externalEntry = target != labelPositions.end() && target->second > loopStart &&
                          target->second <= condIndex + 2;
        }
        if (externalEntry) {
          continue;
        }

        std::size_t loopEnd = condIndex + 3;
        bool liveAfter = false;
        for (std::size_t position = loopEnd; position < ir.size() && !liveAfter; ++position) {
          const IRInst& inst = ir[position];
          if (inst.op == IROp::FUNC_BEGIN || inst.op == IROp::FUNC_END) {
            break;
          }
          liveAfter = (inst.src1.isLocalVar() && definitions.count(inst.src1.name) != 0) ||
                      (inst.src2.isLocalVar() && definitions.count(inst.src2.name) != 0) ||
                      ((inst.op == IROp::RETURN || inst.op == IROp::PARAM) &&
                       inst.dest.isLocalVar() && definitions.count(inst.dest.name) != 0);
        }
        if (liveAfter) {
          continue;
        }

        if (loopEnd < ir.size() && ir[loopEnd].op == IROp::LABEL && ir[loopEnd].dest.isLabel() &&
            labelReferences[ir[loopEnd].dest.name] == 0) {
          ++loopEnd;
        }
        ir.erase(ir.begin() + static_cast<std::ptrdiff_t>(loopStart),
                 ir.begin() + static_cast<std::ptrdiff_t>(loopEnd));
        removedLoop = true;
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

    // Pass 5.45: 汇总上界来自外层非负余数的短内层累加循环。
    //
    // 内联 helper 常形成 `bound = outer % k; while (j < bound) sum += a*j+b`。
    // bound 虽不是编译期常量，但已证明落在 [0,k-1]，可改写为三角和；改写后
    // 外层看到的又是普通周期表达式，可继续由 Pass 5.56 消除。这里只接受被
    // 规范外层 `i += 1` 包围的正模余数、小上界和无控制流/调用的内层循环。
    {
      constexpr int kMaxRuntimeBound = 1024;
      struct LinearValue {
        std::uint32_t coefficient = 0;
        std::uint32_t constant = 0;
      };
      struct RuntimeAccumulator {
        std::string name;
        LinearValue delta;
      };
      struct RuntimeValue {
        std::uint32_t coefficient = 0;
        std::uint32_t constant = 0;
        std::unordered_map<std::string, std::uint32_t> stateCoefficients;
      };

      for (int summaryRound = 0; summaryRound < 8; ++summaryRound) {
        std::unordered_map<std::string, int> labelReferences;
        std::unordered_map<std::string, std::size_t> labelPositions;
        for (std::size_t index = 0; index < ir.size(); ++index) {
          const IRInst& inst = ir[index];
          if (inst.op == IROp::LABEL && inst.dest.isLabel()) {
            labelPositions[inst.dest.name] = index;
          }
          if ((inst.op == IROp::BRANCH || inst.op == IROp::BEQZ || inst.op == IROp::BNEZ) &&
              inst.dest.isLabel()) {
            ++labelReferences[inst.dest.name];
          }
        }

        const auto previousDefinition = [&](const std::string& name,
                                            std::size_t before) -> std::optional<std::size_t> {
          for (std::size_t position = before; position > 0; --position) {
            const IRInst& candidate = ir[position - 1];
            if (candidate.op == IROp::FUNC_BEGIN) {
              break;
            }
            if (candidate.dest.isLocalVar() && candidate.dest.name == name &&
                candidate.op != IROp::RETURN && candidate.op != IROp::PARAM &&
                !(candidate.op == IROp::LOCAL_VAR_DECL && candidate.src1.isNone())) {
              return position - 1;
            }
          }
          return std::nullopt;
        };
        const auto nearbyConstant = [&](std::size_t before,
                                        const std::string& name) -> std::optional<int> {
          for (std::size_t position = before; position > 0; --position) {
            const IRInst& candidate = ir[position - 1];
            if (candidate.dest.isLocalVar() && candidate.dest.name == name) {
              if ((candidate.op == IROp::ASSIGN || candidate.op == IROp::LOCAL_VAR_DECL) &&
                  candidate.src1.isImm()) {
                return candidate.src1.immVal;
              }
              return std::nullopt;
            }
            if (candidate.op == IROp::LABEL || candidate.op == IROp::BRANCH ||
                candidate.op == IROp::BEQZ || candidate.op == IROp::BNEZ ||
                candidate.op == IROp::CALL || candidate.op == IROp::FUNC_BEGIN) {
              break;
            }
          }
          return std::nullopt;
        };

        bool summarized = false;
        for (std::size_t loopStart = ir.size(); loopStart-- > 0 && !summarized;) {
          if (ir[loopStart].op != IROp::BRANCH || !ir[loopStart].dest.isLabel() ||
              loopStart + 1 >= ir.size() || ir[loopStart + 1].op != IROp::LABEL ||
              !ir[loopStart + 1].dest.isLabel()) {
            continue;
          }
          const std::string condLabel = ir[loopStart].dest.name;
          const std::string bodyLabel = ir[loopStart + 1].dest.name;
          if (labelReferences[condLabel] != 1 || labelReferences[bodyLabel] != 1) {
            continue;
          }
          const auto condPosition = labelPositions.find(condLabel);
          if (condPosition == labelPositions.end() || condPosition->second <= loopStart + 2) {
            continue;
          }
          const std::size_t condIndex = condPosition->second;
          if (condIndex + 2 >= ir.size()) {
            continue;
          }
          const IRInst& condition = ir[condIndex + 1];
          const IRInst& backedge = ir[condIndex + 2];
          if (condition.op != IROp::LT || !condition.dest.isLocalVar() ||
              !condition.src1.isLocalVar() || !condition.src2.isLocalVar() ||
              backedge.op != IROp::BNEZ || !backedge.dest.isLabel() ||
              backedge.dest.name != bodyLabel || !backedge.src1.isLocalVar() ||
              backedge.src1.name != condition.dest.name) {
            continue;
          }
          const std::string induction = condition.src1.name;
          const std::string bound = condition.src2.name;
          const auto innerInitial = nearbyConstant(loopStart, induction);
          if (!innerInitial || *innerInitial != 0) {
            continue;
          }

          // 找到线性区间上包围当前内层循环的最近规范外层循环，并证明其归纳
          // 变量从非负常量开始、只执行一次 +1 更新且不会回绕。
          std::optional<std::string> outerInduction;
          std::optional<std::size_t> outerLoopStart;
          for (std::size_t candidate = loopStart; candidate-- > 0;) {
            if (ir[candidate].op == IROp::FUNC_BEGIN) {
              break;
            }
            if (ir[candidate].op != IROp::BRANCH || !ir[candidate].dest.isLabel() ||
                candidate + 1 >= ir.size() || ir[candidate + 1].op != IROp::LABEL ||
                !ir[candidate + 1].dest.isLabel()) {
              continue;
            }
            const auto enclosingCond = labelPositions.find(ir[candidate].dest.name);
            if (enclosingCond == labelPositions.end() || enclosingCond->second <= condIndex + 2 ||
                enclosingCond->second + 2 >= ir.size()) {
              continue;
            }
            const IRInst& outerCondition = ir[enclosingCond->second + 1];
            const IRInst& outerBackedge = ir[enclosingCond->second + 2];
            if ((outerCondition.op != IROp::LT && outerCondition.op != IROp::LE) ||
                !outerCondition.src1.isLocalVar() || !outerCondition.dest.isLocalVar() ||
                outerBackedge.op != IROp::BNEZ || !outerBackedge.dest.isLabel() ||
                outerBackedge.dest.name != ir[candidate + 1].dest.name ||
                !outerBackedge.src1.isLocalVar() ||
                outerBackedge.src1.name != outerCondition.dest.name) {
              continue;
            }
            const auto start = nearbyConstant(candidate, outerCondition.src1.name);
            std::optional<int> upper;
            if (outerCondition.src2.isImm()) {
              upper = outerCondition.src2.immVal;
            } else if (outerCondition.src2.isLocalVar()) {
              upper = nearbyConstant(candidate, outerCondition.src2.name);
              if (!upper) {
                const auto definition = previousDefinition(outerCondition.src2.name, candidate);
                if (definition &&
                    (ir[*definition].op == IROp::ASSIGN ||
                     ir[*definition].op == IROp::LOCAL_VAR_DECL) &&
                    ir[*definition].src1.isImm()) {
                  upper = ir[*definition].src1.immVal;
                }
              }
            }
            if (!start || !upper || *start < 0 ||
                (outerCondition.op == IROp::LE && *upper == INT32_MAX)) {
              continue;
            }
            int writes = 0;
            bool canonicalWrite = false;
            for (std::size_t position = candidate + 2; position < enclosingCond->second;
                 ++position) {
              const IRInst& inst = ir[position];
              if (!inst.dest.isLocalVar() || inst.dest.name != outerCondition.src1.name) {
                continue;
              }
              ++writes;
              canonicalWrite = inst.op == IROp::ADD && inst.src1.isLocalVar() &&
                               inst.src1.name == outerCondition.src1.name && inst.src2.isImm() &&
                               inst.src2.immVal == 1;
            }
            if (writes == 1 && canonicalWrite) {
              outerInduction = outerCondition.src1.name;
              outerLoopStart = candidate;
              break;
            }
          }
          if (!outerInduction || !outerLoopStart) {
            continue;
          }

          // 证明 inner bound 是 `outer % positive_constant`。同时接受前面 Pass 0d
          // 生成的 `outer - (outer / k) * k` 三指令形式。
          int maximumBound = -1;
          std::string resolvedBound = bound;
          std::size_t boundBefore = loopStart;
          for (int copies = 0; copies < 4; ++copies) {
            const auto definition = previousDefinition(resolvedBound, boundBefore);
            if (!definition) {
              break;
            }
            const IRInst& inst = ir[*definition];
            if ((inst.op == IROp::ASSIGN || inst.op == IROp::LOCAL_VAR_DECL) &&
                inst.src1.isLocalVar()) {
              resolvedBound = inst.src1.name;
              boundBefore = *definition;
              continue;
            }
            if (inst.op == IROp::MOD && inst.src1.isLocalVar() &&
                inst.src1.name == *outerInduction && inst.src2.isImm() && inst.src2.immVal > 1 &&
                inst.src2.immVal <= kMaxRuntimeBound && *definition > *outerLoopStart + 1) {
              maximumBound = inst.src2.immVal - 1;
            } else if (inst.op == IROp::SUB && inst.src1.isLocalVar() &&
                       inst.src1.name == *outerInduction && inst.src2.isLocalVar() &&
                       *definition > *outerLoopStart + 1) {
              const auto productDefinition = previousDefinition(inst.src2.name, *definition);
              if (productDefinition && *productDefinition > *outerLoopStart + 1) {
                const IRInst& product = ir[*productDefinition];
                const Operand* quotientOperand = nullptr;
                const Operand* divisorOperand = nullptr;
                if (product.op == IROp::MUL && product.src1.isLocalVar() && product.src2.isImm()) {
                  quotientOperand = &product.src1;
                  divisorOperand = &product.src2;
                } else if (product.op == IROp::MUL && product.src2.isLocalVar() &&
                           product.src1.isImm()) {
                  quotientOperand = &product.src2;
                  divisorOperand = &product.src1;
                }
                if (quotientOperand != nullptr && divisorOperand->immVal > 1 &&
                    divisorOperand->immVal <= kMaxRuntimeBound) {
                  const auto quotientDefinition =
                      previousDefinition(quotientOperand->name, *productDefinition);
                  if (quotientDefinition && *quotientDefinition > *outerLoopStart + 1) {
                    const IRInst& quotient = ir[*quotientDefinition];
                    if (quotient.op == IROp::DIV && quotient.src1.isLocalVar() &&
                        quotient.src1.name == *outerInduction && quotient.src2.isImm() &&
                        quotient.src2.immVal == divisorOperand->immVal) {
                      maximumBound = divisorOperand->immVal - 1;
                    }
                  }
                }
              }
            }
            break;
          }
          if (maximumBound < 0 ||
              static_cast<std::int64_t>(maximumBound) * maximumBound > INT32_MAX) {
            continue;
          }

          std::unordered_map<std::string, RuntimeValue> values;
          values[induction] = {1, 0, {}};
          const auto valueOf = [&](const Operand& operand) -> std::optional<RuntimeValue> {
            if (operand.isImm()) {
              return RuntimeValue{0, static_cast<std::uint32_t>(operand.immVal), {}};
            }
            if (operand.isLocalVar()) {
              const auto found = values.find(operand.name);
              if (found != values.end()) {
                return found->second;
              }
              RuntimeValue state;
              state.stateCoefficients[operand.name] = 1;
              return state;
            }
            return std::nullopt;
          };
          const auto combineValues = [](const RuntimeValue& lhs, const RuntimeValue& rhs,
                                        bool subtract) {
            RuntimeValue result{
                subtract ? lhs.coefficient - rhs.coefficient : lhs.coefficient + rhs.coefficient,
                subtract ? lhs.constant - rhs.constant : lhs.constant + rhs.constant,
                lhs.stateCoefficients};
            for (const auto& [name, coefficient] : rhs.stateCoefficients) {
              auto& destination = result.stateCoefficients[name];
              destination = subtract ? destination - coefficient : destination + coefficient;
              if (destination == 0) {
                result.stateCoefficients.erase(name);
              }
            }
            return result;
          };
          const auto scaleValue = [](RuntimeValue value, std::uint32_t factor) {
            value.coefficient =
                static_cast<std::uint32_t>(static_cast<std::uint64_t>(value.coefficient) * factor);
            value.constant =
                static_cast<std::uint32_t>(static_cast<std::uint64_t>(value.constant) * factor);
            for (auto iterator = value.stateCoefficients.begin();
                 iterator != value.stateCoefficients.end();) {
              iterator->second =
                  static_cast<std::uint32_t>(static_cast<std::uint64_t>(iterator->second) * factor);
              if (iterator->second == 0) {
                iterator = value.stateCoefficients.erase(iterator);
              } else {
                ++iterator;
              }
            }
            return value;
          };
          bool bodySupported = true;
          bool inductionIncremented = false;
          int inductionWrites = 0;
          std::unordered_set<std::string> bodyDefinitions;
          for (std::size_t position = loopStart + 2; position < condIndex && bodySupported;
               ++position) {
            const IRInst& inst = ir[position];
            if (inst.op == IROp::LOCAL_VAR_DECL && inst.src1.isNone()) {
              if (inst.dest.isLocalVar()) {
                bodyDefinitions.insert(inst.dest.name);
              }
              continue;
            }
            if (!inst.dest.isLocalVar() || inductionIncremented) {
              bodySupported = false;
              break;
            }
            bodyDefinitions.insert(inst.dest.name);
            // 循环体若修改上界，实际迭代次数不再等于进入循环时的 bound。
            if (inst.dest.name == bound) {
              bodySupported = false;
              break;
            }
            if (inst.dest.name == induction) {
              ++inductionWrites;
              inductionIncremented = inductionWrites == 1 && inst.op == IROp::ADD &&
                                     inst.src1.isLocalVar() && inst.src1.name == induction &&
                                     inst.src2.isImm() && inst.src2.immVal == 1;
              if (!inductionIncremented) {
                bodySupported = false;
              }
              continue;
            }
            const auto lhs = valueOf(inst.src1);
            const auto rhs = valueOf(inst.src2);
            std::optional<RuntimeValue> result;
            if (inst.op == IROp::ASSIGN || inst.op == IROp::LOCAL_VAR_DECL) {
              result = lhs;
            } else if ((inst.op == IROp::ADD || inst.op == IROp::SUB) && lhs && rhs) {
              result = combineValues(*lhs, *rhs, inst.op == IROp::SUB);
            } else if (inst.op == IROp::MUL && lhs && rhs) {
              const bool lhsConstant = lhs->coefficient == 0 && lhs->stateCoefficients.empty();
              const bool rhsConstant = rhs->coefficient == 0 && rhs->stateCoefficients.empty();
              if (lhsConstant || rhsConstant) {
                result =
                    lhsConstant ? scaleValue(*rhs, lhs->constant) : scaleValue(*lhs, rhs->constant);
              }
            }
            if (!result) {
              bodySupported = false;
              break;
            }
            values[inst.dest.name] = *result;
          }
          if (!bodySupported || inductionWrites != 1 || !inductionIncremented) {
            continue;
          }

          const std::size_t loopEnd = condIndex + 3;
          const auto usedAfterLoop = [&](const std::string& name) {
            for (std::size_t position = loopEnd; position < ir.size(); ++position) {
              const IRInst& inst = ir[position];
              if (inst.op == IROp::FUNC_BEGIN || inst.op == IROp::FUNC_END) {
                break;
              }
              if ((inst.src1.isLocalVar() && inst.src1.name == name) ||
                  (inst.src2.isLocalVar() && inst.src2.name == name) ||
                  ((inst.op == IROp::RETURN || inst.op == IROp::PARAM) && inst.dest.isLocalVar() &&
                   inst.dest.name == name)) {
                return true;
              }
            }
            return false;
          };
          bool invalidLiveResult = false;
          std::vector<RuntimeAccumulator> accumulators;
          for (const std::string& name : bodyDefinitions) {
            if (name == induction || !usedAfterLoop(name)) {
              continue;
            }
            if (isCompilerTemp(name)) {
              invalidLiveResult = true;
              break;
            }
            const auto finalValue = values.find(name);
            if (finalValue == values.end() || finalValue->second.stateCoefficients.size() != 1) {
              invalidLiveResult = true;
              break;
            }
            const auto self = finalValue->second.stateCoefficients.find(name);
            if (self == finalValue->second.stateCoefficients.end() || self->second != 1) {
              invalidLiveResult = true;
              break;
            }
            accumulators.push_back(
                {name, {finalValue->second.coefficient, finalValue->second.constant}});
          }
          if (invalidLiveResult || accumulators.empty()) {
            continue;
          }

          std::vector<IRInst> replacement;
          const bool needsTriangle = std::any_of(accumulators.begin(), accumulators.end(),
                                                 [](const RuntimeAccumulator& accumulator) {
                                                   return accumulator.delta.coefficient != 0;
                                                 });
          Operand triangle = Operand::none();
          if (needsTriangle) {
            const Operand minusOne = Operand::localVar(newTemp());
            const Operand product = Operand::localVar(newTemp());
            triangle = Operand::localVar(newTemp());
            replacement.emplace_back(IROp::SUB, minusOne, Operand::localVar(bound),
                                     Operand::imm(1));
            replacement.emplace_back(IROp::MUL, product, Operand::localVar(bound), minusOne);
            replacement.emplace_back(IROp::DIV, triangle, product, Operand::imm(2));
          }
          for (const RuntimeAccumulator& accumulator : accumulators) {
            if (!usedAfterLoop(accumulator.name)) {
              continue;
            }
            const Operand destination = Operand::localVar(accumulator.name);
            if (accumulator.delta.coefficient != 0) {
              Operand term = triangle;
              if (accumulator.delta.coefficient != 1) {
                term = Operand::localVar(newTemp());
                replacement.emplace_back(
                    IROp::MUL, term, triangle,
                    Operand::imm(static_cast<std::int32_t>(accumulator.delta.coefficient)));
              }
              replacement.emplace_back(IROp::ADD, destination, destination, term);
            }
            if (accumulator.delta.constant != 0) {
              Operand term = Operand::localVar(bound);
              if (accumulator.delta.constant != 1) {
                term = Operand::localVar(newTemp());
                replacement.emplace_back(
                    IROp::MUL, term, Operand::localVar(bound),
                    Operand::imm(static_cast<std::int32_t>(accumulator.delta.constant)));
              }
              replacement.emplace_back(IROp::ADD, destination, destination, term);
            }
          }
          if (usedAfterLoop(induction)) {
            replacement.emplace_back(IROp::ASSIGN, Operand::localVar(induction),
                                     Operand::localVar(bound), Operand::none());
          }

          std::size_t eraseEnd = loopEnd;
          if (eraseEnd < ir.size() && ir[eraseEnd].op == IROp::LABEL &&
              ir[eraseEnd].dest.isLabel() && labelReferences[ir[eraseEnd].dest.name] == 0) {
            ++eraseEnd;
          }
          ir.erase(ir.begin() + static_cast<std::ptrdiff_t>(loopStart),
                   ir.begin() + static_cast<std::ptrdiff_t>(eraseEnd));
          ir.insert(ir.begin() + static_cast<std::ptrdiff_t>(loopStart), replacement.begin(),
                    replacement.end());
          summarized = true;
          changed = true;
        }
        if (!summarized) {
          break;
        }
      }
    }

    // Pass 5.5: 汇总常数边界的直线内层累加循环。
    //
    // 与 Pass 6 要求累加器初值为编译期常量不同，这里允许初值来自外层循环：
    //   while (j < 1000) { sum = sum + i + j; j = j + 1; }
    // 可安全改写为：
    //   sum = sum + i * 1000 + 499500;
    // 下一轮迭代优化即可继续处理外层循环。该变换只做仿射符号证明，不执行源程序；
    // 循环体含分支、调用、全局状态或交叉递推时一律回退。
    {
      struct LinearExpr {
        long long constant = 0;
        std::unordered_map<std::string, long long> coeffs;
      };
      struct AccSummary {
        std::string name;
        int constantDelta = 0;
        std::vector<std::pair<std::string, int>> invariantDeltas;
      };

      const auto findNearbyConstant = [&](std::size_t loopStart, const std::string& name,
                                          int& value) {
        for (std::size_t pos = loopStart; pos > 0; --pos) {
          const IRInst& candidate = ir[pos - 1];
          if (candidate.dest.isLocalVar() && candidate.dest.name == name) {
            if ((candidate.op == IROp::ASSIGN || candidate.op == IROp::LOCAL_VAR_DECL) &&
                candidate.src1.isImm()) {
              value = candidate.src1.immVal;
              return true;
            }
            return false;
          }
          if (candidate.op == IROp::LABEL || candidate.op == IROp::BRANCH ||
              candidate.op == IROp::BEQZ || candidate.op == IROp::BNEZ ||
              candidate.op == IROp::CALL || candidate.op == IROp::FUNC_BEGIN) {
            break;
          }
        }
        return false;
      };
      const auto findUniqueConstant = [&](std::size_t before, const std::string& name, int& value) {
        int definitions = 0;
        bool constantDefinition = false;
        std::size_t functionBegin = before;
        while (functionBegin > 0 && ir[functionBegin - 1].op != IROp::FUNC_BEGIN) {
          --functionBegin;
        }
        for (std::size_t pos = functionBegin; pos < before; ++pos) {
          const IRInst& candidate = ir[pos];
          if (!candidate.dest.isLocalVar() || candidate.dest.name != name ||
              candidate.op == IROp::RETURN || candidate.op == IROp::PARAM ||
              (candidate.op == IROp::LOCAL_VAR_DECL && candidate.src1.isNone())) {
            continue;
          }
          ++definitions;
          constantDefinition =
              (candidate.op == IROp::ASSIGN || candidate.op == IROp::LOCAL_VAR_DECL) &&
              candidate.src1.isImm();
          if (constantDefinition) {
            value = candidate.src1.immVal;
          }
        }
        // 还必须确认循环之后没有其它定义；否则该名字可能是跨回边状态而非不变量。
        for (std::size_t pos = before; pos < ir.size(); ++pos) {
          if (ir[pos].op == IROp::FUNC_END) {
            break;
          }
          if (ir[pos].dest.isLocalVar() && ir[pos].dest.name == name &&
              ir[pos].op != IROp::RETURN && ir[pos].op != IROp::PARAM &&
              !(ir[pos].op == IROp::LOCAL_VAR_DECL && ir[pos].src1.isNone())) {
            ++definitions;
          }
        }
        return definitions == 1 && constantDefinition;
      };
      const auto usedAfterLoop = [&](std::size_t begin, const std::string& name) {
        for (std::size_t pos = begin; pos < ir.size(); ++pos) {
          const IRInst& inst = ir[pos];
          if (inst.op == IROp::FUNC_BEGIN || inst.op == IROp::FUNC_END) {
            break;
          }
          if ((inst.src1.isLocalVar() && inst.src1.name == name) ||
              (inst.src2.isLocalVar() && inst.src2.name == name) ||
              ((inst.op == IROp::RETURN || inst.op == IROp::PARAM) && inst.dest.isLocalVar() &&
               inst.dest.name == name)) {
            return true;
          }
        }
        return false;
      };
      const auto wrappedProduct = [](long long lhs, long long rhs) {
        const uint32_t product = static_cast<uint32_t>(lhs) * static_cast<uint32_t>(rhs);
        return static_cast<int32_t>(product);
      };
      const auto wrappedAdd = [](int lhs, int rhs) {
        return static_cast<int32_t>(static_cast<uint32_t>(lhs) + static_cast<uint32_t>(rhs));
      };

      for (int summaryRound = 0; summaryRound < 4; ++summaryRound) {
        std::vector<IRInst> summarized;
        std::size_t loopStart = 0;
        bool summarizedAny = false;
        while (loopStart < ir.size()) {
          bool summarizedHere = false;
          if (ir[loopStart].op == IROp::BRANCH && loopStart + 1 < ir.size() &&
              ir[loopStart + 1].op == IROp::LABEL) {
            const std::string condLabel = ir[loopStart].dest.name;
            const std::string bodyLabel = ir[loopStart + 1].dest.name;
            std::size_t condIndex = loopStart + 2;
            while (condIndex < ir.size() &&
                   !(ir[condIndex].op == IROp::LABEL && ir[condIndex].dest.name == condLabel)) {
              ++condIndex;
            }
            const std::size_t conditionInst = condIndex + 1;
            const std::size_t backedge = condIndex + 2;
            if (backedge < ir.size() && condIndex > loopStart + 2 &&
                ir[backedge].op == IROp::BNEZ && ir[backedge].dest.name == bodyLabel &&
                (ir[conditionInst].op == IROp::LT || ir[conditionInst].op == IROp::LE) &&
                ir[conditionInst].dest.isLocalVar() && ir[backedge].src1.isLocalVar() &&
                ir[backedge].src1.name == ir[conditionInst].dest.name &&
                ir[conditionInst].src1.isLocalVar()) {
              const IRInst& condition = ir[conditionInst];
              const std::string induction = condition.src1.name;
              bool externalEntry = false;
              for (std::size_t pos = 0; pos < ir.size() && !externalEntry; ++pos) {
                if (pos >= loopStart && pos <= backedge) {
                  continue;
                }
                const IRInst& inst = ir[pos];
                externalEntry =
                    (inst.op == IROp::BRANCH || inst.op == IROp::BEQZ || inst.op == IROp::BNEZ) &&
                    inst.dest.isLabel() &&
                    (inst.dest.name == condLabel || inst.dest.name == bodyLabel);
              }

              int initial = 0;
              int upper = 0;
              bool upperKnown = condition.src2.isImm();
              if (upperKnown) {
                upper = condition.src2.immVal;
              } else if (condition.src2.isLocalVar()) {
                upperKnown = findNearbyConstant(loopStart, condition.src2.name, upper) ||
                             findUniqueConstant(loopStart, condition.src2.name, upper);
              }
              const bool initialKnown = findNearbyConstant(loopStart, induction, initial);

              long long trips = 0;
              if (initialKnown && upperKnown) {
                trips = static_cast<long long>(upper) - initial;
                if (condition.op == IROp::LE) {
                  ++trips;
                }
                trips = std::max(0LL, trips);
              }
              // 超过 INT_MAX 次意味着 32 位归纳变量可能回绕；不在这里证明终止性。
              bool bodyOk = !externalEntry && initialKnown && upperKnown && trips <= INT32_MAX;
              if (condition.op == IROp::LE && upper == INT32_MAX && initial <= upper) {
                bodyOk = false;
              }

              std::unordered_map<std::string, LinearExpr> values;
              std::unordered_set<std::string> written;
              const auto linearOf = [&](const Operand& operand) -> std::optional<LinearExpr> {
                if (operand.isImm()) {
                  LinearExpr value;
                  value.constant = operand.immVal;
                  return value;
                }
                if (!operand.isLocalVar()) {
                  return std::nullopt;
                }
                const auto found = values.find(operand.name);
                if (found != values.end()) {
                  return found->second;
                }
                LinearExpr value;
                value.coeffs[operand.name] = 1;
                return value;
              };
              const auto addScaled = [](LinearExpr& result, const LinearExpr& value,
                                        long long scale) {
                long long next = 0;
                if (__builtin_mul_overflow(value.constant, scale, &next) ||
                    __builtin_add_overflow(result.constant, next, &result.constant)) {
                  return false;
                }
                for (const auto& term : value.coeffs) {
                  long long scaled = 0;
                  long long combined = 0;
                  if (__builtin_mul_overflow(term.second, scale, &scaled) ||
                      __builtin_add_overflow(result.coeffs[term.first], scaled, &combined)) {
                    return false;
                  }
                  if (combined == 0) {
                    result.coeffs.erase(term.first);
                  } else {
                    result.coeffs[term.first] = combined;
                  }
                }
                return true;
              };

              for (std::size_t pos = loopStart + 2; pos < condIndex && bodyOk; ++pos) {
                const IRInst& inst = ir[pos];
                if (inst.op == IROp::LOCAL_VAR_DECL && inst.src1.isNone()) {
                  continue;
                }
                if (!inst.dest.isLocalVar()) {
                  bodyOk = false;
                  break;
                }
                const auto lhs = linearOf(inst.src1);
                const auto rhs = linearOf(inst.src2);
                LinearExpr result;
                if (inst.op == IROp::ASSIGN || inst.op == IROp::LOCAL_VAR_DECL) {
                  if (!lhs) {
                    bodyOk = false;
                    break;
                  }
                  result = *lhs;
                } else if (inst.op == IROp::ADD || inst.op == IROp::SUB) {
                  if (!lhs || !rhs || !addScaled(result, *lhs, 1) ||
                      !addScaled(result, *rhs, inst.op == IROp::ADD ? 1 : -1)) {
                    bodyOk = false;
                    break;
                  }
                } else if (inst.op == IROp::MUL) {
                  if (!lhs || !rhs) {
                    bodyOk = false;
                    break;
                  }
                  if (lhs->coeffs.empty()) {
                    if (!addScaled(result, *rhs, lhs->constant)) {
                      bodyOk = false;
                      break;
                    }
                  } else if (rhs->coeffs.empty()) {
                    if (!addScaled(result, *lhs, rhs->constant)) {
                      bodyOk = false;
                      break;
                    }
                  } else {
                    bodyOk = false;
                    break;
                  }
                } else {
                  bodyOk = false;
                  break;
                }
                values[inst.dest.name] = std::move(result);
                written.insert(inst.dest.name);
              }

              if (bodyOk) {
                if (condition.src2.isLocalVar() && written.count(condition.src2.name) != 0) {
                  bodyOk = false;
                }
                const auto inductionValue = values.find(induction);
                bodyOk = bodyOk && inductionValue != values.end() &&
                         inductionValue->second.constant == 1 &&
                         inductionValue->second.coeffs.size() == 1 &&
                         inductionValue->second.coeffs.find(induction) !=
                             inductionValue->second.coeffs.end() &&
                         inductionValue->second.coeffs.at(induction) == 1;
              }

              std::vector<AccSummary> accumulators;
              if (bodyOk) {
                const long long inductionSum = trips * initial + trips * (trips - 1) / 2;
                for (const std::string& name : written) {
                  if (name == induction || !usedAfterLoop(backedge + 1, name)) {
                    continue;
                  }
                  const auto finalValue = values.find(name);
                  if (finalValue == values.end()) {
                    bodyOk = false;
                    break;
                  }
                  LinearExpr delta = finalValue->second;
                  const auto self = delta.coeffs.find(name);
                  if (self == delta.coeffs.end() || self->second != 1) {
                    bodyOk = false;
                    break;
                  }
                  delta.coeffs.erase(self);

                  long long inductionCoeff = 0;
                  const auto inductionTerm = delta.coeffs.find(induction);
                  if (inductionTerm != delta.coeffs.end()) {
                    inductionCoeff = inductionTerm->second;
                    delta.coeffs.erase(inductionTerm);
                  }

                  AccSummary summary;
                  summary.name = name;
                  const int repeatedConstant = wrappedProduct(delta.constant, trips);
                  const int repeatedInduction = wrappedProduct(inductionCoeff, inductionSum);
                  summary.constantDelta = wrappedAdd(repeatedConstant, repeatedInduction);
                  for (const auto& term : delta.coeffs) {
                    if (written.count(term.first) != 0) {
                      bodyOk = false;
                      break;
                    }
                    const int coefficient = wrappedProduct(term.second, trips);
                    if (coefficient != 0) {
                      summary.invariantDeltas.push_back({term.first, coefficient});
                    }
                  }
                  if (!bodyOk) {
                    break;
                  }
                  accumulators.push_back(std::move(summary));
                }
                bodyOk = bodyOk && !accumulators.empty();
                bool requiresRuntimeSummary = false;
                for (const auto& accumulator : accumulators) {
                  int ignoredInitial = 0;
                  if (!accumulator.invariantDeltas.empty() ||
                      !findNearbyConstant(loopStart, accumulator.name, ignoredInitial)) {
                    requiresRuntimeSummary = true;
                    break;
                  }
                }
                // 全部累加器都有常量初值时交给后面的 Pass 6；它还能连同循环后
                // 的纯表达式一起折叠成直接返回值，代码形状更优。
                bodyOk = bodyOk && requiresRuntimeSummary;
              }

              if (bodyOk) {
                for (const auto& accumulator : accumulators) {
                  const Operand dest = Operand::localVar(accumulator.name);
                  int constantInitial = 0;
                  if (accumulator.invariantDeltas.empty() &&
                      findNearbyConstant(loopStart, accumulator.name, constantInitial)) {
                    summarized.push_back(
                        IRInst(IROp::ASSIGN, dest,
                               Operand::imm(wrappedAdd(constantInitial, accumulator.constantDelta)),
                               Operand::none()));
                    continue;
                  }
                  for (const auto& term : accumulator.invariantDeltas) {
                    const Operand source = Operand::localVar(term.first);
                    if (term.second == 1) {
                      summarized.push_back(IRInst(IROp::ADD, dest, dest, source));
                    } else if (term.second == -1) {
                      summarized.push_back(IRInst(IROp::SUB, dest, dest, source));
                    } else {
                      const Operand product = Operand::localVar(newTemp());
                      summarized.push_back(
                          IRInst(IROp::MUL, product, source, Operand::imm(term.second)));
                      summarized.push_back(IRInst(IROp::ADD, dest, dest, product));
                    }
                  }
                  if (accumulator.constantDelta != 0) {
                    summarized.push_back(
                        IRInst(IROp::ADD, dest, dest, Operand::imm(accumulator.constantDelta)));
                  }
                }
                if (usedAfterLoop(backedge + 1, induction)) {
                  const int finalInduction =
                      trips == 0 ? initial
                                 : static_cast<int>(static_cast<long long>(initial) + trips);
                  summarized.push_back(IRInst(IROp::ASSIGN, Operand::localVar(induction),
                                              Operand::imm(finalInduction), Operand::none()));
                }
                loopStart = backedge + 1;
                if (loopStart < ir.size() && ir[loopStart].op == IROp::LABEL &&
                    ir[loopStart].dest.isLabel()) {
                  bool referenced = false;
                  for (const auto& inst : ir) {
                    if ((inst.op == IROp::BRANCH || inst.op == IROp::BEQZ ||
                         inst.op == IROp::BNEZ) &&
                        inst.dest.isLabel() && inst.dest.name == ir[loopStart].dest.name) {
                      referenced = true;
                      break;
                    }
                  }
                  if (!referenced) {
                    ++loopStart;
                  }
                }
                summarizedHere = true;
                summarizedAny = true;
                changed = true;
              }
            }
          }
          if (!summarizedHere) {
            summarized.push_back(ir[loopStart]);
            ++loopStart;
          }
        }
        if (!summarizedAny) {
          break;
        }
        ir = std::move(summarized);
        changed = true;
      }
    }

    // Pass 5.55: 汇总最高三次的归纳变量多项式累加循环。
    //
    // 对 `sum = sum + P(i); i = i + 1`，其中 P 的次数不超过 3，利用
    // Faulhaber 求和把整个循环替换为一次 32 位环绕加法。这里只构造并计算
    // 符号闭式，不逐轮执行源程序；多次写同一累加器、交叉状态、分支、调用、
    // 全局状态、可变上界或非 +1 归纳均保守回退。
    {
      struct Polynomial {
        std::array<std::uint32_t, 4> coeff{};
      };
      struct PolynomialExpr {
        Polynomial polynomial;
        std::unordered_map<std::string, std::uint32_t> stateCoefficients;
      };

      const auto scalePolynomial = [](const Polynomial& value, std::uint32_t factor) {
        Polynomial result;
        for (std::size_t degree = 0; degree < result.coeff.size(); ++degree) {
          result.coeff[degree] =
              static_cast<std::uint32_t>(static_cast<std::uint64_t>(value.coeff[degree]) * factor);
        }
        return result;
      };
      const auto multiplyPolynomial = [](const Polynomial& lhs,
                                         const Polynomial& rhs) -> std::optional<Polynomial> {
        Polynomial result;
        for (std::size_t left = 0; left < lhs.coeff.size(); ++left) {
          if (lhs.coeff[left] == 0) {
            continue;
          }
          for (std::size_t right = 0; right < rhs.coeff.size(); ++right) {
            if (rhs.coeff[right] == 0) {
              continue;
            }
            if (left + right >= result.coeff.size()) {
              return std::nullopt;
            }
            result.coeff[left + right] += static_cast<std::uint32_t>(
                static_cast<std::uint64_t>(lhs.coeff[left]) * rhs.coeff[right]);
          }
        }
        return result;
      };
      const auto constantPolynomial = [](int value) {
        Polynomial result;
        result.coeff[0] = static_cast<std::uint32_t>(value);
        return result;
      };
      const auto constantValue =
          [](const PolynomialExpr& expression) -> std::optional<std::uint32_t> {
        if (!expression.stateCoefficients.empty()) {
          return std::nullopt;
        }
        for (std::size_t degree = 1; degree < expression.polynomial.coeff.size(); ++degree) {
          if (expression.polynomial.coeff[degree] != 0) {
            return std::nullopt;
          }
        }
        return expression.polynomial.coeff[0];
      };
      const auto combineExpression = [](const PolynomialExpr& lhs, const PolynomialExpr& rhs,
                                        bool subtract) {
        PolynomialExpr result;
        for (std::size_t degree = 0; degree < result.polynomial.coeff.size(); ++degree) {
          result.polynomial.coeff[degree] =
              subtract ? lhs.polynomial.coeff[degree] - rhs.polynomial.coeff[degree]
                       : lhs.polynomial.coeff[degree] + rhs.polynomial.coeff[degree];
        }
        result.stateCoefficients = lhs.stateCoefficients;
        for (const auto& [name, coefficient] : rhs.stateCoefficients) {
          auto& destination = result.stateCoefficients[name];
          destination = subtract ? destination - coefficient : destination + coefficient;
          if (destination == 0) {
            result.stateCoefficients.erase(name);
          }
        }
        return result;
      };
      const auto scaleExpression = [&](const PolynomialExpr& expression, std::uint32_t factor) {
        PolynomialExpr result;
        result.polynomial = scalePolynomial(expression.polynomial, factor);
        for (const auto& [name, coefficient] : expression.stateCoefficients) {
          const std::uint32_t scaled =
              static_cast<std::uint32_t>(static_cast<std::uint64_t>(coefficient) * factor);
          if (scaled != 0) {
            result.stateCoefficients[name] = scaled;
          }
        }
        return result;
      };
      const auto multiplyExpression =
          [&](const PolynomialExpr& lhs,
              const PolynomialExpr& rhs) -> std::optional<PolynomialExpr> {
        const auto lhsConstant = constantValue(lhs);
        const auto rhsConstant = constantValue(rhs);
        if (lhsConstant) {
          return scaleExpression(rhs, *lhsConstant);
        }
        if (rhsConstant) {
          return scaleExpression(lhs, *rhsConstant);
        }
        if (!lhs.stateCoefficients.empty() || !rhs.stateCoefficients.empty()) {
          return std::nullopt;
        }
        const auto product = multiplyPolynomial(lhs.polynomial, rhs.polynomial);
        if (!product) {
          return std::nullopt;
        }
        PolynomialExpr result;
        result.polynomial = *product;
        return result;
      };
      const auto multiply32 = [](std::uint32_t lhs, std::uint32_t rhs) {
        return static_cast<std::uint32_t>(static_cast<std::uint64_t>(lhs) * rhs);
      };
      const auto sumPowersFromZero = [&](std::uint64_t count) {
        std::array<std::uint32_t, 4> sums{};
        sums[0] = static_cast<std::uint32_t>(count);
        if (count == 0) {
          return sums;
        }
        std::uint64_t first = count;
        std::uint64_t second = count - 1;
        if ((first & 1u) == 0) {
          first /= 2;
        } else {
          second /= 2;
        }
        sums[1] = multiply32(static_cast<std::uint32_t>(first), static_cast<std::uint32_t>(second));

        first = count;
        second = count - 1;
        std::uint64_t third = count * 2 - 1;
        if ((first & 1u) == 0) {
          first /= 2;
        } else {
          second /= 2;
        }
        if (first % 3 == 0) {
          first /= 3;
        } else if (second % 3 == 0) {
          second /= 3;
        } else {
          third /= 3;
        }
        sums[2] = multiply32(
            multiply32(static_cast<std::uint32_t>(first), static_cast<std::uint32_t>(second)),
            static_cast<std::uint32_t>(third));
        sums[3] = multiply32(sums[1], sums[1]);
        return sums;
      };
      const auto sumPolynomial = [&](const Polynomial& polynomial, int initial,
                                     std::uint64_t trips) {
        const auto sums = sumPowersFromZero(trips);
        const std::uint32_t start = static_cast<std::uint32_t>(initial);
        const std::uint32_t start2 = multiply32(start, start);
        const std::uint32_t start3 = multiply32(start2, start);
        std::array<std::uint32_t, 4> shifted{};
        shifted[0] = sums[0];
        shifted[1] = multiply32(start, sums[0]) + sums[1];
        shifted[2] =
            multiply32(start2, sums[0]) + multiply32(multiply32(2, start), sums[1]) + sums[2];
        shifted[3] = multiply32(start3, sums[0]) + multiply32(multiply32(3, start2), sums[1]) +
                     multiply32(multiply32(3, start), sums[2]) + sums[3];
        std::uint32_t result = 0;
        for (std::size_t degree = 0; degree < polynomial.coeff.size(); ++degree) {
          result += multiply32(polynomial.coeff[degree], shifted[degree]);
        }
        return result;
      };

      for (int summaryRound = 0; summaryRound < 8; ++summaryRound) {
        std::unordered_map<std::string, int> labelReferences;
        for (const auto& inst : ir) {
          if ((inst.op == IROp::BRANCH || inst.op == IROp::BEQZ || inst.op == IROp::BNEZ) &&
              inst.dest.isLabel()) {
            ++labelReferences[inst.dest.name];
          }
        }

        bool summarized = false;
        for (std::size_t loopStart = ir.size(); loopStart-- > 0 && !summarized;) {
          if (ir[loopStart].op != IROp::BRANCH || !ir[loopStart].dest.isLabel() ||
              loopStart + 1 >= ir.size() || ir[loopStart + 1].op != IROp::LABEL ||
              !ir[loopStart + 1].dest.isLabel()) {
            continue;
          }
          const std::string condLabel = ir[loopStart].dest.name;
          const std::string bodyLabel = ir[loopStart + 1].dest.name;
          if (labelReferences[condLabel] != 1 || labelReferences[bodyLabel] != 1) {
            continue;
          }
          std::size_t condIndex = loopStart + 2;
          while (condIndex < ir.size() && ir[condIndex].op != IROp::FUNC_END &&
                 !(ir[condIndex].op == IROp::LABEL && ir[condIndex].dest.isLabel() &&
                   ir[condIndex].dest.name == condLabel)) {
            ++condIndex;
          }
          if (condIndex + 2 >= ir.size() || ir[condIndex].op == IROp::FUNC_END) {
            continue;
          }
          const IRInst& condition = ir[condIndex + 1];
          const IRInst& backedge = ir[condIndex + 2];
          if ((condition.op != IROp::LT && condition.op != IROp::LE) ||
              !condition.dest.isLocalVar() || !condition.src1.isLocalVar() ||
              backedge.op != IROp::BNEZ || !backedge.dest.isLabel() ||
              backedge.dest.name != bodyLabel || !backedge.src1.isLocalVar() ||
              backedge.src1.name != condition.dest.name) {
            continue;
          }
          const std::string induction = condition.src1.name;

          const auto findNearbyConstant = [&](const std::string& name) -> std::optional<int> {
            for (std::size_t position = loopStart; position > 0; --position) {
              const IRInst& candidate = ir[position - 1];
              if (candidate.dest.isLocalVar() && candidate.dest.name == name) {
                if ((candidate.op == IROp::ASSIGN || candidate.op == IROp::LOCAL_VAR_DECL) &&
                    candidate.src1.isImm()) {
                  return candidate.src1.immVal;
                }
                return std::nullopt;
              }
              if (candidate.op == IROp::LABEL || candidate.op == IROp::BRANCH ||
                  candidate.op == IROp::BEQZ || candidate.op == IROp::BNEZ ||
                  candidate.op == IROp::CALL || candidate.op == IROp::FUNC_BEGIN) {
                break;
              }
            }
            return std::nullopt;
          };
          const auto findUniqueConstant = [&](const std::string& name) -> std::optional<int> {
            std::size_t functionBegin = loopStart;
            while (functionBegin > 0 && ir[functionBegin - 1].op != IROp::FUNC_BEGIN) {
              --functionBegin;
            }
            int definitions = 0;
            std::optional<int> value;
            for (std::size_t position = functionBegin; position < ir.size(); ++position) {
              const IRInst& candidate = ir[position];
              if (candidate.op == IROp::FUNC_END) {
                break;
              }
              if (!candidate.dest.isLocalVar() || candidate.dest.name != name ||
                  candidate.op == IROp::RETURN || candidate.op == IROp::PARAM ||
                  (candidate.op == IROp::LOCAL_VAR_DECL && candidate.src1.isNone())) {
                continue;
              }
              ++definitions;
              if ((candidate.op == IROp::ASSIGN || candidate.op == IROp::LOCAL_VAR_DECL) &&
                  candidate.src1.isImm()) {
                value = candidate.src1.immVal;
              } else {
                value.reset();
              }
            }
            return definitions == 1 ? value : std::nullopt;
          };

          const auto initial = findNearbyConstant(induction);
          std::optional<int> upper;
          if (condition.src2.isImm()) {
            upper = condition.src2.immVal;
          } else if (condition.src2.isLocalVar()) {
            upper = findNearbyConstant(condition.src2.name);
            if (!upper) {
              upper = findUniqueConstant(condition.src2.name);
            }
          }
          if (!initial || !upper ||
              (condition.op == IROp::LE && *upper == INT32_MAX && *initial <= *upper)) {
            continue;
          }
          const bool runs = condition.op == IROp::LT ? *initial < *upper : *initial <= *upper;
          const std::uint64_t trips =
              runs ? static_cast<std::uint64_t>(static_cast<std::int64_t>(*upper) - *initial +
                                                (condition.op == IROp::LE ? 1 : 0))
                   : 0;
          const std::int64_t finalInduction =
              static_cast<std::int64_t>(*initial) + static_cast<std::int64_t>(trips);
          if (trips < 2 || finalInduction < INT32_MIN || finalInduction > INT32_MAX) {
            continue;
          }

          std::unordered_map<std::string, PolynomialExpr> values;
          std::unordered_map<std::string, Polynomial> accumulatorDeltas;
          std::unordered_set<std::string> written;
          bool bodySupported = true;
          bool inductionIncremented = false;
          int inductionWrites = 0;
          const auto expressionForOperand =
              [&](const Operand& operand) -> std::optional<PolynomialExpr> {
            PolynomialExpr expression;
            if (operand.isImm()) {
              expression.polynomial = constantPolynomial(operand.immVal);
              return expression;
            }
            if (!operand.isLocalVar()) {
              return std::nullopt;
            }
            if (operand.name == induction) {
              expression.polynomial.coeff[1] = 1;
              return expression;
            }
            const auto temporary = values.find(operand.name);
            if (temporary != values.end()) {
              return temporary->second;
            }
            const auto constant = findUniqueConstant(operand.name);
            if (constant) {
              expression.polynomial = constantPolynomial(*constant);
              return expression;
            }
            expression.stateCoefficients[operand.name] = 1;
            return expression;
          };

          for (std::size_t position = loopStart + 2; position < condIndex && bodySupported;
               ++position) {
            const IRInst& inst = ir[position];
            if (inst.op == IROp::LOCAL_VAR_DECL && inst.src1.isNone()) {
              continue;
            }
            if ((inst.op == IROp::ADD || inst.op == IROp::SUB) && inst.dest.isLocalVar() &&
                inst.dest.name == induction && inst.src1.isLocalVar() &&
                inst.src1.name == induction && inst.src2.isImm()) {
              const int step = inst.op == IROp::ADD ? inst.src2.immVal : -inst.src2.immVal;
              ++inductionWrites;
              inductionIncremented = step == 1;
              continue;
            }
            if (inductionIncremented ||
                (inst.op != IROp::LOCAL_VAR_DECL && inst.op != IROp::ASSIGN &&
                 inst.op != IROp::ADD && inst.op != IROp::SUB && inst.op != IROp::MUL) ||
                !inst.dest.isLocalVar()) {
              bodySupported = false;
              break;
            }
            const auto lhs = expressionForOperand(inst.src1);
            if (!lhs) {
              bodySupported = false;
              break;
            }
            PolynomialExpr expression;
            if (inst.op == IROp::LOCAL_VAR_DECL || inst.op == IROp::ASSIGN) {
              expression = *lhs;
            } else {
              const auto rhs = expressionForOperand(inst.src2);
              if (!rhs) {
                bodySupported = false;
                break;
              }
              if (inst.op == IROp::ADD || inst.op == IROp::SUB) {
                expression = combineExpression(*lhs, *rhs, inst.op == IROp::SUB);
              } else {
                const auto product = multiplyExpression(*lhs, *rhs);
                if (!product) {
                  bodySupported = false;
                  break;
                }
                expression = *product;
              }
            }

            if (isCompilerTemp(inst.dest.name)) {
              values[inst.dest.name] = std::move(expression);
              continue;
            }
            if (inst.dest.name == induction || written.count(inst.dest.name) != 0 ||
                expression.stateCoefficients.size() != 1) {
              bodySupported = false;
              break;
            }
            const auto self = expression.stateCoefficients.find(inst.dest.name);
            if (self == expression.stateCoefficients.end() || self->second != 1) {
              bodySupported = false;
              break;
            }
            written.insert(inst.dest.name);
            accumulatorDeltas[inst.dest.name] = expression.polynomial;
          }
          bool hasNonlinearTerm = false;
          for (const auto& [name, polynomial] : accumulatorDeltas) {
            (void) name;
            hasNonlinearTerm =
                hasNonlinearTerm || polynomial.coeff[2] != 0 || polynomial.coeff[3] != 0;
          }
          if (!bodySupported || inductionWrites != 1 || !inductionIncremented ||
              accumulatorDeltas.empty() || !hasNonlinearTerm ||
              (condition.src2.isLocalVar() && written.count(condition.src2.name) != 0)) {
            continue;
          }

          const std::size_t loopEnd = condIndex + 3;
          const auto usedAfterLoop = [&](const std::string& name) {
            for (std::size_t position = loopEnd; position < ir.size(); ++position) {
              const IRInst& inst = ir[position];
              if (inst.op == IROp::FUNC_BEGIN || inst.op == IROp::FUNC_END) {
                break;
              }
              if ((inst.src1.isLocalVar() && inst.src1.name == name) ||
                  (inst.src2.isLocalVar() && inst.src2.name == name) ||
                  ((inst.op == IROp::RETURN || inst.op == IROp::PARAM) && inst.dest.isLocalVar() &&
                   inst.dest.name == name)) {
                return true;
              }
            }
            return false;
          };

          std::vector<IRInst> replacement;
          for (const auto& [name, polynomial] : accumulatorDeltas) {
            if (!usedAfterLoop(name)) {
              continue;
            }
            const std::uint32_t delta = sumPolynomial(polynomial, *initial, trips);
            if (delta != 0) {
              replacement.emplace_back(IROp::ADD, Operand::localVar(name), Operand::localVar(name),
                                       Operand::imm(static_cast<std::int32_t>(delta)));
            }
          }
          if (usedAfterLoop(induction)) {
            replacement.emplace_back(IROp::ASSIGN, Operand::localVar(induction),
                                     Operand::imm(static_cast<int>(finalInduction)),
                                     Operand::none());
          }

          std::size_t eraseEnd = loopEnd;
          if (eraseEnd < ir.size() && ir[eraseEnd].op == IROp::LABEL &&
              ir[eraseEnd].dest.isLabel() && labelReferences[ir[eraseEnd].dest.name] == 0) {
            ++eraseEnd;
          }
          ir.erase(ir.begin() + static_cast<std::ptrdiff_t>(loopStart),
                   ir.begin() + static_cast<std::ptrdiff_t>(eraseEnd));
          ir.insert(ir.begin() + static_cast<std::ptrdiff_t>(loopStart), replacement.begin(),
                    replacement.end());
          summarized = true;
          changed = true;
        }
        if (!summarized) {
          break;
        }
      }
    }

    // Pass 5.555: 汇总非负线性归纳变量的整除桶累加。
    //
    // 对 `sum += a * ((i + c) / d) + b`，商在每 d 个连续输入上保持不变。
    // 使用 floor 前缀和按除法桶闭式计算全部增量，不枚举源循环。为保持 C 的
    // 截断语义，只接受被除数全程位于 [0, INT32_MAX]、常量非零除数、规范
    // `i += 1` 和直线局部计算；负被除数、可变除数和交叉状态均保守回退。
    {
      struct BucketValue {
        enum class Kind { UNKNOWN, CONSTANT, INDUCTION, QUOTIENT };
        Kind kind = Kind::UNKNOWN;
        int constant = 0;
        std::int64_t offset = 0;
        std::uint64_t divisor = 1;
        std::uint32_t quotientCoefficient = 0;
        std::uint32_t additiveConstant = 0;
        std::unordered_map<std::string, std::uint32_t> stateCoefficients;
      };
      struct BucketAccumulator {
        std::string name;
        std::int64_t offset = 0;
        std::uint64_t divisor = 1;
        std::uint32_t quotientCoefficient = 0;
        std::uint32_t additiveConstant = 0;
      };

      const auto sameQuotient = [](const BucketValue& lhs, const BucketValue& rhs) {
        return lhs.kind == BucketValue::Kind::QUOTIENT && rhs.kind == BucketValue::Kind::QUOTIENT &&
               lhs.offset == rhs.offset && lhs.divisor == rhs.divisor;
      };
      const auto foldBucketConstant = [](IROp op, int lhs, int rhs) -> std::optional<int> {
        const std::uint32_t left = static_cast<std::uint32_t>(lhs);
        const std::uint32_t right = static_cast<std::uint32_t>(rhs);
        switch (op) {
        case IROp::ADD:
          return static_cast<std::int32_t>(left + right);
        case IROp::SUB:
          return static_cast<std::int32_t>(left - right);
        case IROp::MUL:
          return static_cast<std::int32_t>(
              static_cast<std::uint32_t>(static_cast<std::uint64_t>(left) * right));
        case IROp::DIV:
          if (rhs == 0 || (lhs == INT32_MIN && rhs == -1)) {
            return std::nullopt;
          }
          return lhs / rhs;
        default:
          return std::nullopt;
        }
      };
      const auto quotientPrefix = [](std::uint64_t count, std::uint64_t divisor) {
        const std::uint64_t quotient = count / divisor;
        const std::uint64_t remainder = count % divisor;
        std::uint64_t first = quotient;
        std::uint64_t second = quotient == 0 ? 0 : quotient - 1;
        if ((first & 1u) == 0) {
          first /= 2;
        } else {
          second /= 2;
        }
        return divisor * first * second + quotient * remainder;
      };

      for (int summaryRound = 0; summaryRound < 8; ++summaryRound) {
        std::unordered_map<std::string, int> labelReferences;
        for (const auto& inst : ir) {
          if ((inst.op == IROp::BRANCH || inst.op == IROp::BEQZ || inst.op == IROp::BNEZ) &&
              inst.dest.isLabel()) {
            ++labelReferences[inst.dest.name];
          }
        }

        bool summarized = false;
        for (std::size_t loopStart = ir.size(); loopStart-- > 0 && !summarized;) {
          if (ir[loopStart].op != IROp::BRANCH || !ir[loopStart].dest.isLabel() ||
              loopStart + 1 >= ir.size() || ir[loopStart + 1].op != IROp::LABEL ||
              !ir[loopStart + 1].dest.isLabel()) {
            continue;
          }
          const std::string condLabel = ir[loopStart].dest.name;
          const std::string bodyLabel = ir[loopStart + 1].dest.name;
          if (labelReferences[condLabel] != 1 || labelReferences[bodyLabel] != 1) {
            continue;
          }
          std::size_t condIndex = loopStart + 2;
          while (condIndex < ir.size() && ir[condIndex].op != IROp::FUNC_END &&
                 !(ir[condIndex].op == IROp::LABEL && ir[condIndex].dest.isLabel() &&
                   ir[condIndex].dest.name == condLabel)) {
            ++condIndex;
          }
          if (condIndex + 2 >= ir.size() || ir[condIndex].op == IROp::FUNC_END) {
            continue;
          }
          const IRInst& condition = ir[condIndex + 1];
          const IRInst& backedge = ir[condIndex + 2];
          if ((condition.op != IROp::LT && condition.op != IROp::LE) ||
              !condition.dest.isLocalVar() || !condition.src1.isLocalVar() ||
              backedge.op != IROp::BNEZ || !backedge.dest.isLabel() ||
              backedge.dest.name != bodyLabel || !backedge.src1.isLocalVar() ||
              backedge.src1.name != condition.dest.name) {
            continue;
          }
          const std::string induction = condition.src1.name;

          const auto findNearbyConstant = [&](const std::string& name) -> std::optional<int> {
            for (std::size_t position = loopStart; position > 0; --position) {
              const IRInst& candidate = ir[position - 1];
              if (candidate.dest.isLocalVar() && candidate.dest.name == name) {
                if ((candidate.op == IROp::ASSIGN || candidate.op == IROp::LOCAL_VAR_DECL) &&
                    candidate.src1.isImm()) {
                  return candidate.src1.immVal;
                }
                return std::nullopt;
              }
              if (candidate.op == IROp::LABEL || candidate.op == IROp::BRANCH ||
                  candidate.op == IROp::BEQZ || candidate.op == IROp::BNEZ ||
                  candidate.op == IROp::CALL || candidate.op == IROp::FUNC_BEGIN) {
                break;
              }
            }
            return std::nullopt;
          };
          const auto findUniqueConstant = [&](const std::string& name) -> std::optional<int> {
            std::size_t functionBegin = loopStart;
            while (functionBegin > 0 && ir[functionBegin - 1].op != IROp::FUNC_BEGIN) {
              --functionBegin;
            }
            int definitions = 0;
            std::optional<int> value;
            for (std::size_t position = functionBegin; position < ir.size(); ++position) {
              const IRInst& candidate = ir[position];
              if (candidate.op == IROp::FUNC_END) {
                break;
              }
              if (!candidate.dest.isLocalVar() || candidate.dest.name != name ||
                  candidate.op == IROp::RETURN || candidate.op == IROp::PARAM ||
                  (candidate.op == IROp::LOCAL_VAR_DECL && candidate.src1.isNone())) {
                continue;
              }
              ++definitions;
              if ((candidate.op == IROp::ASSIGN || candidate.op == IROp::LOCAL_VAR_DECL) &&
                  candidate.src1.isImm()) {
                value = candidate.src1.immVal;
              } else {
                value.reset();
              }
            }
            return definitions == 1 ? value : std::nullopt;
          };

          const auto initial = findNearbyConstant(induction);
          std::optional<int> upper;
          if (condition.src2.isImm()) {
            upper = condition.src2.immVal;
          } else if (condition.src2.isLocalVar()) {
            upper = findNearbyConstant(condition.src2.name);
            if (!upper) {
              upper = findUniqueConstant(condition.src2.name);
            }
          }
          if (!initial || !upper ||
              (condition.op == IROp::LE && *upper == INT32_MAX && *initial <= *upper)) {
            continue;
          }
          std::int64_t tripCount = static_cast<std::int64_t>(*upper) - *initial;
          if (condition.op == IROp::LE) {
            ++tripCount;
          }
          tripCount = std::max<std::int64_t>(0, tripCount);
          const std::int64_t finalInduction = static_cast<std::int64_t>(*initial) + tripCount;
          if (tripCount < 2 || tripCount > INT32_MAX || finalInduction > INT32_MAX) {
            continue;
          }
          const std::uint64_t trips = static_cast<std::uint64_t>(tripCount);
          const auto inductionRangeSafe = [&](std::int64_t offset) {
            const std::int64_t lowest = static_cast<std::int64_t>(*initial) + offset;
            const std::int64_t highest = finalInduction - 1 + offset;
            return lowest >= 0 && highest <= INT32_MAX;
          };

          std::unordered_map<std::string, BucketValue> values;
          values[induction] = {BucketValue::Kind::INDUCTION, 0, 0, 1, 0, 0, {}};
          const auto valueOf = [&](const Operand& operand) {
            if (operand.isImm()) {
              return BucketValue{BucketValue::Kind::CONSTANT, operand.immVal, 0, 1, 0, 0, {}};
            }
            if (operand.isLocalVar()) {
              const auto found = values.find(operand.name);
              if (found != values.end()) {
                return found->second;
              }
              const auto constant = findUniqueConstant(operand.name);
              if (constant) {
                return BucketValue{BucketValue::Kind::CONSTANT, *constant, 0, 1, 0, 0, {}};
              }
              if (!isCompilerTemp(operand.name)) {
                BucketValue state;
                state.kind = BucketValue::Kind::QUOTIENT;
                state.stateCoefficients[operand.name] = 1;
                return state;
              }
            }
            return BucketValue{};
          };

          bool bodySupported = true;
          bool inductionIncremented = false;
          int inductionWrites = 0;
          std::unordered_set<std::string> written;
          std::unordered_set<std::string> derivedDefinitions;
          std::vector<BucketAccumulator> accumulators;
          for (std::size_t position = loopStart + 2; position < condIndex && bodySupported;
               ++position) {
            const IRInst& inst = ir[position];
            if (inst.op == IROp::LOCAL_VAR_DECL && inst.src1.isNone()) {
              continue;
            }
            if (!inst.dest.isLocalVar() || inductionIncremented) {
              bodySupported = false;
              break;
            }
            written.insert(inst.dest.name);
            if (inst.dest.name == induction) {
              ++inductionWrites;
              inductionIncremented = inductionWrites == 1 && inst.op == IROp::ADD &&
                                     inst.src1.isLocalVar() && inst.src1.name == induction &&
                                     inst.src2.isImm() && inst.src2.immVal == 1;
              if (!inductionIncremented) {
                bodySupported = false;
              }
              continue;
            }

            const bool lhsSelf = inst.src1.isLocalVar() && inst.src1.name == inst.dest.name;
            const bool rhsSelf = inst.src2.isLocalVar() && inst.src2.name == inst.dest.name;
            if ((inst.op == IROp::ADD || inst.op == IROp::SUB) && (lhsSelf || rhsSelf) &&
                !(inst.op == IROp::SUB && rhsSelf)) {
              const BucketValue delta = valueOf(lhsSelf ? inst.src2 : inst.src1);
              if (!isCompilerTemp(inst.dest.name) && delta.kind == BucketValue::Kind::QUOTIENT &&
                  delta.quotientCoefficient != 0 && delta.stateCoefficients.empty()) {
                const bool subtract = inst.op == IROp::SUB;
                accumulators.push_back(
                    {inst.dest.name, delta.offset, delta.divisor,
                     subtract ? 0u - delta.quotientCoefficient : delta.quotientCoefficient,
                     subtract ? 0u - delta.additiveConstant : delta.additiveConstant});
                values.erase(inst.dest.name);
                continue;
              }
            }

            const BucketValue lhs = valueOf(inst.src1);
            const BucketValue rhs = valueOf(inst.src2);
            BucketValue result;
            if (inst.op == IROp::ASSIGN || inst.op == IROp::LOCAL_VAR_DECL) {
              result = lhs;
            } else if ((inst.op == IROp::ADD || inst.op == IROp::SUB) &&
                       lhs.kind == BucketValue::Kind::CONSTANT &&
                       rhs.kind == BucketValue::Kind::CONSTANT) {
              const auto folded = foldBucketConstant(inst.op, lhs.constant, rhs.constant);
              if (folded) {
                result = {BucketValue::Kind::CONSTANT, *folded, 0, 1, 0, 0, {}};
              }
            } else if (inst.op == IROp::ADD || inst.op == IROp::SUB) {
              const int direction = inst.op == IROp::ADD ? 1 : -1;
              if (lhs.kind == BucketValue::Kind::INDUCTION &&
                  rhs.kind == BucketValue::Kind::CONSTANT) {
                const std::int64_t offset =
                    lhs.offset + static_cast<std::int64_t>(direction) * rhs.constant;
                if (inductionRangeSafe(offset)) {
                  result = {BucketValue::Kind::INDUCTION, 0, offset, 1, 0, 0, {}};
                }
              } else if (inst.op == IROp::ADD && lhs.kind == BucketValue::Kind::CONSTANT &&
                         rhs.kind == BucketValue::Kind::INDUCTION) {
                const std::int64_t offset = rhs.offset + lhs.constant;
                if (inductionRangeSafe(offset)) {
                  result = {BucketValue::Kind::INDUCTION, 0, offset, 1, 0, 0, {}};
                }
              } else {
                const bool lhsLinear = lhs.kind == BucketValue::Kind::CONSTANT ||
                                       lhs.kind == BucketValue::Kind::QUOTIENT;
                const bool rhsLinear = rhs.kind == BucketValue::Kind::CONSTANT ||
                                       rhs.kind == BucketValue::Kind::QUOTIENT;
                const bool lhsHasQuotient =
                    lhs.kind == BucketValue::Kind::QUOTIENT && lhs.quotientCoefficient != 0;
                const bool rhsHasQuotient =
                    rhs.kind == BucketValue::Kind::QUOTIENT && rhs.quotientCoefficient != 0;
                if (lhsLinear && rhsLinear &&
                    (!lhsHasQuotient || !rhsHasQuotient || sameQuotient(lhs, rhs))) {
                  result.kind = BucketValue::Kind::QUOTIENT;
                  const BucketValue& quotientSource = lhsHasQuotient ? lhs : rhs;
                  if (lhsHasQuotient || rhsHasQuotient) {
                    result.offset = quotientSource.offset;
                    result.divisor = quotientSource.divisor;
                  }
                  const std::uint32_t lhsQuotient =
                      lhs.kind == BucketValue::Kind::QUOTIENT ? lhs.quotientCoefficient : 0;
                  const std::uint32_t rhsQuotient =
                      rhs.kind == BucketValue::Kind::QUOTIENT ? rhs.quotientCoefficient : 0;
                  result.quotientCoefficient =
                      inst.op == IROp::ADD ? lhsQuotient + rhsQuotient : lhsQuotient - rhsQuotient;
                  const std::uint32_t lhsConstant = lhs.kind == BucketValue::Kind::CONSTANT
                                                        ? static_cast<std::uint32_t>(lhs.constant)
                                                        : lhs.additiveConstant;
                  const std::uint32_t rhsConstant = rhs.kind == BucketValue::Kind::CONSTANT
                                                        ? static_cast<std::uint32_t>(rhs.constant)
                                                        : rhs.additiveConstant;
                  result.additiveConstant =
                      inst.op == IROp::ADD ? lhsConstant + rhsConstant : lhsConstant - rhsConstant;
                  result.stateCoefficients = lhs.stateCoefficients;
                  for (const auto& [name, coefficient] : rhs.stateCoefficients) {
                    auto& destination = result.stateCoefficients[name];
                    destination = inst.op == IROp::ADD ? destination + coefficient
                                                       : destination - coefficient;
                    if (destination == 0) {
                      result.stateCoefficients.erase(name);
                    }
                  }
                }
              }
            } else if (inst.op == IROp::MUL) {
              const BucketValue* quotient = nullptr;
              const BucketValue* constant = nullptr;
              if (lhs.kind == BucketValue::Kind::QUOTIENT &&
                  rhs.kind == BucketValue::Kind::CONSTANT) {
                quotient = &lhs;
                constant = &rhs;
              } else if (rhs.kind == BucketValue::Kind::QUOTIENT &&
                         lhs.kind == BucketValue::Kind::CONSTANT) {
                quotient = &rhs;
                constant = &lhs;
              }
              if (quotient != nullptr) {
                result = *quotient;
                const std::uint32_t factor = static_cast<std::uint32_t>(constant->constant);
                result.quotientCoefficient = static_cast<std::uint32_t>(
                    static_cast<std::uint64_t>(result.quotientCoefficient) * factor);
                result.additiveConstant = static_cast<std::uint32_t>(
                    static_cast<std::uint64_t>(result.additiveConstant) * factor);
                for (auto& [name, coefficient] : result.stateCoefficients) {
                  (void) name;
                  coefficient =
                      static_cast<std::uint32_t>(static_cast<std::uint64_t>(coefficient) * factor);
                }
              }
            } else if (inst.op == IROp::DIV && lhs.kind == BucketValue::Kind::INDUCTION &&
                       rhs.kind == BucketValue::Kind::CONSTANT && rhs.constant != 0 &&
                       rhs.constant != INT32_MIN && inductionRangeSafe(lhs.offset)) {
              const bool negative = rhs.constant < 0;
              const std::uint64_t divisor = static_cast<std::uint64_t>(
                  negative ? -static_cast<std::int64_t>(rhs.constant) : rhs.constant);
              result = {BucketValue::Kind::QUOTIENT,           0, lhs.offset, divisor,
                        negative ? std::uint32_t{0} - 1u : 1u, 0, {}};
            }
            if (result.kind == BucketValue::Kind::UNKNOWN) {
              bodySupported = false;
              break;
            }
            if (!isCompilerTemp(inst.dest.name) && result.kind == BucketValue::Kind::QUOTIENT &&
                result.quotientCoefficient != 0 && result.stateCoefficients.size() == 1) {
              const auto self = result.stateCoefficients.find(inst.dest.name);
              if (self != result.stateCoefficients.end() && self->second == 1) {
                accumulators.push_back({inst.dest.name, result.offset, result.divisor,
                                        result.quotientCoefficient, result.additiveConstant});
                values.erase(inst.dest.name);
                continue;
              }
            }
            if (!result.stateCoefficients.empty() && !isCompilerTemp(inst.dest.name)) {
              bodySupported = false;
              break;
            }
            values[inst.dest.name] = result;
            if (!isCompilerTemp(inst.dest.name)) {
              derivedDefinitions.insert(inst.dest.name);
            }
          }
          if (!bodySupported || inductionWrites != 1 || !inductionIncremented ||
              accumulators.empty() ||
              (condition.src2.isLocalVar() && written.count(condition.src2.name) != 0)) {
            continue;
          }

          const std::size_t loopEnd = condIndex + 3;
          const auto usedAfterLoop = [&](const std::string& name) {
            for (std::size_t position = loopEnd; position < ir.size(); ++position) {
              const IRInst& inst = ir[position];
              if (inst.op == IROp::FUNC_BEGIN || inst.op == IROp::FUNC_END) {
                break;
              }
              if ((inst.src1.isLocalVar() && inst.src1.name == name) ||
                  (inst.src2.isLocalVar() && inst.src2.name == name) ||
                  ((inst.op == IROp::RETURN || inst.op == IROp::PARAM) && inst.dest.isLocalVar() &&
                   inst.dest.name == name)) {
                return true;
              }
            }
            return false;
          };
          bool leaksDerivedValue = false;
          for (const std::string& name : derivedDefinitions) {
            if (usedAfterLoop(name)) {
              leaksDerivedValue = true;
              break;
            }
          }
          if (leaksDerivedValue) {
            continue;
          }

          std::unordered_map<std::string, std::uint32_t> deltas;
          std::vector<std::string> accumulatorOrder;
          for (const BucketAccumulator& accumulator : accumulators) {
            const std::uint64_t first = static_cast<std::uint64_t>(
                static_cast<std::int64_t>(*initial) + accumulator.offset);
            const std::uint64_t quotientSum = quotientPrefix(first + trips, accumulator.divisor) -
                                              quotientPrefix(first, accumulator.divisor);
            const std::uint32_t delta =
                static_cast<std::uint32_t>(quotientSum * accumulator.quotientCoefficient +
                                           trips * accumulator.additiveConstant);
            if (deltas.count(accumulator.name) == 0) {
              accumulatorOrder.push_back(accumulator.name);
            }
            deltas[accumulator.name] += delta;
          }

          std::vector<IRInst> replacement;
          for (const std::string& name : accumulatorOrder) {
            if (usedAfterLoop(name) && deltas[name] != 0) {
              replacement.emplace_back(IROp::ADD, Operand::localVar(name), Operand::localVar(name),
                                       Operand::imm(static_cast<std::int32_t>(deltas[name])));
            }
          }
          if (usedAfterLoop(induction)) {
            replacement.emplace_back(IROp::ASSIGN, Operand::localVar(induction),
                                     Operand::imm(static_cast<int>(finalInduction)),
                                     Operand::none());
          }

          std::size_t eraseEnd = loopEnd;
          if (eraseEnd < ir.size() && ir[eraseEnd].op == IROp::LABEL &&
              ir[eraseEnd].dest.isLabel() && labelReferences[ir[eraseEnd].dest.name] == 0) {
            ++eraseEnd;
          }
          ir.erase(ir.begin() + static_cast<std::ptrdiff_t>(loopStart),
                   ir.begin() + static_cast<std::ptrdiff_t>(eraseEnd));
          ir.insert(ir.begin() + static_cast<std::ptrdiff_t>(loopStart), replacement.begin(),
                    replacement.end());
          summarized = true;
          changed = true;
        }
        if (!summarized) {
          break;
        }
      }
    }

    // Pass 5.56: 汇总由归纳变量余数驱动的直线累加循环。
    //
    // `i % k`（以及由它组合出的算术表达式）在非负、无回绕的 `i += 1`
    // 循环中至多每 |k| 轮重复。这里只枚举不同的余数相位，再把一个周期的
    // 增量乘以完整周期数；不会按源程序的迭代次数执行循环。累加器之外的可观察
    // 状态、运行时除数、过大的组合周期、非规范归纳或控制流都会保守回退。
    {
      constexpr std::uint64_t kMaxPeriodicPhases = 1024;

      struct PeriodicValue {
        enum class Kind { UNKNOWN, CONSTANT, INDUCTION, QUOTIENT, QUOTIENT_PRODUCT, PERIODIC };
        Kind kind = Kind::UNKNOWN;
        int value = 0;
        int offset = 0;
        int divisor = 0;
        std::uint64_t period = 0;
      };
      struct PeriodicAccumulator {
        std::string name;
        Operand delta;
        int sign = 1;
        std::uint64_t period = 0;
      };

      const auto gcd = [](std::uint64_t lhs, std::uint64_t rhs) {
        while (rhs != 0) {
          const std::uint64_t remainder = lhs % rhs;
          lhs = rhs;
          rhs = remainder;
        }
        return lhs;
      };
      const auto combinePeriod = [&](std::uint64_t lhs, std::uint64_t rhs) {
        if (lhs == 0 || rhs == 0) {
          return std::uint64_t{0};
        }
        const std::uint64_t divisor = gcd(lhs, rhs);
        const std::uint64_t factor = rhs / divisor;
        if (lhs > kMaxPeriodicPhases / factor) {
          return std::uint64_t{0};
        }
        const std::uint64_t result = lhs * factor;
        return result <= kMaxPeriodicPhases ? result : std::uint64_t{0};
      };
      const auto wrappedBinary = [](IROp op, int lhs, int rhs) -> std::optional<int> {
        switch (op) {
        case IROp::ADD:
          return static_cast<std::int32_t>(static_cast<std::uint32_t>(lhs) +
                                           static_cast<std::uint32_t>(rhs));
        case IROp::SUB:
          return static_cast<std::int32_t>(static_cast<std::uint32_t>(lhs) -
                                           static_cast<std::uint32_t>(rhs));
        case IROp::MUL:
          return static_cast<std::int32_t>(static_cast<std::uint32_t>(
              static_cast<std::uint64_t>(static_cast<std::uint32_t>(lhs)) *
              static_cast<std::uint32_t>(rhs)));
        case IROp::DIV:
          if (rhs == 0 || (lhs == INT32_MIN && rhs == -1)) {
            return std::nullopt;
          }
          return lhs / rhs;
        case IROp::MOD:
          if (rhs == 0 || (lhs == INT32_MIN && rhs == -1)) {
            return std::nullopt;
          }
          return lhs % rhs;
        case IROp::LT:
          return lhs < rhs ? 1 : 0;
        case IROp::GT:
          return lhs > rhs ? 1 : 0;
        case IROp::LE:
          return lhs <= rhs ? 1 : 0;
        case IROp::GE:
          return lhs >= rhs ? 1 : 0;
        case IROp::EQ:
          return lhs == rhs ? 1 : 0;
        case IROp::NE:
          return lhs != rhs ? 1 : 0;
        default:
          return std::nullopt;
        }
      };

      for (int summaryRound = 0; summaryRound < 8; ++summaryRound) {
        std::unordered_map<std::string, int> labelReferences;
        std::unordered_map<std::string, std::size_t> labelPositions;
        for (std::size_t index = 0; index < ir.size(); ++index) {
          const IRInst& inst = ir[index];
          if (inst.op == IROp::LABEL && inst.dest.isLabel()) {
            labelPositions[inst.dest.name] = index;
          }
          if ((inst.op == IROp::BRANCH || inst.op == IROp::BEQZ || inst.op == IROp::BNEZ) &&
              inst.dest.isLabel()) {
            ++labelReferences[inst.dest.name];
          }
        }

        bool summarized = false;
        for (std::size_t loopStart = ir.size(); loopStart-- > 0 && !summarized;) {
          if (ir[loopStart].op != IROp::BRANCH || !ir[loopStart].dest.isLabel() ||
              loopStart + 1 >= ir.size() || ir[loopStart + 1].op != IROp::LABEL ||
              !ir[loopStart + 1].dest.isLabel()) {
            continue;
          }
          const std::string condLabel = ir[loopStart].dest.name;
          const std::string bodyLabel = ir[loopStart + 1].dest.name;
          if (labelReferences[condLabel] != 1 || labelReferences[bodyLabel] != 1) {
            continue;
          }
          const auto condPosition = labelPositions.find(condLabel);
          if (condPosition == labelPositions.end() || condPosition->second <= loopStart + 2) {
            continue;
          }
          const std::size_t condIndex = condPosition->second;
          if (condIndex + 2 >= ir.size()) {
            continue;
          }
          const IRInst& condition = ir[condIndex + 1];
          const IRInst& backedge = ir[condIndex + 2];
          if ((condition.op != IROp::LT && condition.op != IROp::LE) ||
              !condition.dest.isLocalVar() || !condition.src1.isLocalVar() ||
              backedge.op != IROp::BNEZ || !backedge.dest.isLabel() ||
              backedge.dest.name != bodyLabel || !backedge.src1.isLocalVar() ||
              backedge.src1.name != condition.dest.name) {
            continue;
          }
          const std::string induction = condition.src1.name;

          const auto findNearbyConstant = [&](const std::string& name) -> std::optional<int> {
            for (std::size_t position = loopStart; position > 0; --position) {
              const IRInst& candidate = ir[position - 1];
              if (candidate.dest.isLocalVar() && candidate.dest.name == name) {
                if ((candidate.op == IROp::ASSIGN || candidate.op == IROp::LOCAL_VAR_DECL) &&
                    candidate.src1.isImm()) {
                  return candidate.src1.immVal;
                }
                return std::nullopt;
              }
              if (candidate.op == IROp::LABEL || candidate.op == IROp::BRANCH ||
                  candidate.op == IROp::BEQZ || candidate.op == IROp::BNEZ ||
                  candidate.op == IROp::CALL || candidate.op == IROp::FUNC_BEGIN) {
                break;
              }
            }
            return std::nullopt;
          };
          const auto findUniqueConstant = [&](const std::string& name) -> std::optional<int> {
            std::size_t functionBegin = loopStart;
            while (functionBegin > 0 && ir[functionBegin - 1].op != IROp::FUNC_BEGIN) {
              --functionBegin;
            }
            int definitions = 0;
            std::optional<int> value;
            for (std::size_t position = functionBegin; position < ir.size(); ++position) {
              const IRInst& candidate = ir[position];
              if (candidate.op == IROp::FUNC_END) {
                break;
              }
              if (!candidate.dest.isLocalVar() || candidate.dest.name != name ||
                  candidate.op == IROp::RETURN || candidate.op == IROp::PARAM ||
                  (candidate.op == IROp::LOCAL_VAR_DECL && candidate.src1.isNone())) {
                continue;
              }
              ++definitions;
              if ((candidate.op == IROp::ASSIGN || candidate.op == IROp::LOCAL_VAR_DECL) &&
                  candidate.src1.isImm()) {
                value = candidate.src1.immVal;
              } else {
                value.reset();
              }
            }
            return definitions == 1 ? value : std::nullopt;
          };

          const auto initial = findNearbyConstant(induction);
          std::optional<int> upper;
          if (condition.src2.isImm()) {
            upper = condition.src2.immVal;
          } else if (condition.src2.isLocalVar()) {
            upper = findNearbyConstant(condition.src2.name);
            if (!upper) {
              upper = findUniqueConstant(condition.src2.name);
            }
          }
          if (!initial || !upper || *initial < 0 ||
              (condition.op == IROp::LE && *upper == INT32_MAX && *initial <= *upper)) {
            continue;
          }
          std::int64_t trips = static_cast<std::int64_t>(*upper) - *initial;
          if (condition.op == IROp::LE) {
            ++trips;
          }
          trips = std::max<std::int64_t>(0, trips);
          const std::int64_t finalInduction = static_cast<std::int64_t>(*initial) + trips;
          if (trips < 2 || trips > INT32_MAX || finalInduction > INT32_MAX) {
            continue;
          }

          std::unordered_map<std::string, PeriodicValue> values;
          values[induction] = {PeriodicValue::Kind::INDUCTION, 0, 0, 0, 0};
          const auto valueOf = [&](const Operand& operand) {
            if (operand.isImm()) {
              return PeriodicValue{PeriodicValue::Kind::CONSTANT, operand.immVal, 0, 0, 1};
            }
            if (operand.isLocalVar()) {
              const auto found = values.find(operand.name);
              if (found != values.end()) {
                return found->second;
              }
              const auto constant = findUniqueConstant(operand.name);
              if (constant) {
                return PeriodicValue{PeriodicValue::Kind::CONSTANT, *constant, 0, 0, 1};
              }
            }
            return PeriodicValue{};
          };
          const auto isInduction = [](const PeriodicValue& value) {
            return value.kind == PeriodicValue::Kind::INDUCTION;
          };
          const auto absoluteDivisor = [](int divisor) {
            return static_cast<std::uint64_t>(divisor < 0 ? -static_cast<std::int64_t>(divisor)
                                                          : divisor);
          };
          const auto inductionRangeSafe = [&](int offset) {
            const std::int64_t lowest = static_cast<std::int64_t>(*initial) + offset;
            const std::int64_t highest = finalInduction - 1 + offset;
            return lowest >= 0 && highest <= INT32_MAX;
          };

          bool bodySupported = true;
          bool inductionIncremented = false;
          int inductionWrites = 0;
          std::unordered_set<std::string> written;
          std::unordered_set<std::string> periodicDefinitions;
          std::vector<PeriodicAccumulator> accumulators;
          for (std::size_t position = loopStart + 2; position < condIndex && bodySupported;
               ++position) {
            const IRInst& inst = ir[position];
            if (inst.op == IROp::LOCAL_VAR_DECL && inst.src1.isNone()) {
              continue;
            }
            if (!inst.dest.isLocalVar() || inductionIncremented) {
              bodySupported = false;
              break;
            }
            written.insert(inst.dest.name);

            if (inst.dest.name == induction) {
              ++inductionWrites;
              inductionIncremented = inductionWrites == 1 && inst.op == IROp::ADD &&
                                     inst.src1.isLocalVar() && inst.src1.name == induction &&
                                     inst.src2.isImm() && inst.src2.immVal == 1;
              if (!inductionIncremented) {
                bodySupported = false;
              }
              continue;
            }

            const PeriodicValue lhs = valueOf(inst.src1);
            const PeriodicValue rhs = valueOf(inst.src2);
            const bool lhsSelf = inst.src1.isLocalVar() && inst.src1.name == inst.dest.name;
            const bool rhsSelf = inst.src2.isLocalVar() && inst.src2.name == inst.dest.name;
            const PeriodicValue& selfValue = lhsSelf ? lhs : rhs;
            const PeriodicValue& deltaValue = lhsSelf ? rhs : lhs;
            if ((inst.op == IROp::ADD || inst.op == IROp::SUB) && (lhsSelf || rhsSelf) &&
                !(inst.op == IROp::SUB && rhsSelf) &&
                selfValue.kind == PeriodicValue::Kind::UNKNOWN &&
                deltaValue.kind == PeriodicValue::Kind::PERIODIC) {
              const auto duplicate = std::find_if(accumulators.begin(), accumulators.end(),
                                                  [&](const PeriodicAccumulator& accumulator) {
                                                    return accumulator.name == inst.dest.name;
                                                  });
              if (duplicate != accumulators.end()) {
                bodySupported = false;
                break;
              }
              accumulators.push_back({inst.dest.name, lhsSelf ? inst.src2 : inst.src1,
                                      inst.op == IROp::SUB ? -1 : 1, deltaValue.period});
              values.erase(inst.dest.name);
              continue;
            }

            PeriodicValue result;
            if (inst.op == IROp::ASSIGN || inst.op == IROp::LOCAL_VAR_DECL) {
              result = lhs;
            } else if (inst.op == IROp::NOT) {
              if (lhs.kind == PeriodicValue::Kind::CONSTANT) {
                result = {PeriodicValue::Kind::CONSTANT, lhs.value == 0 ? 1 : 0, 0, 0, 1};
              } else if (lhs.kind == PeriodicValue::Kind::PERIODIC) {
                result = {PeriodicValue::Kind::PERIODIC, 0, 0, 0, lhs.period};
              }
            } else if (inst.op == IROp::ADD || inst.op == IROp::SUB) {
              if (isInduction(lhs) && rhs.kind == PeriodicValue::Kind::CONSTANT) {
                result = lhs;
                result.offset =
                    inst.op == IROp::ADD ? lhs.offset + rhs.value : lhs.offset - rhs.value;
              } else if (inst.op == IROp::ADD && lhs.kind == PeriodicValue::Kind::CONSTANT &&
                         isInduction(rhs)) {
                result = rhs;
                result.offset += lhs.value;
              }
            } else if (inst.op == IROp::DIV && isInduction(lhs) &&
                       rhs.kind == PeriodicValue::Kind::CONSTANT && rhs.value != 0 &&
                       rhs.value != 1 && rhs.value != -1 && rhs.value != INT32_MIN) {
              if (inductionRangeSafe(lhs.offset)) {
                result = {PeriodicValue::Kind::QUOTIENT, 0, lhs.offset, rhs.value, 0};
              }
            } else if (inst.op == IROp::MOD && isInduction(lhs) &&
                       rhs.kind == PeriodicValue::Kind::CONSTANT && rhs.value != 0 &&
                       rhs.value != 1 && rhs.value != -1 && rhs.value != INT32_MIN) {
              const std::uint64_t period = absoluteDivisor(rhs.value);
              if (period <= kMaxPeriodicPhases && inductionRangeSafe(lhs.offset)) {
                result = {PeriodicValue::Kind::PERIODIC, 0, 0, 0, period};
              }
            } else if (inst.op == IROp::MUL) {
              const PeriodicValue* quotient = nullptr;
              const PeriodicValue* constant = nullptr;
              if (lhs.kind == PeriodicValue::Kind::QUOTIENT &&
                  rhs.kind == PeriodicValue::Kind::CONSTANT) {
                quotient = &lhs;
                constant = &rhs;
              } else if (rhs.kind == PeriodicValue::Kind::QUOTIENT &&
                         lhs.kind == PeriodicValue::Kind::CONSTANT) {
                quotient = &rhs;
                constant = &lhs;
              }
              if (quotient != nullptr && constant->value == quotient->divisor) {
                result = {PeriodicValue::Kind::QUOTIENT_PRODUCT, 0, quotient->offset,
                          quotient->divisor, 0};
              }
            }
            if (inst.op == IROp::SUB && isInduction(lhs) &&
                rhs.kind == PeriodicValue::Kind::QUOTIENT_PRODUCT && lhs.offset == rhs.offset) {
              const std::uint64_t period = absoluteDivisor(rhs.divisor);
              if (period <= kMaxPeriodicPhases) {
                result = {PeriodicValue::Kind::PERIODIC, 0, 0, 0, period};
              }
            }

            if (result.kind == PeriodicValue::Kind::UNKNOWN &&
                (inst.op == IROp::ADD || inst.op == IROp::SUB || inst.op == IROp::MUL ||
                 inst.op == IROp::DIV || inst.op == IROp::MOD || inst.op == IROp::LT ||
                 inst.op == IROp::GT || inst.op == IROp::LE || inst.op == IROp::GE ||
                 inst.op == IROp::EQ || inst.op == IROp::NE)) {
              const bool lhsPeriodic = lhs.kind == PeriodicValue::Kind::CONSTANT ||
                                       lhs.kind == PeriodicValue::Kind::PERIODIC;
              const bool rhsPeriodic = rhs.kind == PeriodicValue::Kind::CONSTANT ||
                                       rhs.kind == PeriodicValue::Kind::PERIODIC;
              if (lhsPeriodic && rhsPeriodic) {
                if (lhs.kind == PeriodicValue::Kind::CONSTANT &&
                    rhs.kind == PeriodicValue::Kind::CONSTANT) {
                  const auto folded = wrappedBinary(inst.op, lhs.value, rhs.value);
                  if (folded) {
                    result = {PeriodicValue::Kind::CONSTANT, *folded, 0, 0, 1};
                  }
                } else {
                  const std::uint64_t period = combinePeriod(lhs.period, rhs.period);
                  if (period != 0) {
                    result = {PeriodicValue::Kind::PERIODIC, 0, 0, 0, period};
                  }
                }
              }
            }
            if (result.kind == PeriodicValue::Kind::UNKNOWN) {
              bodySupported = false;
              break;
            }
            values[inst.dest.name] = result;
            periodicDefinitions.insert(inst.dest.name);
          }
          if (!bodySupported || inductionWrites != 1 || !inductionIncremented ||
              accumulators.empty() ||
              (condition.src2.isLocalVar() && written.count(condition.src2.name) != 0)) {
            continue;
          }

          std::uint64_t period = 1;
          for (const PeriodicAccumulator& accumulator : accumulators) {
            period = combinePeriod(period, accumulator.period);
            if (period == 0) {
              break;
            }
          }
          if (period <= 1 || period > kMaxPeriodicPhases ||
              static_cast<std::int64_t>(*initial) + static_cast<std::int64_t>(period) - 1 >
                  INT32_MAX) {
            continue;
          }

          const std::size_t loopEnd = condIndex + 3;
          const auto usedAfterLoop = [&](const std::string& name) {
            for (std::size_t position = loopEnd; position < ir.size(); ++position) {
              const IRInst& inst = ir[position];
              if (inst.op == IROp::FUNC_BEGIN || inst.op == IROp::FUNC_END) {
                break;
              }
              if ((inst.src1.isLocalVar() && inst.src1.name == name) ||
                  (inst.src2.isLocalVar() && inst.src2.name == name) ||
                  ((inst.op == IROp::RETURN || inst.op == IROp::PARAM) && inst.dest.isLocalVar() &&
                   inst.dest.name == name)) {
                return true;
              }
            }
            return false;
          };
          bool leaksPeriodicTemporary = false;
          for (const std::string& name : periodicDefinitions) {
            if (!isCompilerTemp(name) && usedAfterLoop(name)) {
              leaksPeriodicTemporary = true;
              break;
            }
          }
          if (leaksPeriodicTemporary) {
            continue;
          }

          std::vector<std::uint32_t> cycleDeltas(accumulators.size(), 0);
          std::vector<std::vector<std::uint32_t>> phaseDeltas(
              accumulators.size(), std::vector<std::uint32_t>(period, 0));
          bool evaluated = true;
          for (std::uint64_t phase = 0; phase < period && evaluated; ++phase) {
            std::unordered_map<std::string, int> concrete;
            concrete[induction] = *initial + static_cast<int>(phase);
            const auto concreteValue = [&](const Operand& operand) -> std::optional<int> {
              if (operand.isImm()) {
                return operand.immVal;
              }
              if (operand.isLocalVar()) {
                const auto found = concrete.find(operand.name);
                if (found != concrete.end()) {
                  return found->second;
                }
                return findUniqueConstant(operand.name);
              }
              return std::nullopt;
            };
            for (std::size_t position = loopStart + 2; position < condIndex && evaluated;
                 ++position) {
              const IRInst& inst = ir[position];
              if ((inst.op == IROp::LOCAL_VAR_DECL && inst.src1.isNone()) ||
                  inst.dest.name == induction) {
                continue;
              }
              const auto accumulator = std::find_if(accumulators.begin(), accumulators.end(),
                                                    [&](const PeriodicAccumulator& candidate) {
                                                      return candidate.name == inst.dest.name;
                                                    });
              if (accumulator != accumulators.end()) {
                const auto delta = concreteValue(accumulator->delta);
                if (!delta) {
                  evaluated = false;
                  break;
                }
                const std::size_t index =
                    static_cast<std::size_t>(accumulator - accumulators.begin());
                const std::uint32_t signedDelta = accumulator->sign > 0
                                                      ? static_cast<std::uint32_t>(*delta)
                                                      : 0u - static_cast<std::uint32_t>(*delta);
                phaseDeltas[index][phase] = signedDelta;
                cycleDeltas[index] += signedDelta;
                continue;
              }
              if (!inst.dest.isLocalVar()) {
                evaluated = false;
                break;
              }
              const auto lhs = concreteValue(inst.src1);
              if (!lhs) {
                evaluated = false;
                break;
              }
              std::optional<int> value;
              if (inst.op == IROp::ASSIGN || inst.op == IROp::LOCAL_VAR_DECL) {
                value = *lhs;
              } else if (inst.op == IROp::NOT) {
                value = *lhs == 0 ? 1 : 0;
              } else {
                const auto rhs = concreteValue(inst.src2);
                if (rhs) {
                  value = wrappedBinary(inst.op, *lhs, *rhs);
                }
              }
              if (!value) {
                evaluated = false;
                break;
              }
              concrete[inst.dest.name] = *value;
            }
          }
          if (!evaluated) {
            continue;
          }

          std::vector<IRInst> replacement;
          const std::uint64_t completePeriods = static_cast<std::uint64_t>(trips) / period;
          const std::uint64_t remainder = static_cast<std::uint64_t>(trips) % period;
          for (std::size_t index = 0; index < accumulators.size(); ++index) {
            if (!usedAfterLoop(accumulators[index].name)) {
              continue;
            }
            std::uint32_t delta = static_cast<std::uint32_t>(
                static_cast<std::uint64_t>(cycleDeltas[index]) * completePeriods);
            for (std::uint64_t phase = 0; phase < remainder; ++phase) {
              delta += phaseDeltas[index][phase];
            }
            if (delta != 0) {
              replacement.emplace_back(IROp::ADD, Operand::localVar(accumulators[index].name),
                                       Operand::localVar(accumulators[index].name),
                                       Operand::imm(static_cast<std::int32_t>(delta)));
            }
          }
          if (usedAfterLoop(induction)) {
            replacement.emplace_back(IROp::ASSIGN, Operand::localVar(induction),
                                     Operand::imm(static_cast<int>(finalInduction)),
                                     Operand::none());
          }

          std::size_t eraseEnd = loopEnd;
          if (eraseEnd < ir.size() && ir[eraseEnd].op == IROp::LABEL &&
              ir[eraseEnd].dest.isLabel() && labelReferences[ir[eraseEnd].dest.name] == 0) {
            ++eraseEnd;
          }
          ir.erase(ir.begin() + static_cast<std::ptrdiff_t>(loopStart),
                   ir.begin() + static_cast<std::ptrdiff_t>(eraseEnd));
          ir.insert(ir.begin() + static_cast<std::ptrdiff_t>(loopStart), replacement.begin(),
                    replacement.end());
          summarized = true;
          changed = true;
        }
        if (!summarized) {
          break;
        }
      }
    }

    // Pass 5.565: 汇总带非负循环不变偏移的余数累加。
    //
    // 对 `sum += (base + i) % m`，若 i 是常量边界的非负单步归纳变量，base
    // 在循环内不变且可证明非负，则余数每 m 轮重复。令 n 为迭代次数、r=base%m：
    //
    //   delta = floor(n/m) * m*(m-1)/2
    //         + (n%m)*r + (n%m)*(n%m-1)/2
    //         - m*max(0, r+(n%m)-m)
    //
    // 最后一项用比较结果 0/1 乘回候选值实现，不引入控制流。循环不变条件保护的
    // 同一累加也可复用 delta；负数跨零时 C 余数不具周期性，因此必须通过证明门槛。
    {
      struct GuardedAccumulator {
        std::string name;
        Operand guard;
        std::string label;
      };

      const auto sameOperand = [](const Operand& lhs, const Operand& rhs) {
        if (lhs.type != rhs.type) {
          return false;
        }
        if (lhs.isImm()) {
          return lhs.immVal == rhs.immVal;
        }
        return lhs.name == rhs.name;
      };

      for (int summaryRound = 0; summaryRound < 8; ++summaryRound) {
        std::unordered_map<std::string, std::size_t> labelPositions;
        std::unordered_map<std::string, int> labelReferences;
        for (std::size_t index = 0; index < ir.size(); ++index) {
          const IRInst& inst = ir[index];
          if (inst.op == IROp::LABEL && inst.dest.isLabel()) {
            labelPositions[inst.dest.name] = index;
          }
          if ((inst.op == IROp::BRANCH || inst.op == IROp::BEQZ || inst.op == IROp::BNEZ) &&
              inst.dest.isLabel()) {
            ++labelReferences[inst.dest.name];
          }
        }

        bool summarized = false;
        for (std::size_t loopStart = ir.size(); loopStart-- > 0 && !summarized;) {
          if (ir[loopStart].op != IROp::BRANCH || !ir[loopStart].dest.isLabel() ||
              loopStart + 1 >= ir.size() || ir[loopStart + 1].op != IROp::LABEL ||
              !ir[loopStart + 1].dest.isLabel()) {
            continue;
          }
          const std::string condLabel = ir[loopStart].dest.name;
          const std::string bodyLabel = ir[loopStart + 1].dest.name;
          if (labelReferences[condLabel] != 1 || labelReferences[bodyLabel] != 1) {
            continue;
          }
          const auto condFound = labelPositions.find(condLabel);
          if (condFound == labelPositions.end() || condFound->second <= loopStart + 2) {
            continue;
          }
          const std::size_t condIndex = condFound->second;
          if (condIndex + 2 >= ir.size()) {
            continue;
          }
          const IRInst& condition = ir[condIndex + 1];
          const IRInst& backedge = ir[condIndex + 2];
          if ((condition.op != IROp::LT && condition.op != IROp::LE) ||
              !condition.dest.isLocalVar() || !condition.src1.isLocalVar() ||
              backedge.op != IROp::BNEZ || !backedge.dest.isLabel() ||
              backedge.dest.name != bodyLabel || !backedge.src1.isLocalVar() ||
              backedge.src1.name != condition.dest.name) {
            continue;
          }
          const std::string induction = condition.src1.name;

          std::size_t functionBegin = loopStart;
          while (functionBegin > 0 && ir[functionBegin].op != IROp::FUNC_BEGIN) {
            --functionBegin;
          }
          std::size_t functionEnd = condIndex + 3;
          while (functionEnd < ir.size() && ir[functionEnd].op != IROp::FUNC_END) {
            ++functionEnd;
          }

          const auto findNearbyConstant = [&](const std::string& name) -> std::optional<int> {
            for (std::size_t position = loopStart; position > functionBegin; --position) {
              const IRInst& candidate = ir[position - 1];
              if (candidate.dest.isLocalVar() && candidate.dest.name == name) {
                if ((candidate.op == IROp::ASSIGN || candidate.op == IROp::LOCAL_VAR_DECL) &&
                    candidate.src1.isImm()) {
                  return candidate.src1.immVal;
                }
                return std::nullopt;
              }
              if (candidate.op == IROp::LABEL || candidate.op == IROp::BRANCH ||
                  candidate.op == IROp::BEQZ || candidate.op == IROp::BNEZ ||
                  candidate.op == IROp::CALL) {
                break;
              }
            }
            return std::nullopt;
          };
          const auto findUniqueConstant = [&](const std::string& name) -> std::optional<int> {
            int definitions = 0;
            std::optional<int> value;
            for (std::size_t position = functionBegin; position < functionEnd; ++position) {
              const IRInst& candidate = ir[position];
              if (!candidate.dest.isLocalVar() || candidate.dest.name != name ||
                  candidate.op == IROp::RETURN || candidate.op == IROp::PARAM ||
                  (candidate.op == IROp::LOCAL_VAR_DECL && candidate.src1.isNone())) {
                continue;
              }
              ++definitions;
              if ((candidate.op == IROp::ASSIGN || candidate.op == IROp::LOCAL_VAR_DECL) &&
                  candidate.src1.isImm()) {
                value = candidate.src1.immVal;
              } else {
                value.reset();
              }
            }
            return definitions == 1 ? value : std::nullopt;
          };

          const auto initial = findNearbyConstant(induction);
          std::optional<int> upper;
          if (condition.src2.isImm()) {
            upper = condition.src2.immVal;
          } else if (condition.src2.isLocalVar()) {
            upper = findNearbyConstant(condition.src2.name);
            if (!upper) {
              upper = findUniqueConstant(condition.src2.name);
            }
          }
          if (!initial || !upper || *initial < 0 ||
              (condition.op == IROp::LE && *upper == INT32_MAX)) {
            continue;
          }
          std::int64_t trips = static_cast<std::int64_t>(*upper) - *initial;
          if (condition.op == IROp::LE) {
            ++trips;
          }
          trips = std::max<std::int64_t>(0, trips);
          if (trips < 2 || trips > INT32_MAX) {
            continue;
          }

          std::unordered_set<std::string> written;
          for (std::size_t position = loopStart + 2; position < condIndex; ++position) {
            const IRInst& inst = ir[position];
            if (inst.dest.isLocalVar() && inst.op != IROp::RETURN && inst.op != IROp::PARAM) {
              written.insert(inst.dest.name);
            }
          }
          if (condition.src2.isLocalVar() && written.count(condition.src2.name) != 0) {
            continue;
          }

          // 证明某局部变量在本函数中所有定义都保持非负。自增定义可以依赖自身，
          // 但必须另有一个不依赖自身的非负根定义，避免循环论证未初始化值。
          std::unordered_map<std::string, int> nonNegativeMemo;
          std::function<bool(const std::string&)> proveNonNegative;
          proveNonNegative = [&](const std::string& name) {
            const auto memo = nonNegativeMemo.find(name);
            if (memo != nonNegativeMemo.end()) {
              return memo->second == 2;
            }
            nonNegativeMemo[name] = 1;
            bool sawDefinition = false;
            bool sawGroundDefinition = false;
            for (std::size_t position = functionBegin; position < functionEnd; ++position) {
              const IRInst& inst = ir[position];
              if (!inst.dest.isLocalVar() || inst.dest.name != name || inst.op == IROp::RETURN ||
                  inst.op == IROp::PARAM ||
                  (inst.op == IROp::LOCAL_VAR_DECL && inst.src1.isNone())) {
                continue;
              }
              sawDefinition = true;
              const auto operandProof = [&](const Operand& operand) {
                if (operand.isImm()) {
                  return std::pair<bool, bool>{operand.immVal >= 0, false};
                }
                if (!operand.isLocalVar()) {
                  return std::pair<bool, bool>{false, false};
                }
                if (operand.name == name) {
                  return std::pair<bool, bool>{true, true};
                }
                return std::pair<bool, bool>{proveNonNegative(operand.name), false};
              };
              const auto lhs = operandProof(inst.src1);
              const auto rhs = operandProof(inst.src2);
              bool safe = false;
              bool selfDependent = lhs.second || rhs.second;
              if (inst.op == IROp::ASSIGN || inst.op == IROp::LOCAL_VAR_DECL) {
                safe = lhs.first;
                selfDependent = lhs.second;
              } else if (inst.op == IROp::ADD || inst.op == IROp::MUL) {
                safe = lhs.first && rhs.first;
              } else if ((inst.op == IROp::DIV || inst.op == IROp::MOD) && lhs.first &&
                         inst.src2.isImm() && inst.src2.immVal > 0) {
                safe = true;
              } else if (inst.op == IROp::NOT || inst.op == IROp::LT || inst.op == IROp::GT ||
                         inst.op == IROp::LE || inst.op == IROp::GE || inst.op == IROp::EQ ||
                         inst.op == IROp::NE) {
                safe = true;
                selfDependent = false;
              }
              if (!safe) {
                nonNegativeMemo[name] = 3;
                return false;
              }
              if (!selfDependent) {
                sawGroundDefinition = true;
              }
            }
            const bool proven = sawDefinition && sawGroundDefinition;
            nonNegativeMemo[name] = proven ? 2 : 3;
            return proven;
          };

          std::optional<std::size_t> chainBegin;
          Operand invariantBase;
          std::string remainderName;
          int modulus = 0;
          for (std::size_t position = loopStart + 2; position + 3 < condIndex; ++position) {
            const IRInst& add = ir[position];
            const IRInst& quotient = ir[position + 1];
            const IRInst& product = ir[position + 2];
            const IRInst& remainder = ir[position + 3];
            if (add.op != IROp::ADD || !add.dest.isLocalVar()) {
              continue;
            }
            if (add.src1.isLocalVar() && add.src1.name == induction) {
              invariantBase = add.src2;
            } else if (add.src2.isLocalVar() && add.src2.name == induction) {
              invariantBase = add.src1;
            } else {
              continue;
            }
            if ((!invariantBase.isLocalVar() && !invariantBase.isImm()) ||
                (invariantBase.isLocalVar() && written.count(invariantBase.name) != 0) ||
                (invariantBase.isImm() && invariantBase.immVal < 0) ||
                (invariantBase.isLocalVar() && !proveNonNegative(invariantBase.name))) {
              continue;
            }
            if (quotient.op != IROp::DIV || !quotient.dest.isLocalVar() ||
                !sameOperand(quotient.src1, add.dest) || !quotient.src2.isImm() ||
                quotient.src2.immVal <= 1 || product.op != IROp::MUL ||
                !product.dest.isLocalVar() || !product.src2.isImm() ||
                product.src2.immVal != quotient.src2.immVal ||
                !sameOperand(product.src1, quotient.dest) || remainder.op != IROp::SUB ||
                !remainder.dest.isLocalVar() || !sameOperand(remainder.src1, add.dest) ||
                !sameOperand(remainder.src2, product.dest)) {
              continue;
            }
            chainBegin = position;
            remainderName = remainder.dest.name;
            modulus = quotient.src2.immVal;
            break;
          }
          if (!chainBegin || modulus <= 1) {
            continue;
          }

          std::vector<std::string> directAccumulators;
          std::vector<GuardedAccumulator> guardedAccumulators;
          std::unordered_set<std::size_t> allowed = {*chainBegin, *chainBegin + 1, *chainBegin + 2,
                                                     *chainBegin + 3};
          bool inductionIncremented = false;
          for (std::size_t position = loopStart + 2; position < condIndex;) {
            const IRInst& inst = ir[position];
            if (allowed.count(position) != 0 ||
                (inst.op == IROp::LOCAL_VAR_DECL && inst.src1.isNone())) {
              ++position;
              continue;
            }
            if (inst.op == IROp::ADD && inst.dest.isLocalVar() && inst.dest.name == induction &&
                inst.src1.isLocalVar() && inst.src1.name == induction && inst.src2.isImm() &&
                inst.src2.immVal == 1) {
              inductionIncremented = !inductionIncremented;
              if (!inductionIncremented) {
                break;
              }
              ++position;
              continue;
            }
            const bool directAccumulator =
                inst.op == IROp::ADD && inst.dest.isLocalVar() && inst.src1.isLocalVar() &&
                inst.src1.name == inst.dest.name && inst.src2.isLocalVar() &&
                inst.src2.name == remainderName;
            if (directAccumulator) {
              directAccumulators.push_back(inst.dest.name);
              ++position;
              continue;
            }
            if (inst.op == IROp::BEQZ && inst.dest.isLabel() && inst.src1.isLocalVar() &&
                position + 2 < condIndex) {
              const IRInst& update = ir[position + 1];
              const IRInst& join = ir[position + 2];
              const bool guardedAccumulator =
                  update.op == IROp::ADD && update.dest.isLocalVar() && update.src1.isLocalVar() &&
                  update.src1.name == update.dest.name && update.src2.isLocalVar() &&
                  update.src2.name == remainderName && join.op == IROp::LABEL &&
                  join.dest.isLabel() && join.dest.name == inst.dest.name &&
                  labelReferences[inst.dest.name] == 1 && written.count(inst.src1.name) == 0;
              if (guardedAccumulator) {
                guardedAccumulators.push_back({update.dest.name, inst.src1, inst.dest.name});
                position += 3;
                continue;
              }
            }
            break;
          }
          if (!inductionIncremented ||
              (directAccumulators.empty() && guardedAccumulators.empty())) {
            continue;
          }

          // 确认循环体没有未识别的状态变化。
          std::size_t recognized =
              4 + 1 + directAccumulators.size() + guardedAccumulators.size() * 3;
          for (std::size_t position = loopStart + 2; position < condIndex; ++position) {
            if (ir[position].op == IROp::LOCAL_VAR_DECL && ir[position].src1.isNone()) {
              ++recognized;
            }
          }
          if (recognized != condIndex - (loopStart + 2)) {
            continue;
          }

          const std::size_t loopEnd = condIndex + 3;
          const auto usedAfterLoop = [&](const std::string& name) {
            for (std::size_t position = loopEnd; position < functionEnd; ++position) {
              const IRInst& inst = ir[position];
              if ((inst.src1.isLocalVar() && inst.src1.name == name) ||
                  (inst.src2.isLocalVar() && inst.src2.name == name) ||
                  ((inst.op == IROp::RETURN || inst.op == IROp::PARAM) && inst.dest.isLocalVar() &&
                   inst.dest.name == name)) {
                return true;
              }
            }
            return false;
          };
          if (usedAfterLoop(ir[*chainBegin].dest.name) ||
              usedAfterLoop(ir[*chainBegin + 1].dest.name) ||
              usedAfterLoop(ir[*chainBegin + 2].dest.name) || usedAfterLoop(remainderName)) {
            continue;
          }

          const std::int64_t completePeriods = trips / modulus;
          const int residual = static_cast<int>(trips % modulus);
          const std::int64_t constantDelta =
              completePeriods * modulus * (static_cast<std::int64_t>(modulus) - 1) / 2 +
              static_cast<std::int64_t>(residual) * (residual - 1) / 2;
          const std::int64_t maximumDelta = trips * (static_cast<std::int64_t>(modulus) - 1);
          if (constantDelta < 0 || constantDelta > INT32_MAX || maximumDelta > INT32_MAX) {
            continue;
          }

          std::vector<IRInst> replacement;
          Operand base = invariantBase;
          if (*initial != 0) {
            if (base.isImm()) {
              const std::int64_t shifted = static_cast<std::int64_t>(base.immVal) + *initial;
              if (shifted > INT32_MAX) {
                continue;
              }
              base = Operand::imm(static_cast<int>(shifted));
            } else {
              const std::string shifted = newTemp();
              replacement.emplace_back(IROp::ADD, Operand::localVar(shifted), base,
                                       Operand::imm(*initial));
              base = Operand::localVar(shifted);
            }
          }

          Operand delta = Operand::imm(static_cast<int>(constantDelta));
          if (residual != 0) {
            const std::string remainder = newTemp();
            replacement.emplace_back(IROp::MOD, Operand::localVar(remainder), base,
                                     Operand::imm(modulus));
            Operand partial = Operand::localVar(remainder);
            if (residual != 1) {
              const std::string scaled = newTemp();
              replacement.emplace_back(IROp::MUL, Operand::localVar(scaled), partial,
                                       Operand::imm(residual));
              partial = Operand::localVar(scaled);
            }
            if (constantDelta != 0) {
              const std::string withConstant = newTemp();
              replacement.emplace_back(IROp::ADD, Operand::localVar(withConstant), partial,
                                       Operand::imm(static_cast<int>(constantDelta)));
              partial = Operand::localVar(withConstant);
            }
            const std::string wrapCandidate = newTemp();
            replacement.emplace_back(IROp::ADD, Operand::localVar(wrapCandidate),
                                     Operand::localVar(remainder),
                                     Operand::imm(residual - modulus));
            const std::string wraps = newTemp();
            replacement.emplace_back(IROp::GT, Operand::localVar(wraps),
                                     Operand::localVar(wrapCandidate), Operand::imm(0));
            const std::string wrappedCount = newTemp();
            replacement.emplace_back(IROp::MUL, Operand::localVar(wrappedCount),
                                     Operand::localVar(wrapCandidate), Operand::localVar(wraps));
            const std::string correction = newTemp();
            replacement.emplace_back(IROp::MUL, Operand::localVar(correction),
                                     Operand::localVar(wrappedCount), Operand::imm(modulus));
            const std::string finalDelta = newTemp();
            replacement.emplace_back(IROp::SUB, Operand::localVar(finalDelta), partial,
                                     Operand::localVar(correction));
            delta = Operand::localVar(finalDelta);
          }

          for (const std::string& accumulator : directAccumulators) {
            replacement.emplace_back(IROp::ADD, Operand::localVar(accumulator),
                                     Operand::localVar(accumulator), delta);
          }
          for (const GuardedAccumulator& accumulator : guardedAccumulators) {
            replacement.emplace_back(IROp::BEQZ, Operand::label(accumulator.label),
                                     accumulator.guard, Operand::none());
            replacement.emplace_back(IROp::ADD, Operand::localVar(accumulator.name),
                                     Operand::localVar(accumulator.name), delta);
            replacement.emplace_back(IROp::LABEL, Operand::label(accumulator.label),
                                     Operand::none(), Operand::none());
          }
          if (usedAfterLoop(induction)) {
            replacement.emplace_back(IROp::ASSIGN, Operand::localVar(induction),
                                     Operand::imm(static_cast<int>(*initial + trips)),
                                     Operand::none());
          }

          std::size_t eraseEnd = loopEnd;
          if (eraseEnd < ir.size() && ir[eraseEnd].op == IROp::LABEL &&
              ir[eraseEnd].dest.isLabel() && labelReferences[ir[eraseEnd].dest.name] == 0) {
            ++eraseEnd;
          }
          ir.erase(ir.begin() + static_cast<std::ptrdiff_t>(loopStart),
                   ir.begin() + static_cast<std::ptrdiff_t>(eraseEnd));
          ir.insert(ir.begin() + static_cast<std::ptrdiff_t>(loopStart), replacement.begin(),
                    replacement.end());
          summarized = true;
          changed = true;
        }
        if (!summarized) {
          break;
        }
      }
    }

    // Pass 5.57: 汇总单个有界模状态驱动的自主循环。
    //
    // `state = transition(state) % m; sum += f(state)` 不是归纳变量周期，也不是
    // 仿射递推，但更新后的有符号余数只能落在 [-(m-1), m-1]。对小常量 m，
    // 状态转移必在至多 2*m-1 个状态内进入环。这里解释的是这一有限状态转移，
    // 而不是按源循环的千万/亿次迭代执行；累加器用符号系数证明仅作独立加法。
    {
      constexpr int kMaxStateModulus = 16384;
      constexpr int kMaxAccumulators = 8;

      struct SymbolicStateValue {
        std::int32_t constant = 0;
        std::vector<std::uint32_t> coefficients;
      };
      struct StateTransition {
        std::int32_t nextState = 0;
        std::vector<std::uint32_t> deltas;
      };

      const auto sameOperand = [](const Operand& lhs, const Operand& rhs) {
        if (lhs.type != rhs.type) {
          return false;
        }
        if (lhs.isImm()) {
          return lhs.immVal == rhs.immVal;
        }
        if (lhs.isLocalVar() || lhs.isGlobalVar() || lhs.isLabel() || lhs.isParam()) {
          return lhs.name == rhs.name;
        }
        return lhs.isNone() && rhs.isNone();
      };
      const auto wrappedBinary = [](IROp op, std::int32_t lhs,
                                    std::int32_t rhs) -> std::optional<std::int32_t> {
        switch (op) {
        case IROp::ADD:
          return static_cast<std::int32_t>(static_cast<std::uint32_t>(lhs) +
                                           static_cast<std::uint32_t>(rhs));
        case IROp::SUB:
          return static_cast<std::int32_t>(static_cast<std::uint32_t>(lhs) -
                                           static_cast<std::uint32_t>(rhs));
        case IROp::MUL:
          return static_cast<std::int32_t>(static_cast<std::uint32_t>(
              static_cast<std::uint64_t>(static_cast<std::uint32_t>(lhs)) *
              static_cast<std::uint32_t>(rhs)));
        case IROp::DIV:
          if (rhs == 0 || (lhs == INT32_MIN && rhs == -1)) {
            return std::nullopt;
          }
          return lhs / rhs;
        case IROp::MOD:
          if (rhs == 0 || (lhs == INT32_MIN && rhs == -1)) {
            return std::nullopt;
          }
          return lhs % rhs;
        case IROp::LT:
          return lhs < rhs ? 1 : 0;
        case IROp::GT:
          return lhs > rhs ? 1 : 0;
        case IROp::LE:
          return lhs <= rhs ? 1 : 0;
        case IROp::GE:
          return lhs >= rhs ? 1 : 0;
        case IROp::EQ:
          return lhs == rhs ? 1 : 0;
        case IROp::NE:
          return lhs != rhs ? 1 : 0;
        default:
          return std::nullopt;
        }
      };

      for (int summaryRound = 0; summaryRound < 8; ++summaryRound) {
        std::unordered_map<std::string, int> labelReferences;
        std::unordered_map<std::string, std::size_t> labelPositions;
        for (std::size_t index = 0; index < ir.size(); ++index) {
          const IRInst& inst = ir[index];
          if (inst.op == IROp::LABEL && inst.dest.isLabel()) {
            labelPositions[inst.dest.name] = index;
          }
          if ((inst.op == IROp::BRANCH || inst.op == IROp::BEQZ || inst.op == IROp::BNEZ) &&
              inst.dest.isLabel()) {
            ++labelReferences[inst.dest.name];
          }
        }

        bool summarized = false;
        for (std::size_t loopStart = ir.size(); loopStart-- > 0 && !summarized;) {
          if (ir[loopStart].op != IROp::BRANCH || !ir[loopStart].dest.isLabel() ||
              loopStart + 1 >= ir.size() || ir[loopStart + 1].op != IROp::LABEL ||
              !ir[loopStart + 1].dest.isLabel()) {
            continue;
          }
          const std::string condLabel = ir[loopStart].dest.name;
          const std::string bodyLabel = ir[loopStart + 1].dest.name;
          if (labelReferences[condLabel] != 1 || labelReferences[bodyLabel] != 1) {
            continue;
          }
          const auto condPosition = labelPositions.find(condLabel);
          if (condPosition == labelPositions.end() || condPosition->second <= loopStart + 2) {
            continue;
          }
          const std::size_t condIndex = condPosition->second;
          if (condIndex + 2 >= ir.size()) {
            continue;
          }
          const IRInst& condition = ir[condIndex + 1];
          const IRInst& backedge = ir[condIndex + 2];
          if ((condition.op != IROp::LT && condition.op != IROp::LE) ||
              !condition.dest.isLocalVar() || !condition.src1.isLocalVar() ||
              backedge.op != IROp::BNEZ || !backedge.dest.isLabel() ||
              backedge.dest.name != bodyLabel || !backedge.src1.isLocalVar() ||
              backedge.src1.name != condition.dest.name) {
            continue;
          }
          const std::string induction = condition.src1.name;

          const auto findNearbyConstant = [&](const std::string& name) -> std::optional<int> {
            for (std::size_t position = loopStart; position > 0; --position) {
              const IRInst& candidate = ir[position - 1];
              if (candidate.dest.isLocalVar() && candidate.dest.name == name) {
                if ((candidate.op == IROp::ASSIGN || candidate.op == IROp::LOCAL_VAR_DECL) &&
                    candidate.src1.isImm()) {
                  return candidate.src1.immVal;
                }
                return std::nullopt;
              }
              if (candidate.op == IROp::LABEL || candidate.op == IROp::BRANCH ||
                  candidate.op == IROp::BEQZ || candidate.op == IROp::BNEZ ||
                  candidate.op == IROp::CALL || candidate.op == IROp::FUNC_BEGIN) {
                break;
              }
            }
            return std::nullopt;
          };
          const auto findUniqueConstant = [&](const std::string& name) -> std::optional<int> {
            std::size_t functionBegin = loopStart;
            while (functionBegin > 0 && ir[functionBegin - 1].op != IROp::FUNC_BEGIN) {
              --functionBegin;
            }
            int definitions = 0;
            std::optional<int> value;
            for (std::size_t position = functionBegin; position < ir.size(); ++position) {
              const IRInst& candidate = ir[position];
              if (candidate.op == IROp::FUNC_END) {
                break;
              }
              if (!candidate.dest.isLocalVar() || candidate.dest.name != name ||
                  candidate.op == IROp::RETURN || candidate.op == IROp::PARAM ||
                  (candidate.op == IROp::LOCAL_VAR_DECL && candidate.src1.isNone())) {
                continue;
              }
              ++definitions;
              if ((candidate.op == IROp::ASSIGN || candidate.op == IROp::LOCAL_VAR_DECL) &&
                  candidate.src1.isImm()) {
                value = candidate.src1.immVal;
              } else {
                value.reset();
              }
            }
            return definitions == 1 ? value : std::nullopt;
          };

          const auto initial = findNearbyConstant(induction);
          std::optional<int> upper;
          if (condition.src2.isImm()) {
            upper = condition.src2.immVal;
          } else if (condition.src2.isLocalVar()) {
            upper = findNearbyConstant(condition.src2.name);
            if (!upper) {
              upper = findUniqueConstant(condition.src2.name);
            }
          }
          if (!initial || !upper ||
              (condition.op == IROp::LE && *upper == INT32_MAX && *initial <= *upper)) {
            continue;
          }
          std::int64_t trips = static_cast<std::int64_t>(*upper) - *initial;
          if (condition.op == IROp::LE) {
            ++trips;
          }
          trips = std::max<std::int64_t>(0, trips);
          const std::int64_t finalInduction = static_cast<std::int64_t>(*initial) + trips;
          if (trips < 1000 || trips > INT32_MAX || finalInduction < INT32_MIN ||
              finalInduction > INT32_MAX) {
            continue;
          }

          bool bodySupported = true;
          bool inductionIncremented = false;
          int inductionWrites = 0;
          std::unordered_map<std::string, int> writeCounts;
          for (std::size_t position = loopStart + 2; position < condIndex && bodySupported;
               ++position) {
            const IRInst& inst = ir[position];
            if (inst.op == IROp::LOCAL_VAR_DECL && inst.src1.isNone()) {
              continue;
            }
            if (!inst.dest.isLocalVar() || inductionIncremented) {
              bodySupported = false;
              break;
            }
            ++writeCounts[inst.dest.name];
            if (inst.dest.name == induction) {
              ++inductionWrites;
              inductionIncremented = inductionWrites == 1 && inst.op == IROp::ADD &&
                                     inst.src1.isLocalVar() && inst.src1.name == induction &&
                                     inst.src2.isImm() && inst.src2.immVal == 1;
              if (!inductionIncremented) {
                bodySupported = false;
              }
              continue;
            }
            switch (inst.op) {
            case IROp::LOCAL_VAR_DECL:
            case IROp::ASSIGN:
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
              break;
            default:
              bodySupported = false;
              break;
            }
          }
          if (!bodySupported || inductionWrites != 1 || !inductionIncremented ||
              (condition.src2.isLocalVar() && writeCounts.count(condition.src2.name) != 0)) {
            continue;
          }

          const std::size_t loopEnd = condIndex + 3;
          const auto usedAfterLoop = [&](const std::string& name) {
            for (std::size_t position = loopEnd; position < ir.size(); ++position) {
              const IRInst& inst = ir[position];
              if (inst.op == IROp::FUNC_BEGIN || inst.op == IROp::FUNC_END) {
                break;
              }
              if ((inst.src1.isLocalVar() && inst.src1.name == name) ||
                  (inst.src2.isLocalVar() && inst.src2.name == name) ||
                  ((inst.op == IROp::RETURN || inst.op == IROp::PARAM) && inst.dest.isLocalVar() &&
                   inst.dest.name == name)) {
                return true;
              }
            }
            return false;
          };
          const auto previousBodyDefinition =
              [&](const std::string& name, std::size_t before) -> std::optional<std::size_t> {
            for (std::size_t position = before; position > loopStart + 2; --position) {
              const IRInst& candidate = ir[position - 1];
              if (candidate.dest.isLocalVar() && candidate.dest.name == name &&
                  !(candidate.op == IROp::LOCAL_VAR_DECL && candidate.src1.isNone())) {
                return position - 1;
              }
            }
            return std::nullopt;
          };
          const auto reducedModulus = [&](const std::string& state) -> std::optional<int> {
            if (writeCounts[state] != 1) {
              return std::nullopt;
            }
            for (std::size_t position = loopStart + 2; position < condIndex; ++position) {
              const IRInst& inst = ir[position];
              if (!inst.dest.isLocalVar() || inst.dest.name != state) {
                continue;
              }
              if (inst.op == IROp::MOD && inst.src2.isImm() && inst.src2.immVal > 1 &&
                  inst.src2.immVal <= kMaxStateModulus) {
                return inst.src2.immVal;
              }
              if (inst.op != IROp::SUB || !inst.src2.isLocalVar()) {
                return std::nullopt;
              }
              const auto productDefinition = previousBodyDefinition(inst.src2.name, position);
              if (!productDefinition) {
                return std::nullopt;
              }
              const IRInst& product = ir[*productDefinition];
              const Operand* quotientOperand = nullptr;
              const Operand* divisorOperand = nullptr;
              if (product.op == IROp::MUL && product.src1.isLocalVar() && product.src2.isImm()) {
                quotientOperand = &product.src1;
                divisorOperand = &product.src2;
              } else if (product.op == IROp::MUL && product.src2.isLocalVar() &&
                         product.src1.isImm()) {
                quotientOperand = &product.src2;
                divisorOperand = &product.src1;
              }
              if (quotientOperand == nullptr || divisorOperand->immVal <= 1 ||
                  divisorOperand->immVal > kMaxStateModulus) {
                return std::nullopt;
              }
              const auto quotientDefinition =
                  previousBodyDefinition(quotientOperand->name, *productDefinition);
              if (!quotientDefinition) {
                return std::nullopt;
              }
              const IRInst& quotient = ir[*quotientDefinition];
              if (quotient.op != IROp::DIV || !quotient.src2.isImm() ||
                  quotient.src2.immVal != divisorOperand->immVal ||
                  !sameOperand(quotient.src1, inst.src1)) {
                return std::nullopt;
              }
              return divisorOperand->immVal;
            }
            return std::nullopt;
          };

          bool leaksCompilerTemporary = false;
          for (const auto& [name, writes] : writeCounts) {
            if (writes > 0 && isCompilerTemp(name) && usedAfterLoop(name)) {
              leaksCompilerTemporary = true;
              break;
            }
          }
          if (leaksCompilerTemporary) {
            continue;
          }

          std::vector<std::string> stateCandidates;
          for (const auto& [name, writes] : writeCounts) {
            if (writes > 0 && name != induction && !isCompilerTemp(name) && reducedModulus(name)) {
              stateCandidates.push_back(name);
            }
          }
          std::sort(stateCandidates.begin(), stateCandidates.end());

          for (const std::string& state : stateCandidates) {
            const auto modulus = reducedModulus(state);
            const auto initialState = findNearbyConstant(state);
            if (!modulus || !initialState) {
              continue;
            }

            std::vector<std::string> accumulators;
            for (const auto& [name, writes] : writeCounts) {
              if (writes == 0 || name == induction || name == state || isCompilerTemp(name)) {
                continue;
              }
              if (usedAfterLoop(name)) {
                accumulators.push_back(name);
              }
            }
            if (accumulators.size() > kMaxAccumulators) {
              continue;
            }
            std::sort(accumulators.begin(), accumulators.end());

            const std::size_t coefficientCount = accumulators.size() + 1;
            const auto coefficientsAreZero = [](const SymbolicStateValue& value) {
              return std::all_of(value.coefficients.begin(), value.coefficients.end(),
                                 [](std::uint32_t coefficient) { return coefficient == 0; });
            };
            const auto combineValues = [](const SymbolicStateValue& lhs,
                                          const SymbolicStateValue& rhs, bool subtract) {
              SymbolicStateValue result;
              result.constant = static_cast<std::int32_t>(
                  subtract ? static_cast<std::uint32_t>(lhs.constant) -
                                 static_cast<std::uint32_t>(rhs.constant)
                           : static_cast<std::uint32_t>(lhs.constant) +
                                 static_cast<std::uint32_t>(rhs.constant));
              result.coefficients.resize(lhs.coefficients.size());
              for (std::size_t index = 0; index < result.coefficients.size(); ++index) {
                result.coefficients[index] =
                    subtract ? lhs.coefficients[index] - rhs.coefficients[index]
                             : lhs.coefficients[index] + rhs.coefficients[index];
              }
              return result;
            };
            const auto scaleValue = [](SymbolicStateValue value, std::uint32_t factor) {
              value.constant = static_cast<std::int32_t>(static_cast<std::uint32_t>(
                  static_cast<std::uint64_t>(static_cast<std::uint32_t>(value.constant)) * factor));
              for (auto& coefficient : value.coefficients) {
                coefficient =
                    static_cast<std::uint32_t>(static_cast<std::uint64_t>(coefficient) * factor);
              }
              return value;
            };

            const auto evaluateTransition =
                [&](std::int32_t currentState) -> std::optional<StateTransition> {
              std::unordered_map<std::string, SymbolicStateValue> values;
              values[state] = {currentState, std::vector<std::uint32_t>(coefficientCount, 0)};
              for (std::size_t index = 0; index < accumulators.size(); ++index) {
                SymbolicStateValue accumulator{0, std::vector<std::uint32_t>(coefficientCount, 0)};
                accumulator.coefficients[index] = 1;
                values[accumulators[index]] = std::move(accumulator);
              }
              SymbolicStateValue symbolicInduction{0,
                                                   std::vector<std::uint32_t>(coefficientCount, 0)};
              symbolicInduction.coefficients.back() = 1;
              values[induction] = std::move(symbolicInduction);

              const auto valueOf =
                  [&](const Operand& operand) -> std::optional<SymbolicStateValue> {
                if (operand.isImm()) {
                  return SymbolicStateValue{operand.immVal,
                                            std::vector<std::uint32_t>(coefficientCount, 0)};
                }
                if (operand.isLocalVar()) {
                  const auto found = values.find(operand.name);
                  if (found != values.end()) {
                    return found->second;
                  }
                  const auto constant = findUniqueConstant(operand.name);
                  if (constant) {
                    return SymbolicStateValue{*constant,
                                              std::vector<std::uint32_t>(coefficientCount, 0)};
                  }
                }
                return std::nullopt;
              };

              for (std::size_t position = loopStart + 2; position < condIndex; ++position) {
                const IRInst& inst = ir[position];
                if (inst.op == IROp::LOCAL_VAR_DECL && inst.src1.isNone()) {
                  values.erase(inst.dest.name);
                  continue;
                }
                if (!inst.dest.isLocalVar()) {
                  return std::nullopt;
                }
                if (inst.dest.name == induction) {
                  continue;
                }
                const auto lhs = valueOf(inst.src1);
                if (!lhs) {
                  return std::nullopt;
                }
                std::optional<SymbolicStateValue> result;
                if (inst.op == IROp::ASSIGN || inst.op == IROp::LOCAL_VAR_DECL) {
                  result = *lhs;
                } else if (inst.op == IROp::NOT) {
                  if (coefficientsAreZero(*lhs)) {
                    result = SymbolicStateValue{lhs->constant == 0 ? 1 : 0,
                                                std::vector<std::uint32_t>(coefficientCount, 0)};
                  }
                } else {
                  const auto rhs = valueOf(inst.src2);
                  if (!rhs) {
                    return std::nullopt;
                  }
                  if (inst.op == IROp::ADD || inst.op == IROp::SUB) {
                    result = combineValues(*lhs, *rhs, inst.op == IROp::SUB);
                  } else if (inst.op == IROp::MUL) {
                    const bool lhsConstant = coefficientsAreZero(*lhs);
                    const bool rhsConstant = coefficientsAreZero(*rhs);
                    if (lhsConstant || rhsConstant) {
                      result = lhsConstant
                                   ? scaleValue(*rhs, static_cast<std::uint32_t>(lhs->constant))
                                   : scaleValue(*lhs, static_cast<std::uint32_t>(rhs->constant));
                    }
                  } else if (coefficientsAreZero(*lhs) && coefficientsAreZero(*rhs)) {
                    const auto folded = wrappedBinary(inst.op, lhs->constant, rhs->constant);
                    if (folded) {
                      result = SymbolicStateValue{*folded,
                                                  std::vector<std::uint32_t>(coefficientCount, 0)};
                    }
                  }
                }
                if (!result) {
                  return std::nullopt;
                }
                values[inst.dest.name] = std::move(*result);
              }

              const auto finalState = values.find(state);
              if (finalState == values.end() || !coefficientsAreZero(finalState->second)) {
                return std::nullopt;
              }
              StateTransition transition;
              transition.nextState = finalState->second.constant;
              transition.deltas.resize(accumulators.size());
              for (std::size_t row = 0; row < accumulators.size(); ++row) {
                const auto finalAccumulator = values.find(accumulators[row]);
                if (finalAccumulator == values.end()) {
                  return std::nullopt;
                }
                for (std::size_t column = 0; column < coefficientCount; ++column) {
                  const std::uint32_t expected = column == row ? 1u : 0u;
                  if (finalAccumulator->second.coefficients[column] != expected) {
                    return std::nullopt;
                  }
                }
                transition.deltas[row] =
                    static_cast<std::uint32_t>(finalAccumulator->second.constant);
              }
              return transition;
            };

            const std::uint64_t maximumStates = static_cast<std::uint64_t>(*modulus) * 2 + 1;
            // 小循环交给正常代码生成；本 Pass 只在状态空间远小于动态迭代数时
            // 寻找周期，避免退化成按源循环次数进行编译期求值。
            if (static_cast<std::uint64_t>(trips) <= maximumStates) {
              continue;
            }
            std::unordered_map<std::int32_t, std::uint64_t> seen;
            std::vector<std::int32_t> states;
            std::vector<std::vector<std::uint32_t>> totals;
            seen.reserve(static_cast<std::size_t>(maximumStates));
            states.reserve(static_cast<std::size_t>(maximumStates));
            totals.reserve(static_cast<std::size_t>(maximumStates));
            states.push_back(*initialState);
            totals.push_back(std::vector<std::uint32_t>(accumulators.size(), 0));
            seen.emplace(*initialState, 0);

            std::uint64_t simulated = 0;
            std::uint64_t cycleStart = 0;
            std::uint64_t cycleEnd = 0;
            bool foundCycle = false;
            bool evaluated = true;
            while (simulated < static_cast<std::uint64_t>(trips) && simulated < maximumStates) {
              const auto transition = evaluateTransition(states.back());
              if (!transition || transition->nextState <= -*modulus ||
                  transition->nextState >= *modulus) {
                evaluated = false;
                break;
              }
              std::vector<std::uint32_t> nextTotals = totals.back();
              for (std::size_t index = 0; index < accumulators.size(); ++index) {
                nextTotals[index] += transition->deltas[index];
              }
              ++simulated;
              states.push_back(transition->nextState);
              totals.push_back(std::move(nextTotals));
              const auto [found, inserted] = seen.emplace(states.back(), simulated);
              if (!inserted) {
                cycleStart = found->second;
                cycleEnd = simulated;
                foundCycle = true;
                break;
              }
            }
            if (!evaluated || (simulated < static_cast<std::uint64_t>(trips) && !foundCycle)) {
              continue;
            }

            std::int32_t finalState = states.back();
            std::vector<std::uint32_t> totalDelta(accumulators.size(), 0);
            if (simulated == static_cast<std::uint64_t>(trips)) {
              totalDelta = totals.back();
            } else {
              const std::uint64_t cycleLength = cycleEnd - cycleStart;
              const std::uint64_t remaining = static_cast<std::uint64_t>(trips) - cycleStart;
              const std::uint64_t completeCycles = remaining / cycleLength;
              const std::uint64_t remainder = remaining % cycleLength;
              finalState = states[static_cast<std::size_t>(cycleStart + remainder)];
              for (std::size_t index = 0; index < accumulators.size(); ++index) {
                const std::uint32_t prefix = totals[static_cast<std::size_t>(cycleStart)][index];
                const std::uint32_t cycle =
                    totals[static_cast<std::size_t>(cycleEnd)][index] - prefix;
                const std::uint32_t tail =
                    totals[static_cast<std::size_t>(cycleStart + remainder)][index] - prefix;
                totalDelta[index] =
                    prefix +
                    static_cast<std::uint32_t>(static_cast<std::uint64_t>(cycle) * completeCycles) +
                    tail;
              }
            }

            std::vector<IRInst> replacement;
            if (usedAfterLoop(state)) {
              replacement.emplace_back(IROp::ASSIGN, Operand::localVar(state),
                                       Operand::imm(finalState), Operand::none());
            }
            for (std::size_t index = 0; index < accumulators.size(); ++index) {
              if (totalDelta[index] != 0) {
                replacement.emplace_back(
                    IROp::ADD, Operand::localVar(accumulators[index]),
                    Operand::localVar(accumulators[index]),
                    Operand::imm(static_cast<std::int32_t>(totalDelta[index])));
              }
            }
            if (usedAfterLoop(induction)) {
              replacement.emplace_back(IROp::ASSIGN, Operand::localVar(induction),
                                       Operand::imm(static_cast<int>(finalInduction)),
                                       Operand::none());
            }

            std::size_t eraseEnd = loopEnd;
            if (eraseEnd < ir.size() && ir[eraseEnd].op == IROp::LABEL &&
                ir[eraseEnd].dest.isLabel() && labelReferences[ir[eraseEnd].dest.name] == 0) {
              ++eraseEnd;
            }
            ir.erase(ir.begin() + static_cast<std::ptrdiff_t>(loopStart),
                     ir.begin() + static_cast<std::ptrdiff_t>(eraseEnd));
            ir.insert(ir.begin() + static_cast<std::ptrdiff_t>(loopStart), replacement.begin(),
                      replacement.end());
            summarized = true;
            changed = true;
            break;
          }
        }
        if (!summarized) {
          break;
        }
      }
    }

    // Pass 5.6: 汇总耦合仿射状态循环。
    //
    // Pass 5.5 只接受 `x' = x + affine(invariants, induction)`，因此会保守拒绝
    // Fibonacci/线性状态机一类交叉递推。这里对同样严格的常数次数直线循环构造
    // 一次迭代的增广矩阵 state' = M * state，并用二进制快速幂得到 M^N。变换
    // 结果仍是普通的 RV32 三地址算术；循环有分支、调用、全局状态、可变上界，
    // 或归纳变量不是严格 +1 时均不应用。
    {
      // 覆盖百状态 helper 及其表达式临时量，同时给 O(n^3) 矩阵乘法保留
      // 明确的编译时上限；超出预算仍保留源循环，避免无界快速幂消耗。
      constexpr std::size_t kMaxCoupledAffineVariables = 384;
      using AffineRow = std::vector<std::uint32_t>;
      using AffineMatrix = std::vector<AffineRow>;

      const auto identityMatrix = [](std::size_t dimension) {
        AffineMatrix identity(dimension, AffineRow(dimension, 0));
        for (std::size_t index = 0; index < dimension; ++index) {
          identity[index][index] = 1;
        }
        return identity;
      };
      const auto multiplyMatrices = [](const AffineMatrix& lhs, const AffineMatrix& rhs) {
        const std::size_t dimension = lhs.size();
        AffineMatrix product(dimension, AffineRow(dimension, 0));
        for (std::size_t row = 0; row < dimension; ++row) {
          for (std::size_t pivot = 0; pivot < dimension; ++pivot) {
            if (lhs[row][pivot] == 0) {
              continue;
            }
            for (std::size_t column = 0; column < dimension; ++column) {
              product[row][column] += static_cast<std::uint32_t>(
                  static_cast<std::uint64_t>(lhs[row][pivot]) * rhs[pivot][column]);
            }
          }
        }
        return product;
      };
      const auto rowIsConstant = [](const AffineRow& row,
                                    std::size_t constantColumn) -> std::optional<std::uint32_t> {
        for (std::size_t column = 0; column < constantColumn; ++column) {
          if (row[column] != 0) {
            return std::nullopt;
          }
        }
        return row[constantColumn];
      };

      for (int summaryRound = 0; summaryRound < 8; ++summaryRound) {
        std::unordered_map<std::string, int> labelReferences;
        for (const auto& inst : ir) {
          if ((inst.op == IROp::BRANCH || inst.op == IROp::BEQZ || inst.op == IROp::BNEZ) &&
              inst.dest.isLabel()) {
            ++labelReferences[inst.dest.name];
          }
        }

        bool summarized = false;
        // 从后向前扫描，先把最内层循环改成直线变换；下一轮即可继续处理外层。
        for (std::size_t loopStart = ir.size(); loopStart-- > 0 && !summarized;) {
          if (ir[loopStart].op != IROp::BRANCH || !ir[loopStart].dest.isLabel() ||
              loopStart + 1 >= ir.size() || ir[loopStart + 1].op != IROp::LABEL ||
              !ir[loopStart + 1].dest.isLabel()) {
            continue;
          }
          const std::string condLabel = ir[loopStart].dest.name;
          const std::string bodyLabel = ir[loopStart + 1].dest.name;
          if (labelReferences[condLabel] != 1 || labelReferences[bodyLabel] != 1) {
            continue;
          }

          std::size_t condIndex = loopStart + 2;
          while (condIndex < ir.size() && ir[condIndex].op != IROp::FUNC_END &&
                 !(ir[condIndex].op == IROp::LABEL && ir[condIndex].dest.isLabel() &&
                   ir[condIndex].dest.name == condLabel)) {
            ++condIndex;
          }
          if (condIndex + 2 >= ir.size() || ir[condIndex].op == IROp::FUNC_END) {
            continue;
          }
          const IRInst& condition = ir[condIndex + 1];
          const IRInst& backedge = ir[condIndex + 2];
          if ((condition.op != IROp::LT && condition.op != IROp::LE && condition.op != IROp::GT &&
               condition.op != IROp::GE) ||
              !condition.dest.isLocalVar() || backedge.op != IROp::BNEZ ||
              !backedge.dest.isLabel() || backedge.dest.name != bodyLabel ||
              !backedge.src1.isLocalVar() || backedge.src1.name != condition.dest.name) {
            continue;
          }

          const auto isScalarVariable = [](const Operand& operand) {
            return operand.isLocalVar() || operand.isGlobalVar();
          };
          const auto sameVariable = [&](const Operand& lhs, const Operand& rhs) {
            return lhs.type == rhs.type && isScalarVariable(lhs) && lhs.name == rhs.name;
          };
          const auto writtenInBody = [&](const Operand& operand) {
            if (!isScalarVariable(operand)) {
              return false;
            }
            for (std::size_t position = loopStart + 2; position < condIndex; ++position) {
              const IRInst& candidate = ir[position];
              if (sameVariable(candidate.dest, operand) && candidate.op != IROp::RETURN &&
                  candidate.op != IROp::PARAM) {
                return true;
              }
            }
            return false;
          };
          IROp relation = condition.op;
          Operand inductionOperand = condition.src1;
          Operand boundOperand = condition.src2;
          if (!writtenInBody(inductionOperand) && writtenInBody(boundOperand)) {
            std::swap(inductionOperand, boundOperand);
            switch (relation) {
            case IROp::LT:
              relation = IROp::GT;
              break;
            case IROp::LE:
              relation = IROp::GE;
              break;
            case IROp::GT:
              relation = IROp::LT;
              break;
            case IROp::GE:
              relation = IROp::LE;
              break;
            default:
              break;
            }
          }
          if (!isScalarVariable(inductionOperand) || !writtenInBody(inductionOperand)) {
            continue;
          }

          const std::string induction = inductionOperand.name;
          const auto findNearbyConstant = [&](const Operand& operand) -> std::optional<int> {
            for (std::size_t position = loopStart; position > 0; --position) {
              const IRInst& candidate = ir[position - 1];
              if (sameVariable(candidate.dest, operand)) {
                if ((candidate.op == IROp::ASSIGN || candidate.op == IROp::LOCAL_VAR_DECL ||
                     candidate.op == IROp::GLOBAL_VAR_DECL) &&
                    candidate.src1.isImm()) {
                  return candidate.src1.immVal;
                }
                return std::nullopt;
              }
              if (candidate.op == IROp::LABEL || candidate.op == IROp::BRANCH ||
                  candidate.op == IROp::BEQZ || candidate.op == IROp::BNEZ ||
                  candidate.op == IROp::CALL || candidate.op == IROp::FUNC_BEGIN) {
                break;
              }
            }
            return std::nullopt;
          };
          const auto findUniqueConstant = [&](const Operand& operand) -> std::optional<int> {
            std::size_t functionBegin = loopStart;
            while (functionBegin > 0 && ir[functionBegin - 1].op != IROp::FUNC_BEGIN) {
              --functionBegin;
            }

            // 全局状态只有在 main 的循环入口可证明时才使用声明初值。此前任何
            // 调用都可能改写全局；若同一全局在此前控制流中出现过写入，则也不
            // 能把线性 IR 中看到的常量当作必经值。紧邻循环的直线常量赋值已由
            // findNearbyConstant 处理；这里仅安全回退到从未写过的声明初值。
            if (operand.isGlobalVar()) {
              if (functionBegin == 0 || ir[functionBegin - 1].op != IROp::FUNC_BEGIN ||
                  !ir[functionBegin - 1].dest.isFunc() ||
                  ir[functionBegin - 1].dest.name != "main") {
                return std::nullopt;
              }
              for (std::size_t position = functionBegin; position < loopStart; ++position) {
                const IRInst& candidate = ir[position];
                if (candidate.op == IROp::CALL) {
                  return std::nullopt;
                }
                if (sameVariable(candidate.dest, operand)) {
                  return std::nullopt;
                }
              }
              std::optional<int> constant;
              for (std::size_t position = 0; position + 1 < functionBegin; ++position) {
                const IRInst& candidate = ir[position];
                if (candidate.op == IROp::GLOBAL_VAR_DECL &&
                    sameVariable(candidate.dest, operand) && candidate.src1.isImm()) {
                  if (constant) {
                    return std::nullopt;
                  }
                  constant = candidate.src1.immVal;
                }
              }
              return constant;
            }

            int definitions = 0;
            std::optional<int> constant;
            for (std::size_t position = functionBegin; position < ir.size(); ++position) {
              const IRInst& candidate = ir[position];
              if (candidate.op == IROp::FUNC_END) {
                break;
              }
              if (!sameVariable(candidate.dest, operand) || candidate.op == IROp::RETURN ||
                  candidate.op == IROp::PARAM ||
                  (candidate.op == IROp::LOCAL_VAR_DECL && candidate.src1.isNone())) {
                continue;
              }
              ++definitions;
              if ((candidate.op == IROp::ASSIGN || candidate.op == IROp::LOCAL_VAR_DECL) &&
                  candidate.src1.isImm()) {
                constant = candidate.src1.immVal;
              } else {
                constant.reset();
              }
            }
            return definitions == 1 ? constant : std::nullopt;
          };

          auto initial = findNearbyConstant(inductionOperand);
          if (!initial && inductionOperand.isGlobalVar()) {
            initial = findUniqueConstant(inductionOperand);
          }
          std::optional<int> upper;
          if (boundOperand.isImm()) {
            upper = boundOperand.immVal;
          } else if (boundOperand.isLocalVar()) {
            upper = findNearbyConstant(boundOperand);
            if (!upper) {
              upper = findUniqueConstant(boundOperand);
            }
          } else if (boundOperand.isGlobalVar()) {
            upper = findNearbyConstant(boundOperand);
            if (!upper) {
              upper = findUniqueConstant(boundOperand);
            }
          }
          if (!initial || !upper) {
            continue;
          }

          std::vector<std::string> variables;
          std::vector<Operand> variableOperands;
          std::unordered_map<std::string, std::size_t> variableIndex;
          const auto addVariable = [&](const Operand& operand) {
            if (!isScalarVariable(operand) || variableIndex.count(operand.name) != 0) {
              return;
            }
            variableIndex[operand.name] = variables.size();
            variables.push_back(operand.name);
            variableOperands.push_back(operand);
          };
          std::unordered_set<std::string> written;
          bool bodySupported = condIndex > loopStart + 2;
          for (std::size_t position = loopStart + 2; position < condIndex && bodySupported;
               ++position) {
            const IRInst& inst = ir[position];
            if (inst.op == IROp::LOCAL_VAR_DECL && inst.src1.isNone()) {
              continue;
            }
            if (inst.op != IROp::LOCAL_VAR_DECL && inst.op != IROp::ASSIGN &&
                inst.op != IROp::ADD && inst.op != IROp::SUB && inst.op != IROp::MUL &&
                inst.op != IROp::DIV && inst.op != IROp::MOD) {
              bodySupported = false;
              break;
            }
            if (!isScalarVariable(inst.dest) ||
                (!inst.src1.isImm() && !isScalarVariable(inst.src1)) ||
                (!inst.src2.isNone() && !inst.src2.isImm() && !isScalarVariable(inst.src2))) {
              bodySupported = false;
              break;
            }
            addVariable(inst.dest);
            addVariable(inst.src1);
            addVariable(inst.src2);
            written.insert(inst.dest.name);
          }
          addVariable(inductionOperand);
          if (!bodySupported || variables.empty() ||
              variables.size() > kMaxCoupledAffineVariables || written.count(induction) == 0 ||
              (isScalarVariable(boundOperand) && written.count(boundOperand.name) != 0)) {
            continue;
          }

          const std::size_t constantColumn = variables.size();
          const std::size_t dimension = variables.size() + 1;
          AffineMatrix transform = identityMatrix(dimension);
          std::vector<std::optional<std::uint32_t>> normalizedModulus(variables.size());
          // 当前迭代内的非负值上界。模运算会把结果收紧到 [0, modulus)，
          // 后续正数乘加据此证明不会发生有符号溢出；这比反复把表达式展开到
          // 循环入口状态更精确，长模仿射链也不会因膨胀系数而误回退。
          std::vector<std::optional<std::uint64_t>> nonnegativeUpper(variables.size());
          std::optional<std::uint32_t> loopModulus;
          std::unordered_set<std::size_t> modularInputColumns;
          const auto rowForOperand = [&](const Operand& operand) -> std::optional<AffineRow> {
            AffineRow row(dimension, 0);
            if (operand.isImm()) {
              row[constantColumn] = static_cast<std::uint32_t>(operand.immVal);
              return row;
            }
            if (!isScalarVariable(operand)) {
              return std::nullopt;
            }
            const auto found = variableIndex.find(operand.name);
            if (found == variableIndex.end()) {
              return std::nullopt;
            }
            return transform[found->second];
          };
          const auto upperForOperand = [&](const Operand& operand) -> std::optional<std::uint64_t> {
            if (operand.isImm()) {
              if (operand.immVal < 0) {
                return std::nullopt;
              }
              return static_cast<std::uint64_t>(operand.immVal);
            }
            if (!isScalarVariable(operand)) {
              return std::nullopt;
            }
            const auto found = variableIndex.find(operand.name);
            return found == variableIndex.end() ? std::nullopt : nonnegativeUpper[found->second];
          };
          const auto sameOperand = [](const Operand& lhs, const Operand& rhs) {
            if (lhs.type != rhs.type) {
              return false;
            }
            return lhs.isImm() ? lhs.immVal == rhs.immVal : lhs.name == rhs.name;
          };
          const auto applyPositiveModulus = [&](std::size_t destination, const AffineRow& dividend,
                                                std::uint32_t modulus,
                                                std::optional<std::uint64_t> currentUpper) {
            if (loopModulus && *loopModulus != modulus) {
              return false;
            }

            // C 的有符号余数只有在被除数非负时等同数学模运算。这里按每个
            // 入口状态位于 [0, modulus) 估算第一次被除数；链中后续被除数优先
            // 使用上一条模运算传播出的当前值上界，避免展开系数虚高。
            for (std::size_t column = 0; column < constantColumn; ++column) {
              const std::uint32_t coefficient = dividend[column];
              if (coefficient != 0) {
                modularInputColumns.insert(column);
              }
            }
            bool nonnegativeAndBounded = currentUpper && *currentUpper <= INT32_MAX;
            if (!currentUpper) {
              std::uint64_t expandedUpper = dividend[constantColumn];
              nonnegativeAndBounded = expandedUpper <= INT32_MAX;
              for (std::size_t column = 0; column < constantColumn && nonnegativeAndBounded;
                   ++column) {
                const std::uint32_t coefficient = dividend[column];
                if (coefficient == 0) {
                  continue;
                }
                const std::uint64_t term = static_cast<std::uint64_t>(coefficient) *
                                           static_cast<std::uint64_t>(modulus - 1);
                nonnegativeAndBounded = term <= INT32_MAX && expandedUpper <= INT32_MAX - term;
                if (nonnegativeAndBounded) {
                  expandedUpper += term;
                }
              }
            }
            if (!nonnegativeAndBounded) {
              return false;
            }

            AffineRow result = dividend;
            for (std::uint32_t& coefficient : result) {
              coefficient %= modulus;
            }
            transform[destination] = std::move(result);
            normalizedModulus[destination] = modulus;
            nonnegativeUpper[destination] = static_cast<std::uint64_t>(modulus - 1);
            loopModulus = modulus;
            return true;
          };

          for (std::size_t position = loopStart + 2; position < condIndex && bodySupported;
               ++position) {
            const IRInst& inst = ir[position];
            if (inst.op == IROp::LOCAL_VAR_DECL && inst.src1.isNone()) {
              continue;
            }

            // Pass 0d 已把 `x % c` 规范成 `q=x/c; m=q*c; d=x-m`。在这里把
            // 这个三指令序列重新识别成模仿射行，既保留除法/CSE 的通用优化，
            // 又让常数次数的模矩阵递推可以被整体摘要。
            if (inst.op == IROp::DIV && inst.dest.isLocalVar() && inst.src2.isImm() &&
                inst.src2.immVal > 1 && position + 2 < condIndex) {
              const IRInst& product = ir[position + 1];
              const IRInst& remainder = ir[position + 2];
              const bool productUsesQuotient =
                  product.op == IROp::MUL && product.dest.isLocalVar() &&
                  ((sameOperand(product.src1, inst.dest) && sameOperand(product.src2, inst.src2)) ||
                   (sameOperand(product.src2, inst.dest) && sameOperand(product.src1, inst.src2)));
              if (productUsesQuotient && remainder.op == IROp::SUB && remainder.dest.isLocalVar() &&
                  sameOperand(remainder.src1, inst.src1) &&
                  sameOperand(remainder.src2, product.dest)) {
                const auto destination = variableIndex.find(remainder.dest.name);
                const auto dividend = rowForOperand(inst.src1);
                if (destination == variableIndex.end() || !dividend ||
                    !applyPositiveModulus(destination->second, *dividend,
                                          static_cast<std::uint32_t>(inst.src2.immVal),
                                          upperForOperand(inst.src1))) {
                  bodySupported = false;
                  break;
                }
                position += 2;
                continue;
              }
            }
            if (inst.op == IROp::DIV) {
              bodySupported = false;
              break;
            }

            const auto destination = variableIndex.find(inst.dest.name);
            const auto lhs = rowForOperand(inst.src1);
            if (destination == variableIndex.end() || !lhs) {
              bodySupported = false;
              break;
            }
            if (inst.op == IROp::LOCAL_VAR_DECL || inst.op == IROp::ASSIGN) {
              transform[destination->second] = *lhs;
              nonnegativeUpper[destination->second] = upperForOperand(inst.src1);
              if (inst.src1.isLocalVar()) {
                normalizedModulus[destination->second] =
                    normalizedModulus[variableIndex.at(inst.src1.name)];
              } else {
                normalizedModulus[destination->second].reset();
              }
              continue;
            }

            if (inst.op == IROp::MOD) {
              if (!inst.src2.isImm() || inst.src2.immVal <= 1) {
                bodySupported = false;
                break;
              }
              if (!applyPositiveModulus(destination->second, *lhs,
                                        static_cast<std::uint32_t>(inst.src2.immVal),
                                        upperForOperand(inst.src1))) {
                bodySupported = false;
                break;
              }
              continue;
            }

            const auto rhs = rowForOperand(inst.src2);
            if (!rhs) {
              bodySupported = false;
              break;
            }
            const std::optional<std::uint64_t> lhsUpper = upperForOperand(inst.src1);
            const std::optional<std::uint64_t> rhsUpper = upperForOperand(inst.src2);
            AffineRow result(dimension, 0);
            if (inst.op == IROp::ADD || inst.op == IROp::SUB) {
              for (std::size_t column = 0; column < dimension; ++column) {
                result[column] = inst.op == IROp::ADD ? (*lhs)[column] + (*rhs)[column]
                                                      : (*lhs)[column] - (*rhs)[column];
              }
            } else {
              const auto lhsConstant = rowIsConstant(*lhs, constantColumn);
              const auto rhsConstant = rowIsConstant(*rhs, constantColumn);
              if (!lhsConstant && !rhsConstant) {
                bodySupported = false;
                break;
              }
              const std::uint32_t factor = lhsConstant ? *lhsConstant : *rhsConstant;
              const AffineRow& value = lhsConstant ? *rhs : *lhs;
              for (std::size_t column = 0; column < dimension; ++column) {
                result[column] =
                    static_cast<std::uint32_t>(static_cast<std::uint64_t>(value[column]) * factor);
              }
            }
            transform[destination->second] = std::move(result);
            normalizedModulus[destination->second].reset();
            std::optional<std::uint64_t> resultUpper;
            if (inst.op == IROp::ADD && lhsUpper && rhsUpper && *rhsUpper <= INT32_MAX &&
                *lhsUpper <= static_cast<std::uint64_t>(INT32_MAX) - *rhsUpper) {
              resultUpper = *lhsUpper + *rhsUpper;
            } else if (inst.op == IROp::SUB && lhsUpper && rhsUpper && *rhsUpper == 0) {
              resultUpper = lhsUpper;
            } else if (inst.op == IROp::MUL && lhsUpper && rhsUpper &&
                       (*lhsUpper == 0 ||
                        *rhsUpper <= static_cast<std::uint64_t>(INT32_MAX) / *lhsUpper)) {
              resultUpper = *lhsUpper * *rhsUpper;
            }
            nonnegativeUpper[destination->second] = resultUpper;
          }
          if (!bodySupported) {
            continue;
          }

          const std::size_t inductionIndex = variableIndex.at(induction);
          const AffineRow& inductionRow = transform[inductionIndex];
          const std::int64_t inductionStep =
              static_cast<std::int32_t>(inductionRow[constantColumn]);
          bool canonicalInduction = inductionStep != 0;
          for (std::size_t column = 0; column < constantColumn && canonicalInduction; ++column) {
            canonicalInduction = inductionRow[column] == (column == inductionIndex ? 1u : 0u);
          }
          if (!canonicalInduction) {
            continue;
          }

          // 常量步长的单调循环。按源比较关系精确计算迭代次数，并验证最后一次
          // 归纳更新仍在 int32 范围内；越过边界会溢出的循环必须保留原形。
          const bool increasingRelation = relation == IROp::LT || relation == IROp::LE;
          const bool decreasingRelation = relation == IROp::GT || relation == IROp::GE;
          if ((increasingRelation && inductionStep <= 0) ||
              (decreasingRelation && inductionStep >= 0)) {
            continue;
          }
          const std::int64_t startValue = *initial;
          const std::int64_t boundValue = *upper;
          const bool loopRuns = relation == IROp::LT   ? startValue < boundValue
                                : relation == IROp::LE ? startValue <= boundValue
                                : relation == IROp::GT ? startValue > boundValue
                                                       : startValue >= boundValue;
          std::uint64_t trips = 0;
          if (loopRuns) {
            if (increasingRelation) {
              const std::uint64_t distance = static_cast<std::uint64_t>(boundValue - startValue);
              const std::uint64_t step = static_cast<std::uint64_t>(inductionStep);
              trips = relation == IROp::LT ? (distance + step - 1) / step : distance / step + 1;
            } else {
              const std::uint64_t distance = static_cast<std::uint64_t>(startValue - boundValue);
              const std::uint64_t step = static_cast<std::uint64_t>(-inductionStep);
              trips = relation == IROp::GT ? (distance + step - 1) / step : distance / step + 1;
            }
          }
          const std::int64_t finalInduction =
              startValue + static_cast<std::int64_t>(trips) * inductionStep;
          if (trips < 2 || finalInduction < INT32_MIN || finalInduction > INT32_MAX) {
            continue;
          }

          std::size_t loopEnd = condIndex + 3;
          const auto usedAfterLoop = [&](const std::string& name) {
            const auto variable = variableIndex.find(name);
            if (variable != variableIndex.end() &&
                variableOperands[variable->second].isGlobalVar()) {
              return true;
            }
            for (std::size_t position = loopEnd; position < ir.size(); ++position) {
              const IRInst& inst = ir[position];
              if (inst.op == IROp::FUNC_BEGIN || inst.op == IROp::FUNC_END) {
                break;
              }
              if ((inst.src1.isLocalVar() && inst.src1.name == name) ||
                  (inst.src2.isLocalVar() && inst.src2.name == name) ||
                  ((inst.op == IROp::RETURN || inst.op == IROp::PARAM) && inst.dest.isLocalVar() &&
                   inst.dest.name == name)) {
                return true;
              }
            }
            return false;
          };

          // 内层摘要常把自己的归纳状态改写为常量，例如外层每轮执行 `j = 0`
          // 后，已闭式化的内层最终写回 `j = 5`。只有当该 reset 状态的入口旧值
          // 对所有循环后可观察行的矩阵系数都为 0 时，才跨层继续摘要；这证明旧
          // 状态已在本轮被完全杀死。若旧值仍流入任一结果，则保留外层回边。
          bool resetStateIsKilled = true;
          for (const std::string& name : written) {
            if (name == induction || isCompilerTemp(name)) {
              continue;
            }
            const std::size_t resetIndex = variableIndex.at(name);
            if (!rowIsConstant(transform[resetIndex], constantColumn)) {
              continue;
            }
            for (std::size_t rowIndex = 0; rowIndex < variables.size(); ++rowIndex) {
              if (written.count(variables[rowIndex]) != 0 && usedAfterLoop(variables[rowIndex]) &&
                  transform[rowIndex][resetIndex] != 0) {
                resetStateIsKilled = false;
                break;
              }
            }
            if (!resetStateIsKilled) {
              break;
            }
          }
          if (!resetStateIsKilled) {
            continue;
          }

          bool hasCoupledObservableState = false;
          for (const std::string& name : written) {
            if (name == induction || !usedAfterLoop(name)) {
              continue;
            }
            const std::size_t rowIndex = variableIndex.at(name);
            if (transform[rowIndex][rowIndex] != 1) {
              hasCoupledObservableState = true;
              break;
            }
            for (std::size_t column = 0; column < variables.size(); ++column) {
              if (column != rowIndex && column != inductionIndex &&
                  transform[rowIndex][column] != 0 && written.count(variables[column]) != 0) {
                hasCoupledObservableState = true;
                break;
              }
            }
            if (hasCoupledObservableState) {
              break;
            }
          }
          if (!hasCoupledObservableState) {
            continue; // Pass 5.5/6 对独立累加器会生成更短的闭式。
          }

          if (loopModulus) {
            const std::uint32_t modulus = *loopModulus;
            bool modularSummarySafe = true;
            std::vector<std::optional<std::uint32_t>> initialValues(variables.size());
            for (const std::size_t column : modularInputColumns) {
              std::optional<int> value = findNearbyConstant(variableOperands[column]);
              if (!value) {
                value = findUniqueConstant(variableOperands[column]);
              }
              if (!value || *value < 0 || static_cast<std::uint32_t>(*value) >= modulus ||
                  normalizedModulus[column] != loopModulus) {
                modularSummarySafe = false;
                break;
              }
              initialValues[column] = static_cast<std::uint32_t>(*value);
            }
            for (std::size_t rowIndex = 0; rowIndex < variables.size() && modularSummarySafe;
                 ++rowIndex) {
              const std::string& name = variables[rowIndex];
              if (name == induction || !written.count(name) || !usedAfterLoop(name)) {
                continue;
              }
              if (normalizedModulus[rowIndex] != loopModulus ||
                  transform[rowIndex][inductionIndex] != 0) {
                modularSummarySafe = false;
              }
            }
            if (!modularSummarySafe) {
              continue;
            }

            const auto multiplyMatricesModulo = [modulus](const AffineMatrix& lhs,
                                                          const AffineMatrix& rhs) {
              const std::size_t matrixDimension = lhs.size();
              AffineMatrix product(matrixDimension, AffineRow(matrixDimension, 0));
              for (std::size_t row = 0; row < matrixDimension; ++row) {
                for (std::size_t pivot = 0; pivot < matrixDimension; ++pivot) {
                  if (lhs[row][pivot] == 0) {
                    continue;
                  }
                  for (std::size_t column = 0; column < matrixDimension; ++column) {
                    const std::uint64_t term =
                        static_cast<std::uint64_t>(lhs[row][pivot]) * rhs[pivot][column];
                    product[row][column] = static_cast<std::uint32_t>(
                        (static_cast<std::uint64_t>(product[row][column]) + term) % modulus);
                  }
                }
              }
              return product;
            };

            AffineMatrix modularTransform = transform;
            for (AffineRow& row : modularTransform) {
              for (std::uint32_t& coefficient : row) {
                coefficient %= modulus;
              }
            }
            AffineMatrix modularPower = modularTransform;
            AffineMatrix modularSummary = identityMatrix(dimension);
            for (std::uint64_t remaining = static_cast<std::uint64_t>(trips); remaining != 0;
                 remaining >>= 1) {
              if ((remaining & 1u) != 0) {
                modularSummary = multiplyMatricesModulo(modularPower, modularSummary);
              }
              if (remaining > 1) {
                modularPower = multiplyMatricesModulo(modularPower, modularPower);
              }
            }

            std::vector<IRInst> replacement;
            for (std::size_t rowIndex = 0; rowIndex < variables.size(); ++rowIndex) {
              const std::string& name = variables[rowIndex];
              if (name == induction || !written.count(name) || !usedAfterLoop(name)) {
                continue;
              }
              std::uint64_t finalValue = modularSummary[rowIndex][constantColumn];
              for (std::size_t column = 0; column < variables.size(); ++column) {
                const std::uint32_t coefficient = modularSummary[rowIndex][column];
                if (coefficient == 0) {
                  continue;
                }
                if (!initialValues[column]) {
                  modularSummarySafe = false;
                  break;
                }
                finalValue = (finalValue +
                              static_cast<std::uint64_t>(coefficient) * *initialValues[column]) %
                             modulus;
              }
              if (!modularSummarySafe) {
                break;
              }
              replacement.emplace_back(IROp::ASSIGN, variableOperands[rowIndex],
                                       Operand::imm(static_cast<std::int32_t>(finalValue)),
                                       Operand::none());
            }
            if (!modularSummarySafe) {
              continue;
            }
            if (usedAfterLoop(induction)) {
              replacement.emplace_back(IROp::ASSIGN, inductionOperand,
                                       Operand::imm(static_cast<int>(finalInduction)),
                                       Operand::none());
            }
            if (replacement.empty()) {
              continue;
            }

            if (loopEnd < ir.size() && ir[loopEnd].op == IROp::LABEL &&
                ir[loopEnd].dest.isLabel() && labelReferences[ir[loopEnd].dest.name] == 0) {
              ++loopEnd;
            }
            ir.erase(ir.begin() + static_cast<std::ptrdiff_t>(loopStart),
                     ir.begin() + static_cast<std::ptrdiff_t>(loopEnd));
            ir.insert(ir.begin() + static_cast<std::ptrdiff_t>(loopStart), replacement.begin(),
                      replacement.end());
            summarized = true;
            changed = true;
            break;
          }

          AffineMatrix power = transform;
          AffineMatrix summary = identityMatrix(dimension);
          for (std::uint64_t remaining = static_cast<std::uint64_t>(trips); remaining != 0;
               remaining >>= 1) {
            if ((remaining & 1u) != 0) {
              summary = multiplyMatrices(power, summary);
            }
            if (remaining > 1) {
              power = multiplyMatrices(power, power);
            }
          }

          std::vector<IRInst> replacement;
          std::vector<std::pair<Operand, std::string>> finalMoves;
          bool writeFinalInduction = false;
          for (std::size_t rowIndex = 0; rowIndex < variables.size(); ++rowIndex) {
            const std::string& name = variables[rowIndex];
            if (!written.count(name) || !usedAfterLoop(name)) {
              continue;
            }
            bool changedRow = false;
            for (std::size_t column = 0; column < dimension; ++column) {
              const std::uint32_t expected = column == rowIndex ? 1u : 0u;
              if (summary[rowIndex][column] != expected) {
                changedRow = true;
                break;
              }
            }
            if (!changedRow) {
              continue;
            }
            if (name == induction) {
              // 和其它状态一样延迟到所有闭式表达式算完后再回写。其它行可能
              // 仍读取归纳变量入口值；若在这里提前覆盖，会把并行矩阵赋值错误
              // 地变成顺序赋值。
              writeFinalInduction = true;
              continue;
            }

            const std::string finalName = newTemp();
            bool haveValue = false;
            for (std::size_t column = 0; column < variables.size(); ++column) {
              const std::uint32_t coefficient = summary[rowIndex][column];
              if (coefficient == 0) {
                continue;
              }
              Operand term = variableOperands[column];
              if (coefficient != 1) {
                const std::string productName = newTemp();
                replacement.emplace_back(IROp::MUL, Operand::localVar(productName), term,
                                         Operand::imm(static_cast<std::int32_t>(coefficient)));
                term = Operand::localVar(productName);
              }
              if (!haveValue) {
                replacement.emplace_back(IROp::ASSIGN, Operand::localVar(finalName), term,
                                         Operand::none());
                haveValue = true;
              } else {
                replacement.emplace_back(IROp::ADD, Operand::localVar(finalName),
                                         Operand::localVar(finalName), term);
              }
            }
            const std::uint32_t constant = summary[rowIndex][constantColumn];
            if (!haveValue) {
              replacement.emplace_back(IROp::ASSIGN, Operand::localVar(finalName),
                                       Operand::imm(static_cast<std::int32_t>(constant)),
                                       Operand::none());
            } else if (constant != 0) {
              replacement.emplace_back(IROp::ADD, Operand::localVar(finalName),
                                       Operand::localVar(finalName),
                                       Operand::imm(static_cast<std::int32_t>(constant)));
            }
            finalMoves.push_back({variableOperands[rowIndex], finalName});
          }
          for (const auto& move : finalMoves) {
            replacement.emplace_back(IROp::ASSIGN, move.first, Operand::localVar(move.second),
                                     Operand::none());
          }
          if (writeFinalInduction) {
            replacement.emplace_back(IROp::ASSIGN, inductionOperand,
                                     Operand::imm(static_cast<int>(finalInduction)),
                                     Operand::none());
          }
          if (replacement.empty()) {
            continue;
          }

          if (loopEnd < ir.size() && ir[loopEnd].op == IROp::LABEL && ir[loopEnd].dest.isLabel() &&
              labelReferences[ir[loopEnd].dest.name] == 0) {
            ++loopEnd;
          }
          ir.erase(ir.begin() + static_cast<std::ptrdiff_t>(loopStart),
                   ir.begin() + static_cast<std::ptrdiff_t>(loopEnd));
          ir.insert(ir.begin() + static_cast<std::ptrdiff_t>(loopStart), replacement.begin(),
                    replacement.end());
          summarized = true;
        }
        if (!summarized) {
          break;
        }
      }
    }

    // Pass 5.7: 汇总由归纳变量余数控制的仿射分支循环。
    //
    // Pass 5.6 只接受直线循环体，但 `if (i % k == r)` 的选择以 k 为周期重复。
    // 当两个分支及公共后缀都是局部仿射变换时，先合成一个完整周期的矩阵，再
    // 对周期矩阵做快速幂。该变换只依赖符号证明，不迭代执行源循环；条件不是
    // 已证明的余数周期、存在外部跳转/调用/全局状态或任一分支非仿射时均回退。
    {
      using AffineRow = std::vector<std::uint32_t>;
      using AffineMatrix = std::vector<AffineRow>;
      constexpr std::uint64_t kMaxPeriodicBranchPhases = 256;

      const auto identityMatrix = [](std::size_t dimension) {
        AffineMatrix identity(dimension, AffineRow(dimension, 0));
        for (std::size_t index = 0; index < dimension; ++index) {
          identity[index][index] = 1;
        }
        return identity;
      };
      const auto multiplyMatrices = [](const AffineMatrix& lhs, const AffineMatrix& rhs) {
        const std::size_t dimension = lhs.size();
        AffineMatrix product(dimension, AffineRow(dimension, 0));
        for (std::size_t row = 0; row < dimension; ++row) {
          for (std::size_t pivot = 0; pivot < dimension; ++pivot) {
            if (lhs[row][pivot] == 0) {
              continue;
            }
            for (std::size_t column = 0; column < dimension; ++column) {
              product[row][column] += static_cast<std::uint32_t>(
                  static_cast<std::uint64_t>(lhs[row][pivot]) * rhs[pivot][column]);
            }
          }
        }
        return product;
      };
      const auto rowIsConstant = [](const AffineRow& row,
                                    std::size_t constantColumn) -> std::optional<std::uint32_t> {
        for (std::size_t column = 0; column < constantColumn; ++column) {
          if (row[column] != 0) {
            return std::nullopt;
          }
        }
        return row[constantColumn];
      };
      const auto gcd = [](std::uint64_t lhs, std::uint64_t rhs) {
        while (rhs != 0) {
          const std::uint64_t remainder = lhs % rhs;
          lhs = rhs;
          rhs = remainder;
        }
        return lhs;
      };

      struct PeriodicValue {
        enum class Kind { UNKNOWN, CONSTANT, INDUCTION, QUOTIENT, QUOTIENT_PRODUCT, PERIODIC };
        Kind kind = Kind::UNKNOWN;
        int value = 0;
        int divisor = 0;
        std::uint64_t period = 0;
      };

      for (int summaryRound = 0; summaryRound < 8; ++summaryRound) {
        std::unordered_map<std::string, int> labelReferences;
        std::unordered_map<std::string, std::size_t> labelPositions;
        for (std::size_t index = 0; index < ir.size(); ++index) {
          const auto& inst = ir[index];
          if (inst.op == IROp::LABEL && inst.dest.isLabel()) {
            labelPositions[inst.dest.name] = index;
          }
          if ((inst.op == IROp::BRANCH || inst.op == IROp::BEQZ || inst.op == IROp::BNEZ) &&
              inst.dest.isLabel()) {
            ++labelReferences[inst.dest.name];
          }
        }

        bool summarized = false;
        for (std::size_t loopStart = ir.size(); loopStart-- > 0 && !summarized;) {
          if (ir[loopStart].op != IROp::BRANCH || !ir[loopStart].dest.isLabel() ||
              loopStart + 1 >= ir.size() || ir[loopStart + 1].op != IROp::LABEL ||
              !ir[loopStart + 1].dest.isLabel()) {
            continue;
          }
          const std::string condLabel = ir[loopStart].dest.name;
          const std::string bodyLabel = ir[loopStart + 1].dest.name;
          if (labelReferences[condLabel] != 1 || labelReferences[bodyLabel] != 1) {
            continue;
          }
          const auto condPosition = labelPositions.find(condLabel);
          if (condPosition == labelPositions.end() || condPosition->second <= loopStart + 2) {
            continue;
          }
          const std::size_t condIndex = condPosition->second;
          if (condIndex + 2 >= ir.size()) {
            continue;
          }
          const IRInst& condition = ir[condIndex + 1];
          const IRInst& backedge = ir[condIndex + 2];
          if ((condition.op != IROp::LT && condition.op != IROp::LE) ||
              !condition.dest.isLocalVar() || !condition.src1.isLocalVar() ||
              backedge.op != IROp::BNEZ || !backedge.dest.isLabel() ||
              backedge.dest.name != bodyLabel || !backedge.src1.isLocalVar() ||
              backedge.src1.name != condition.dest.name) {
            continue;
          }
          const std::string induction = condition.src1.name;

          const auto findNearbyConstant = [&](const std::string& name) -> std::optional<int> {
            for (std::size_t position = loopStart; position > 0; --position) {
              const IRInst& candidate = ir[position - 1];
              if (candidate.dest.isLocalVar() && candidate.dest.name == name) {
                if ((candidate.op == IROp::ASSIGN || candidate.op == IROp::LOCAL_VAR_DECL) &&
                    candidate.src1.isImm()) {
                  return candidate.src1.immVal;
                }
                return std::nullopt;
              }
              if (candidate.op == IROp::LABEL || candidate.op == IROp::BRANCH ||
                  candidate.op == IROp::BEQZ || candidate.op == IROp::BNEZ ||
                  candidate.op == IROp::CALL || candidate.op == IROp::FUNC_BEGIN) {
                break;
              }
            }
            return std::nullopt;
          };
          const auto findUniqueConstant = [&](const std::string& name) -> std::optional<int> {
            std::size_t functionBegin = loopStart;
            while (functionBegin > 0 && ir[functionBegin - 1].op != IROp::FUNC_BEGIN) {
              --functionBegin;
            }
            int definitions = 0;
            std::optional<int> value;
            for (std::size_t position = functionBegin; position < ir.size(); ++position) {
              const IRInst& candidate = ir[position];
              if (candidate.op == IROp::FUNC_END) {
                break;
              }
              if (!candidate.dest.isLocalVar() || candidate.dest.name != name ||
                  candidate.op == IROp::RETURN || candidate.op == IROp::PARAM ||
                  (candidate.op == IROp::LOCAL_VAR_DECL && candidate.src1.isNone())) {
                continue;
              }
              ++definitions;
              if ((candidate.op == IROp::ASSIGN || candidate.op == IROp::LOCAL_VAR_DECL) &&
                  candidate.src1.isImm()) {
                value = candidate.src1.immVal;
              } else {
                value.reset();
              }
            }
            return definitions == 1 ? value : std::nullopt;
          };

          const auto initial = findNearbyConstant(induction);
          std::optional<int> upper;
          if (condition.src2.isImm()) {
            upper = condition.src2.immVal;
          } else if (condition.src2.isLocalVar()) {
            upper = findNearbyConstant(condition.src2.name);
            if (!upper) {
              upper = findUniqueConstant(condition.src2.name);
            }
          }
          if (!initial || !upper || *initial < 0 ||
              (condition.op == IROp::LE && *upper == INT32_MAX && *initial <= *upper)) {
            continue;
          }
          std::int64_t trips = static_cast<std::int64_t>(*upper) - *initial;
          if (condition.op == IROp::LE) {
            ++trips;
          }
          trips = std::max<std::int64_t>(0, trips);
          const std::int64_t finalInduction = static_cast<std::int64_t>(*initial) + trips;
          if (trips < 2 || trips > INT32_MAX || finalInduction > INT32_MAX) {
            continue;
          }

          std::size_t guardIndex = loopStart + 2;
          while (guardIndex < condIndex && ir[guardIndex].op != IROp::BEQZ &&
                 ir[guardIndex].op != IROp::BNEZ) {
            ++guardIndex;
          }
          if (guardIndex == loopStart + 2 || guardIndex >= condIndex ||
              !ir[guardIndex].dest.isLabel() || !ir[guardIndex].src1.isLocalVar()) {
            continue;
          }
          const auto elsePosition = labelPositions.find(ir[guardIndex].dest.name);
          if (elsePosition == labelPositions.end() || elsePosition->second <= guardIndex + 1 ||
              elsePosition->second >= condIndex || labelReferences[ir[guardIndex].dest.name] != 1) {
            continue;
          }
          const std::size_t elseIndex = elsePosition->second;
          std::size_t thenJump = elseIndex;
          while (thenJump > guardIndex + 1 && ir[thenJump - 1].op == IROp::LABEL) {
            --thenJump;
          }
          if (thenJump == guardIndex + 1 || ir[thenJump - 1].op != IROp::BRANCH ||
              !ir[thenJump - 1].dest.isLabel()) {
            continue;
          }
          --thenJump;
          const auto joinPosition = labelPositions.find(ir[thenJump].dest.name);
          if (joinPosition == labelPositions.end() || joinPosition->second <= elseIndex ||
              joinPosition->second >= condIndex || labelReferences[ir[thenJump].dest.name] != 1) {
            continue;
          }
          const std::size_t joinIndex = joinPosition->second;

          std::unordered_map<std::string, PeriodicValue> symbolic;
          symbolic[induction] = {PeriodicValue::Kind::INDUCTION, 0, 0, 0};
          const auto symbolicOperand = [&](const Operand& operand) {
            if (operand.isImm()) {
              return PeriodicValue{PeriodicValue::Kind::CONSTANT, operand.immVal, 0, 1};
            }
            if (operand.isLocalVar()) {
              const auto found = symbolic.find(operand.name);
              if (found != symbolic.end()) {
                return found->second;
              }
              const auto constant = findUniqueConstant(operand.name);
              if (constant) {
                return PeriodicValue{PeriodicValue::Kind::CONSTANT, *constant, 0, 1};
              }
            }
            return PeriodicValue{};
          };
          const auto combinePeriod = [&](std::uint64_t lhs, std::uint64_t rhs) -> std::uint64_t {
            if (lhs == 0 || rhs == 0) {
              return 0;
            }
            const std::uint64_t divisor = gcd(lhs, rhs);
            if (lhs > kMaxPeriodicBranchPhases / (rhs / divisor)) {
              return 0;
            }
            const std::uint64_t result = lhs * (rhs / divisor);
            return result <= kMaxPeriodicBranchPhases ? result : 0;
          };

          bool periodicProof = true;
          for (std::size_t position = loopStart + 2; position < guardIndex && periodicProof;
               ++position) {
            const IRInst& inst = ir[position];
            if (inst.op == IROp::LOCAL_VAR_DECL && inst.src1.isNone()) {
              continue;
            }
            if (!inst.dest.isLocalVar() || !isCompilerTemp(inst.dest.name)) {
              periodicProof = false;
              break;
            }
            const PeriodicValue lhs = symbolicOperand(inst.src1);
            const PeriodicValue rhs = symbolicOperand(inst.src2);
            PeriodicValue result;
            if (inst.op == IROp::ASSIGN || inst.op == IROp::LOCAL_VAR_DECL) {
              result = lhs;
            } else if (inst.op == IROp::NOT) {
              if (lhs.kind == PeriodicValue::Kind::CONSTANT) {
                result = {PeriodicValue::Kind::CONSTANT, lhs.value == 0 ? 1 : 0, 0, 1};
              } else if (lhs.kind == PeriodicValue::Kind::PERIODIC) {
                result = {PeriodicValue::Kind::PERIODIC, 0, 0, lhs.period};
              }
            } else if (inst.op == IROp::DIV && lhs.kind == PeriodicValue::Kind::INDUCTION &&
                       rhs.kind == PeriodicValue::Kind::CONSTANT && rhs.value != 0 &&
                       rhs.value != 1 && rhs.value != -1 && rhs.value != INT32_MIN) {
              result = {PeriodicValue::Kind::QUOTIENT, 0, rhs.value, 0};
            } else if (inst.op == IROp::MOD && lhs.kind == PeriodicValue::Kind::INDUCTION &&
                       rhs.kind == PeriodicValue::Kind::CONSTANT && rhs.value != 0 &&
                       rhs.value != 1 && rhs.value != -1 && rhs.value != INT32_MIN) {
              const std::uint64_t period = static_cast<std::uint64_t>(
                  rhs.value < 0 ? -static_cast<std::int64_t>(rhs.value) : rhs.value);
              if (period <= kMaxPeriodicBranchPhases) {
                result = {PeriodicValue::Kind::PERIODIC, 0, 0, period};
              }
            } else if (inst.op == IROp::MUL) {
              const PeriodicValue* quotient = nullptr;
              const PeriodicValue* constant = nullptr;
              if (lhs.kind == PeriodicValue::Kind::QUOTIENT &&
                  rhs.kind == PeriodicValue::Kind::CONSTANT) {
                quotient = &lhs;
                constant = &rhs;
              } else if (rhs.kind == PeriodicValue::Kind::QUOTIENT &&
                         lhs.kind == PeriodicValue::Kind::CONSTANT) {
                quotient = &rhs;
                constant = &lhs;
              }
              if (quotient != nullptr && constant->value == quotient->divisor) {
                result = {PeriodicValue::Kind::QUOTIENT_PRODUCT, 0, quotient->divisor, 0};
              }
            } else if (inst.op == IROp::SUB && lhs.kind == PeriodicValue::Kind::INDUCTION &&
                       rhs.kind == PeriodicValue::Kind::QUOTIENT_PRODUCT) {
              const std::uint64_t period = static_cast<std::uint64_t>(
                  rhs.divisor < 0 ? -static_cast<std::int64_t>(rhs.divisor) : rhs.divisor);
              if (period <= kMaxPeriodicBranchPhases) {
                result = {PeriodicValue::Kind::PERIODIC, 0, 0, period};
              }
            }

            if (result.kind == PeriodicValue::Kind::UNKNOWN &&
                (inst.op == IROp::ADD || inst.op == IROp::SUB || inst.op == IROp::MUL ||
                 inst.op == IROp::DIV || inst.op == IROp::MOD || inst.op == IROp::LT ||
                 inst.op == IROp::GT || inst.op == IROp::LE || inst.op == IROp::GE ||
                 inst.op == IROp::EQ || inst.op == IROp::NE)) {
              const bool lhsPeriodic = lhs.kind == PeriodicValue::Kind::CONSTANT ||
                                       lhs.kind == PeriodicValue::Kind::PERIODIC;
              const bool rhsPeriodic = rhs.kind == PeriodicValue::Kind::CONSTANT ||
                                       rhs.kind == PeriodicValue::Kind::PERIODIC;
              if (lhsPeriodic && rhsPeriodic) {
                if (lhs.kind == PeriodicValue::Kind::CONSTANT &&
                    rhs.kind == PeriodicValue::Kind::CONSTANT) {
                  result = {PeriodicValue::Kind::CONSTANT, 0, 0, 1};
                } else {
                  const std::uint64_t period = combinePeriod(lhs.period, rhs.period);
                  if (period != 0) {
                    result = {PeriodicValue::Kind::PERIODIC, 0, 0, period};
                  }
                }
              }
            }
            if (result.kind == PeriodicValue::Kind::UNKNOWN) {
              periodicProof = false;
              break;
            }
            symbolic[inst.dest.name] = result;
          }
          const auto guardSymbol = symbolic.find(ir[guardIndex].src1.name);
          const bool hasPeriodicGuard =
              periodicProof && guardSymbol != symbolic.end() &&
              (guardSymbol->second.kind == PeriodicValue::Kind::CONSTANT ||
               guardSymbol->second.kind == PeriodicValue::Kind::PERIODIC);

          // 同一个 if/else 骨架也可能由单调的归纳变量阈值控制。把条件前缀证明为
          // `a*i+b <op> 0`，随后只在真值改变点切段；每段仍复用下面的仿射矩阵。
          struct AffineScalar {
            std::int64_t coefficient = 0;
            std::int64_t constant = 0;
          };
          struct ThresholdPredicate {
            std::int64_t coefficient = 0;
            std::int64_t constant = 0;
            IROp relation = IROp::NE;
          };
          std::unordered_map<std::string, AffineScalar> affineScalars;
          std::unordered_map<std::string, ThresholdPredicate> predicates;
          affineScalars[induction] = {1, 0};
          const auto checkedAdd64 = [](std::int64_t lhs,
                                       std::int64_t rhs) -> std::optional<std::int64_t> {
            std::int64_t result = 0;
            return __builtin_add_overflow(lhs, rhs, &result) ? std::nullopt
                                                             : std::optional<std::int64_t>(result);
          };
          const auto checkedSub64 = [](std::int64_t lhs,
                                       std::int64_t rhs) -> std::optional<std::int64_t> {
            std::int64_t result = 0;
            return __builtin_sub_overflow(lhs, rhs, &result) ? std::nullopt
                                                             : std::optional<std::int64_t>(result);
          };
          const auto checkedMul64 = [](std::int64_t lhs,
                                       std::int64_t rhs) -> std::optional<std::int64_t> {
            std::int64_t result = 0;
            return __builtin_mul_overflow(lhs, rhs, &result) ? std::nullopt
                                                             : std::optional<std::int64_t>(result);
          };
          const auto affineValue = [&](const Operand& operand) -> std::optional<AffineScalar> {
            if (operand.isImm()) {
              return AffineScalar{0, operand.immVal};
            }
            if (operand.isLocalVar()) {
              const auto found = affineScalars.find(operand.name);
              if (found != affineScalars.end()) {
                return found->second;
              }
              const auto constant = findUniqueConstant(operand.name);
              if (constant) {
                return AffineScalar{0, *constant};
              }
            }
            return std::nullopt;
          };
          const auto affineRangeFits = [&](const AffineScalar& value) {
            const auto evaluate = [&](std::int64_t inductionValue) -> std::optional<std::int64_t> {
              const auto product = checkedMul64(value.coefficient, inductionValue);
              return product ? checkedAdd64(*product, value.constant) : std::nullopt;
            };
            const auto first = evaluate(*initial);
            const auto last = evaluate(finalInduction - 1);
            return first && last && std::min(*first, *last) >= INT32_MIN &&
                   std::max(*first, *last) <= INT32_MAX;
          };
          const auto checkedAffine = [&](std::int64_t coefficient,
                                         std::int64_t constant) -> std::optional<AffineScalar> {
            const AffineScalar value{coefficient, constant};
            return affineRangeFits(value) ? std::optional<AffineScalar>(value) : std::nullopt;
          };
          const auto invertedRelation = [](IROp relation) -> std::optional<IROp> {
            switch (relation) {
            case IROp::LT:
              return IROp::GE;
            case IROp::GT:
              return IROp::LE;
            case IROp::LE:
              return IROp::GT;
            case IROp::GE:
              return IROp::LT;
            case IROp::EQ:
              return IROp::NE;
            case IROp::NE:
              return IROp::EQ;
            default:
              return std::nullopt;
            }
          };

          bool thresholdProof = true;
          for (std::size_t position = loopStart + 2; position < guardIndex && thresholdProof;
               ++position) {
            const IRInst& inst = ir[position];
            if (inst.op == IROp::LOCAL_VAR_DECL && inst.src1.isNone()) {
              continue;
            }
            if (!inst.dest.isLocalVar() || !isCompilerTemp(inst.dest.name)) {
              thresholdProof = false;
              break;
            }
            const auto lhs = affineValue(inst.src1);
            const auto rhs = affineValue(inst.src2);
            std::optional<AffineScalar> result;
            if (inst.op == IROp::ASSIGN || inst.op == IROp::LOCAL_VAR_DECL) {
              if (lhs) {
                result = *lhs;
              } else if (inst.src1.isLocalVar()) {
                const auto predicate = predicates.find(inst.src1.name);
                if (predicate != predicates.end()) {
                  predicates[inst.dest.name] = predicate->second;
                  continue;
                }
              }
            } else if ((inst.op == IROp::ADD || inst.op == IROp::SUB) && lhs && rhs) {
              const auto coefficient = inst.op == IROp::ADD
                                           ? checkedAdd64(lhs->coefficient, rhs->coefficient)
                                           : checkedSub64(lhs->coefficient, rhs->coefficient);
              const auto constant = inst.op == IROp::ADD
                                        ? checkedAdd64(lhs->constant, rhs->constant)
                                        : checkedSub64(lhs->constant, rhs->constant);
              if (coefficient && constant) {
                result = checkedAffine(*coefficient, *constant);
              }
            } else if (inst.op == IROp::MUL && lhs && rhs &&
                       (lhs->coefficient == 0 || rhs->coefficient == 0)) {
              const AffineScalar& varying = lhs->coefficient == 0 ? *rhs : *lhs;
              const std::int64_t factor = lhs->coefficient == 0 ? lhs->constant : rhs->constant;
              const auto coefficient = checkedMul64(varying.coefficient, factor);
              const auto constant = checkedMul64(varying.constant, factor);
              if (coefficient && constant) {
                result = checkedAffine(*coefficient, *constant);
              }
            } else if ((inst.op == IROp::LT || inst.op == IROp::GT || inst.op == IROp::LE ||
                        inst.op == IROp::GE || inst.op == IROp::EQ || inst.op == IROp::NE) &&
                       lhs && rhs) {
              const auto coefficient = checkedSub64(lhs->coefficient, rhs->coefficient);
              const auto constant = checkedSub64(lhs->constant, rhs->constant);
              if (coefficient && constant) {
                predicates[inst.dest.name] = {*coefficient, *constant, inst.op};
                continue;
              }
            } else if (inst.op == IROp::NOT && inst.src1.isLocalVar()) {
              const auto predicate = predicates.find(inst.src1.name);
              if (predicate != predicates.end()) {
                const auto relation = invertedRelation(predicate->second.relation);
                if (relation) {
                  predicates[inst.dest.name] = {predicate->second.coefficient,
                                                predicate->second.constant, *relation};
                  continue;
                }
              } else if (lhs) {
                predicates[inst.dest.name] = {lhs->coefficient, lhs->constant, IROp::EQ};
                continue;
              }
            }
            if (!result) {
              thresholdProof = false;
              break;
            }
            affineScalars[inst.dest.name] = *result;
          }
          std::optional<ThresholdPredicate> thresholdGuard;
          if (thresholdProof) {
            const auto predicate = predicates.find(ir[guardIndex].src1.name);
            if (predicate != predicates.end()) {
              thresholdGuard = predicate->second;
            } else {
              const auto scalar = affineScalars.find(ir[guardIndex].src1.name);
              if (scalar != affineScalars.end()) {
                thresholdGuard = {scalar->second.coefficient, scalar->second.constant, IROp::NE};
              }
            }
          }
          if (!hasPeriodicGuard && !thresholdGuard) {
            continue;
          }

          const auto evaluatePrefix = [&](int inductionValue) -> std::optional<int> {
            std::unordered_map<std::string, int> values;
            values[induction] = inductionValue;
            const auto operandValue = [&](const Operand& operand) -> std::optional<int> {
              if (operand.isImm()) {
                return operand.immVal;
              }
              if (operand.isLocalVar()) {
                const auto found = values.find(operand.name);
                if (found != values.end()) {
                  return found->second;
                }
                return findUniqueConstant(operand.name);
              }
              return std::nullopt;
            };
            for (std::size_t position = loopStart + 2; position < guardIndex; ++position) {
              const IRInst& inst = ir[position];
              if (inst.op == IROp::LOCAL_VAR_DECL && inst.src1.isNone()) {
                continue;
              }
              const auto lhs = operandValue(inst.src1);
              const auto rhs = operandValue(inst.src2);
              if (!lhs || !inst.dest.isLocalVar()) {
                return std::nullopt;
              }
              int value = 0;
              if (inst.op == IROp::ASSIGN || inst.op == IROp::LOCAL_VAR_DECL) {
                value = *lhs;
              } else if (inst.op == IROp::NOT) {
                value = *lhs == 0 ? 1 : 0;
              } else {
                if (!rhs) {
                  return std::nullopt;
                }
                switch (inst.op) {
                case IROp::ADD:
                  value = static_cast<std::int32_t>(static_cast<std::uint32_t>(*lhs) +
                                                    static_cast<std::uint32_t>(*rhs));
                  break;
                case IROp::SUB:
                  value = static_cast<std::int32_t>(static_cast<std::uint32_t>(*lhs) -
                                                    static_cast<std::uint32_t>(*rhs));
                  break;
                case IROp::MUL:
                  value = static_cast<std::int32_t>(static_cast<std::uint32_t>(
                      static_cast<std::uint64_t>(static_cast<std::uint32_t>(*lhs)) *
                      static_cast<std::uint32_t>(*rhs)));
                  break;
                case IROp::DIV:
                  if (*rhs == 0) {
                    return std::nullopt;
                  }
                  value = (*lhs == INT32_MIN && *rhs == -1) ? INT32_MIN : *lhs / *rhs;
                  break;
                case IROp::MOD:
                  if (*rhs == 0) {
                    return std::nullopt;
                  }
                  value = (*lhs == INT32_MIN && *rhs == -1) ? 0 : *lhs % *rhs;
                  break;
                case IROp::LT:
                  value = *lhs < *rhs ? 1 : 0;
                  break;
                case IROp::GT:
                  value = *lhs > *rhs ? 1 : 0;
                  break;
                case IROp::LE:
                  value = *lhs <= *rhs ? 1 : 0;
                  break;
                case IROp::GE:
                  value = *lhs >= *rhs ? 1 : 0;
                  break;
                case IROp::EQ:
                  value = *lhs == *rhs ? 1 : 0;
                  break;
                case IROp::NE:
                  value = *lhs != *rhs ? 1 : 0;
                  break;
                default:
                  return std::nullopt;
                }
              }
              values[inst.dest.name] = value;
            }
            const auto found = values.find(ir[guardIndex].src1.name);
            return found == values.end() ? std::nullopt : std::optional<int>(found->second);
          };

          std::uint64_t period = 0;
          std::vector<bool> useThen;
          struct ThresholdRun {
            std::uint64_t count = 0;
            bool useThen = false;
          };
          std::vector<ThresholdRun> thresholdRuns;
          if (hasPeriodicGuard) {
            period = guardSymbol->second.kind == PeriodicValue::Kind::CONSTANT
                         ? 1
                         : guardSymbol->second.period;
            if (period == 0 || period > kMaxPeriodicBranchPhases ||
                static_cast<std::int64_t>(*initial) + static_cast<std::int64_t>(period) - 1 >
                    INT32_MAX) {
              continue;
            }
            useThen.assign(period, false);
            bool valuesKnown = true;
            for (std::uint64_t phase = 0; phase < period; ++phase) {
              const auto value = evaluatePrefix(*initial + static_cast<int>(phase));
              if (!value) {
                valuesKnown = false;
                break;
              }
              const bool branchTaken = ir[guardIndex].op == IROp::BEQZ ? *value == 0 : *value != 0;
              useThen[phase] = !branchTaken;
            }
            if (!valuesKnown) {
              continue;
            }
          } else {
            ThresholdPredicate predicate = *thresholdGuard;
            if (predicate.coefficient < 0) {
              if (predicate.coefficient == INT64_MIN || predicate.constant == INT64_MIN) {
                continue;
              }
              predicate.coefficient = -predicate.coefficient;
              predicate.constant = -predicate.constant;
              switch (predicate.relation) {
              case IROp::LT:
                predicate.relation = IROp::GT;
                break;
              case IROp::GT:
                predicate.relation = IROp::LT;
                break;
              case IROp::LE:
                predicate.relation = IROp::GE;
                break;
              case IROp::GE:
                predicate.relation = IROp::LE;
                break;
              default:
                break;
              }
            }
            const auto predicateValue =
                [&](std::uint64_t iteration) -> std::optional<std::int64_t> {
              const std::int64_t inductionValue =
                  static_cast<std::int64_t>(*initial) + static_cast<std::int64_t>(iteration);
              const auto product = checkedMul64(predicate.coefficient, inductionValue);
              return product ? checkedAdd64(*product, predicate.constant) : std::nullopt;
            };
            const auto firstPredicate = predicateValue(0);
            const auto lastPredicate = predicateValue(static_cast<std::uint64_t>(trips) - 1);
            if (!firstPredicate || !lastPredicate) {
              continue;
            }
            const auto firstMatching = [&](bool strictPositive) {
              std::uint64_t low = 0;
              std::uint64_t high = static_cast<std::uint64_t>(trips);
              while (low < high) {
                const std::uint64_t middle = low + (high - low) / 2;
                const std::int64_t value = *predicateValue(middle);
                const bool matches = strictPositive ? value > 0 : value >= 0;
                if (matches) {
                  high = middle;
                } else {
                  low = middle + 1;
                }
              }
              return low;
            };
            std::vector<std::uint64_t> boundaries{0, static_cast<std::uint64_t>(trips)};
            if (predicate.coefficient != 0) {
              boundaries.push_back(firstMatching(false));
              boundaries.push_back(firstMatching(true));
            }
            std::sort(boundaries.begin(), boundaries.end());
            boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());
            const auto comparisonHolds = [&](std::int64_t value) {
              switch (predicate.relation) {
              case IROp::LT:
                return value < 0;
              case IROp::GT:
                return value > 0;
              case IROp::LE:
                return value <= 0;
              case IROp::GE:
                return value >= 0;
              case IROp::EQ:
                return value == 0;
              case IROp::NE:
                return value != 0;
              default:
                return false;
              }
            };
            for (std::size_t index = 1; index < boundaries.size(); ++index) {
              const std::uint64_t begin = boundaries[index - 1];
              const std::uint64_t end = boundaries[index];
              if (begin == end) {
                continue;
              }
              const bool truth = comparisonHolds(*predicateValue(begin));
              const bool branchTaken = ir[guardIndex].op == IROp::BEQZ ? !truth : truth;
              const bool thenBranch = !branchTaken;
              if (!thresholdRuns.empty() && thresholdRuns.back().useThen == thenBranch) {
                thresholdRuns.back().count += end - begin;
              } else {
                thresholdRuns.push_back({end - begin, thenBranch});
              }
            }
            if (thresholdRuns.empty()) {
              continue;
            }
          }

          std::vector<std::string> variables;
          std::unordered_map<std::string, std::size_t> variableIndex;
          std::unordered_set<std::string> written;
          const auto addVariable = [&](const Operand& operand) {
            if (!operand.isLocalVar() || variableIndex.count(operand.name) != 0) {
              return;
            }
            variableIndex[operand.name] = variables.size();
            variables.push_back(operand.name);
          };
          const auto collectAffineRange = [&](std::size_t begin, std::size_t end) {
            for (std::size_t position = begin; position < end; ++position) {
              const IRInst& inst = ir[position];
              if (inst.op == IROp::LOCAL_VAR_DECL && inst.src1.isNone()) {
                continue;
              }
              if (inst.op != IROp::LOCAL_VAR_DECL && inst.op != IROp::ASSIGN &&
                  inst.op != IROp::ADD && inst.op != IROp::SUB && inst.op != IROp::MUL) {
                return false;
              }
              if (!inst.dest.isLocalVar() || (!inst.src1.isImm() && !inst.src1.isLocalVar()) ||
                  (!inst.src2.isNone() && !inst.src2.isImm() && !inst.src2.isLocalVar())) {
                return false;
              }
              addVariable(inst.dest);
              addVariable(inst.src1);
              addVariable(inst.src2);
              written.insert(inst.dest.name);
            }
            return true;
          };
          const std::size_t thenBegin = guardIndex + 1;
          const std::size_t elseBegin = elseIndex + 1;
          const std::size_t suffixBegin = joinIndex + 1;
          if (!collectAffineRange(thenBegin, thenJump) ||
              !collectAffineRange(elseBegin, joinIndex) ||
              !collectAffineRange(suffixBegin, condIndex)) {
            continue;
          }
          addVariable(Operand::localVar(induction));
          if (variables.empty() || variables.size() > 20 || written.count(induction) == 0 ||
              (condition.src2.isLocalVar() && written.count(condition.src2.name) != 0)) {
            continue;
          }

          const std::size_t constantColumn = variables.size();
          const std::size_t dimension = variables.size() + 1;
          const auto applyAffineRange = [&](std::size_t begin, std::size_t end,
                                            AffineMatrix& transform) {
            const auto rowForOperand = [&](const Operand& operand) -> std::optional<AffineRow> {
              AffineRow row(dimension, 0);
              if (operand.isImm()) {
                row[constantColumn] = static_cast<std::uint32_t>(operand.immVal);
                return row;
              }
              if (!operand.isLocalVar()) {
                return std::nullopt;
              }
              const auto found = variableIndex.find(operand.name);
              return found == variableIndex.end()
                         ? std::nullopt
                         : std::optional<AffineRow>(transform[found->second]);
            };
            for (std::size_t position = begin; position < end; ++position) {
              const IRInst& inst = ir[position];
              if (inst.op == IROp::LOCAL_VAR_DECL && inst.src1.isNone()) {
                continue;
              }
              const auto destination = variableIndex.find(inst.dest.name);
              const auto lhs = rowForOperand(inst.src1);
              if (destination == variableIndex.end() || !lhs) {
                return false;
              }
              if (inst.op == IROp::LOCAL_VAR_DECL || inst.op == IROp::ASSIGN) {
                transform[destination->second] = *lhs;
                continue;
              }
              const auto rhs = rowForOperand(inst.src2);
              if (!rhs) {
                return false;
              }
              AffineRow result(dimension, 0);
              if (inst.op == IROp::ADD || inst.op == IROp::SUB) {
                for (std::size_t column = 0; column < dimension; ++column) {
                  result[column] = inst.op == IROp::ADD ? (*lhs)[column] + (*rhs)[column]
                                                        : (*lhs)[column] - (*rhs)[column];
                }
              } else {
                const auto lhsConstant = rowIsConstant(*lhs, constantColumn);
                const auto rhsConstant = rowIsConstant(*rhs, constantColumn);
                if (!lhsConstant && !rhsConstant) {
                  return false;
                }
                const std::uint32_t factor = lhsConstant ? *lhsConstant : *rhsConstant;
                const AffineRow& value = lhsConstant ? *rhs : *lhs;
                for (std::size_t column = 0; column < dimension; ++column) {
                  result[column] = static_cast<std::uint32_t>(
                      static_cast<std::uint64_t>(value[column]) * factor);
                }
              }
              transform[destination->second] = std::move(result);
            }
            return true;
          };

          AffineMatrix thenTransform = identityMatrix(dimension);
          AffineMatrix elseTransform = identityMatrix(dimension);
          if (!applyAffineRange(thenBegin, thenJump, thenTransform) ||
              !applyAffineRange(suffixBegin, condIndex, thenTransform) ||
              !applyAffineRange(elseBegin, joinIndex, elseTransform) ||
              !applyAffineRange(suffixBegin, condIndex, elseTransform)) {
            continue;
          }
          const std::size_t inductionIndex = variableIndex.at(induction);
          const auto canonicalInduction = [&](const AffineMatrix& transform) {
            const AffineRow& row = transform[inductionIndex];
            if (row[constantColumn] != 1) {
              return false;
            }
            for (std::size_t column = 0; column < constantColumn; ++column) {
              if (row[column] != (column == inductionIndex ? 1u : 0u)) {
                return false;
              }
            }
            return true;
          };
          if (!canonicalInduction(thenTransform) || !canonicalInduction(elseTransform)) {
            continue;
          }

          std::size_t loopEnd = condIndex + 3;
          const auto usedAfterLoop = [&](const std::string& name) {
            for (std::size_t position = loopEnd; position < ir.size(); ++position) {
              const IRInst& inst = ir[position];
              if (inst.op == IROp::FUNC_BEGIN || inst.op == IROp::FUNC_END) {
                break;
              }
              if ((inst.src1.isLocalVar() && inst.src1.name == name) ||
                  (inst.src2.isLocalVar() && inst.src2.name == name) ||
                  ((inst.op == IROp::RETURN || inst.op == IROp::PARAM) && inst.dest.isLocalVar() &&
                   inst.dest.name == name)) {
                return true;
              }
            }
            return false;
          };
          bool resetsObservableState = false;
          for (const std::string& name : written) {
            if (name == induction || isCompilerTemp(name) || !usedAfterLoop(name)) {
              continue;
            }
            const std::size_t rowIndex = variableIndex.at(name);
            if (rowIsConstant(thenTransform[rowIndex], constantColumn) ||
                rowIsConstant(elseTransform[rowIndex], constantColumn)) {
              resetsObservableState = true;
              break;
            }
          }
          if (resetsObservableState) {
            continue;
          }

          AffineMatrix summary = identityMatrix(dimension);
          const auto appendRepeatedTransform = [&](const AffineMatrix& transform,
                                                   std::uint64_t count) {
            AffineMatrix power = transform;
            AffineMatrix repeated = identityMatrix(dimension);
            while (count != 0) {
              if ((count & 1u) != 0) {
                repeated = multiplyMatrices(power, repeated);
              }
              count >>= 1;
              if (count != 0) {
                power = multiplyMatrices(power, power);
              }
            }
            summary = multiplyMatrices(repeated, summary);
          };
          if (hasPeriodicGuard) {
            AffineMatrix periodTransform = identityMatrix(dimension);
            for (std::uint64_t phase = 0; phase < period; ++phase) {
              const AffineMatrix& iteration = useThen[phase] ? thenTransform : elseTransform;
              periodTransform = multiplyMatrices(iteration, periodTransform);
            }
            appendRepeatedTransform(periodTransform, static_cast<std::uint64_t>(trips) / period);
            const std::uint64_t remainder = static_cast<std::uint64_t>(trips) % period;
            for (std::uint64_t phase = 0; phase < remainder; ++phase) {
              const AffineMatrix& iteration = useThen[phase] ? thenTransform : elseTransform;
              summary = multiplyMatrices(iteration, summary);
            }
          } else {
            for (const ThresholdRun& run : thresholdRuns) {
              appendRepeatedTransform(run.useThen ? thenTransform : elseTransform, run.count);
            }
          }

          bool observableChange = false;
          bool validDependencies = true;
          for (std::size_t rowIndex = 0; rowIndex < variables.size(); ++rowIndex) {
            const std::string& name = variables[rowIndex];
            if (!written.count(name) || !usedAfterLoop(name)) {
              continue;
            }
            for (std::size_t column = 0; column < variables.size(); ++column) {
              if (summary[rowIndex][column] != 0 && isCompilerTemp(variables[column])) {
                validDependencies = false;
              }
            }
            for (std::size_t column = 0; column < dimension; ++column) {
              const std::uint32_t expected = column == rowIndex ? 1u : 0u;
              if (summary[rowIndex][column] != expected) {
                observableChange = true;
              }
            }
          }
          if (!observableChange || !validDependencies) {
            continue;
          }

          std::vector<IRInst> replacement;
          std::vector<std::pair<std::string, std::string>> finalMoves;
          bool writeFinalInduction = false;
          for (std::size_t rowIndex = 0; rowIndex < variables.size(); ++rowIndex) {
            const std::string& name = variables[rowIndex];
            if (!written.count(name) || !usedAfterLoop(name)) {
              continue;
            }
            bool changedRow = false;
            for (std::size_t column = 0; column < dimension; ++column) {
              const std::uint32_t expected = column == rowIndex ? 1u : 0u;
              if (summary[rowIndex][column] != expected) {
                changedRow = true;
                break;
              }
            }
            if (!changedRow) {
              continue;
            }
            if (name == induction) {
              writeFinalInduction = true;
              continue;
            }
            const std::string finalName = newTemp();
            bool haveValue = false;
            for (std::size_t column = 0; column < variables.size(); ++column) {
              const std::uint32_t coefficient = summary[rowIndex][column];
              if (coefficient == 0) {
                continue;
              }
              Operand term = Operand::localVar(variables[column]);
              if (coefficient != 1) {
                const std::string productName = newTemp();
                replacement.emplace_back(IROp::MUL, Operand::localVar(productName), term,
                                         Operand::imm(static_cast<std::int32_t>(coefficient)));
                term = Operand::localVar(productName);
              }
              if (!haveValue) {
                replacement.emplace_back(IROp::ASSIGN, Operand::localVar(finalName), term,
                                         Operand::none());
                haveValue = true;
              } else {
                replacement.emplace_back(IROp::ADD, Operand::localVar(finalName),
                                         Operand::localVar(finalName), term);
              }
            }
            const std::uint32_t constant = summary[rowIndex][constantColumn];
            if (!haveValue) {
              replacement.emplace_back(IROp::ASSIGN, Operand::localVar(finalName),
                                       Operand::imm(static_cast<std::int32_t>(constant)),
                                       Operand::none());
            } else if (constant != 0) {
              replacement.emplace_back(IROp::ADD, Operand::localVar(finalName),
                                       Operand::localVar(finalName),
                                       Operand::imm(static_cast<std::int32_t>(constant)));
            }
            finalMoves.push_back({name, finalName});
          }
          for (const auto& move : finalMoves) {
            replacement.emplace_back(IROp::ASSIGN, Operand::localVar(move.first),
                                     Operand::localVar(move.second), Operand::none());
          }
          if (writeFinalInduction) {
            replacement.emplace_back(IROp::ASSIGN, Operand::localVar(induction),
                                     Operand::imm(static_cast<int>(finalInduction)),
                                     Operand::none());
          }
          if (replacement.empty()) {
            continue;
          }

          if (loopEnd < ir.size() && ir[loopEnd].op == IROp::LABEL && ir[loopEnd].dest.isLabel() &&
              labelReferences[ir[loopEnd].dest.name] == 0) {
            ++loopEnd;
          }
          ir.erase(ir.begin() + static_cast<std::ptrdiff_t>(loopStart),
                   ir.begin() + static_cast<std::ptrdiff_t>(loopEnd));
          ir.insert(ir.begin() + static_cast<std::ptrdiff_t>(loopStart), replacement.begin(),
                    replacement.end());
          summarized = true;
        }
        if (!summarized) {
          break;
        }
      }
    }

    // Pass 6: 常数迭代循环消除（闭合形式）
    // 模式（循环反转后）：
    //   <循环前> ASSIGN i, #i0; ASSIGN s, #s0;
    //   BRANCH Lc; LABEL Lb; ADD s, s, #c; ADD i, i, #1;
    //   LABEL Lc; LT t, i, k; BNEZ Lb; <循环后>
    // 其中 k 为立即数（或循环前赋常数的 k 变量），循环体仅含 s、i 的自增。
    // 此时迭代次数 T = max(0, k - i0)（LT）或 max(0, k - i0 + 1)（LE），
    // s 的最终值 s0 + c*T 在编译期可算，整个循环替换为常量赋值。
    {
      const auto isSelfInc = [](const IRInst& inst, const std::string& name, int& step) {
        if ((inst.op == IROp::ADD || inst.op == IROp::SUB) && inst.dest.isLocalVar() &&
            inst.dest.name == name && inst.src1.isLocalVar() && inst.src1.name == name &&
            inst.src2.isImm()) {
          step = (inst.op == IROp::ADD) ? inst.src2.immVal : -inst.src2.immVal;
          return true;
        }
        return false;
      };
      std::unordered_set<std::string> referencedLabels;
      for (const auto& inst : ir) {
        if ((inst.op == IROp::BRANCH || inst.op == IROp::BEQZ || inst.op == IROp::BNEZ) &&
            inst.dest.isLabel()) {
          referencedLabels.insert(inst.dest.name);
        }
      }
      const auto foldPostlude = [&](std::size_t begin,
                                    std::unordered_map<std::string, int> constants,
                                    std::size_t& end, int& returnValue) {
        const auto resolve = [&](const Operand& operand) -> std::optional<int> {
          if (operand.isImm()) {
            return operand.immVal;
          }
          if (operand.isLocalVar()) {
            auto found = constants.find(operand.name);
            if (found != constants.end()) {
              return found->second;
            }
          }
          return std::nullopt;
        };
        const auto foldBinary = [](IROp op, int lhs, int rhs) -> std::optional<int> {
          const long long wideLhs = lhs;
          const long long wideRhs = rhs;
          long long wideResult = 0;
          switch (op) {
          case IROp::ADD:
            wideResult = wideLhs + wideRhs;
            break;
          case IROp::SUB:
            wideResult = wideLhs - wideRhs;
            break;
          case IROp::MUL:
            wideResult = wideLhs * wideRhs;
            break;
          case IROp::DIV:
            if (rhs == 0 || (lhs == INT32_MIN && rhs == -1)) {
              return std::nullopt;
            }
            return lhs / rhs;
          case IROp::MOD:
            if (rhs == 0 || (lhs == INT32_MIN && rhs == -1)) {
              return std::nullopt;
            }
            return lhs % rhs;
          case IROp::LT:
            return lhs < rhs ? 1 : 0;
          case IROp::GT:
            return lhs > rhs ? 1 : 0;
          case IROp::LE:
            return lhs <= rhs ? 1 : 0;
          case IROp::GE:
            return lhs >= rhs ? 1 : 0;
          case IROp::EQ:
            return lhs == rhs ? 1 : 0;
          case IROp::NE:
            return lhs != rhs ? 1 : 0;
          default:
            return std::nullopt;
          }
          if (wideResult < INT32_MIN || wideResult > INT32_MAX) {
            return std::nullopt;
          }
          return static_cast<int>(wideResult);
        };

        for (std::size_t index = begin; index < ir.size(); ++index) {
          const auto& inst = ir[index];
          if (inst.op == IROp::LABEL) {
            if (referencedLabels.count(inst.dest.name) != 0) {
              return false;
            }
            continue;
          }
          if (inst.op == IROp::LOCAL_VAR_DECL && inst.src1.isNone()) {
            continue;
          }
          if (inst.op == IROp::ASSIGN || inst.op == IROp::LOCAL_VAR_DECL) {
            const auto value = resolve(inst.src1);
            if (!value || !inst.dest.isLocalVar()) {
              return false;
            }
            constants[inst.dest.name] = *value;
            continue;
          }
          if (inst.op == IROp::NOT && inst.dest.isLocalVar()) {
            const auto value = resolve(inst.src1);
            if (!value) {
              return false;
            }
            constants[inst.dest.name] = *value == 0 ? 1 : 0;
            continue;
          }
          if (isCombinableOp(inst.op) && inst.dest.isLocalVar()) {
            const auto lhs = resolve(inst.src1);
            const auto rhs = resolve(inst.src2);
            if (!lhs || !rhs) {
              return false;
            }
            const auto value = foldBinary(inst.op, *lhs, *rhs);
            if (!value) {
              return false;
            }
            constants[inst.dest.name] = *value;
            continue;
          }
          if (inst.op == IROp::RETURN) {
            const auto value = inst.dest.isNone() ? std::optional<int>(0) : resolve(inst.dest);
            if (!value) {
              return false;
            }
            returnValue = *value;
            end = index + 1;
            // IR generation appends a default return after an explicit return.
            // It is unreachable on this straight-line path and can be skipped.
            while (end < ir.size() && ir[end].op == IROp::RETURN) {
              ++end;
            }
            return true;
          }
          return false;
        }
        return false;
      };
      // 初值/上限查找：从循环入口（li）向前扫描同一基本块内最近的定义；
      // 遇到对目标变量的任何定义都停止，且只接受 ASSIGN #imm 或
      // LOCAL_VAR_DECL #imm（`int i = 0;` 形式）作为常量初始化
      const auto findConstInit = [&](std::size_t li, const std::string& name, int& value) {
        for (std::size_t k = li; k > 0; --k) {
          const IRInst& prev = ir[k - 1];
          if (prev.dest.isLocalVar() && prev.dest.name == name) {
            const bool constInit =
                (prev.op == IROp::ASSIGN || prev.op == IROp::LOCAL_VAR_DECL) && prev.src1.isImm();
            if (constInit) {
              value = prev.src1.immVal;
              return true;
            }
            return false; // 定义不是常量初始化，无法确认初值
          }
          if (prev.op == IROp::LABEL || prev.op == IROp::BRANCH || prev.op == IROp::BEQZ ||
              prev.op == IROp::BNEZ || prev.op == IROp::CALL) {
            return false;
          }
        }
        return false;
      };
      std::vector<IRInst> optimized;
      std::size_t li = 0;
      bool loopEliminated = false;
      while (li < ir.size()) {
        bool eliminatedHere = false;
        if (ir[li].op == IROp::BRANCH && li + 1 < ir.size() && ir[li + 1].op == IROp::LABEL) {
          const std::string condLabel = ir[li].dest.name;
          const std::string bodyLabel = ir[li + 1].dest.name;
          std::size_t condIdx = li + 2;
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
          if (condIdx < ir.size() && bnezIdx < ir.size() && condIdx > li + 2) {
            // 条件必须是 LT/LE 且 src1 为计数变量
            const IRInst& cond = ir[condIdx + 1];
            const bool isLT = cond.op == IROp::LT;
            const bool isLE = cond.op == IROp::LE;
            const std::string indName = (cond.src1.isLocalVar()) ? cond.src1.name : std::string();
            if ((isLT || isLE) && !indName.empty()) {
              // ---- 解析循环体：计数变量自增 + 临时变量（循环变量线性函数）+ 累加变量 ----
              // 支持任意数量的累加变量，每个累加变量可以是自身加/减常量、加/减循环变量、
              // 或加/减循环变量的线性函数（通过临时变量 t = i*c 间接累加）。
              // 临时变量必须是循环变量的线性函数：t = i*c, t = i+c, t = i, t = c 等。
              struct AccVar {
                std::string name;
                int step;     // 每次迭代的常量增量（可正可负）
                int indCoeff; // 循环变量系数（0=不加循环变量, +1/-1=加/减循环变量）
              };
              // 临时变量的线性表示：value = indCoeff * i + constOffset
              struct TempLin {
                int indCoeff = 0;
                int constOffset = 0;
              };
              std::unordered_map<std::string, TempLin> tempLinMap;
              std::vector<AccVar> accVars;
              int indStep = 0;
              bool bodyOk = true;
              bool indIncremented = false; // 循环变量是否已自增
              for (std::size_t k = li + 2; k < condIdx; ++k) {
                // 声明本身不执行计算。前面的传播/DCE 常把循环内 copy/CSE
                // 局部量的初始化完全消掉，只留下无初始化声明；它们不应阻止
                // 已有的闭合形式证明。带初始化的声明仍按普通定义处理。
                if (ir[k].op == IROp::LOCAL_VAR_DECL && ir[k].src1.isNone()) {
                  continue;
                }
                int step = 0;
                // 计数变量自增
                if (isSelfInc(ir[k], indName, step)) {
                  indStep += step;
                  indIncremented = true;
                  continue;
                }
                // 临时变量定义：t = L1 op L2，其中 L 是立即数 / 归纳变量 / 已知线性临时变量
                // （支持组合：t2 = t1 + i，t1 = i*c → t2 = (c+1)*i）
                if ((ir[k].op == IROp::MUL || ir[k].op == IROp::ADD || ir[k].op == IROp::SUB ||
                     ir[k].op == IROp::ASSIGN) &&
                    ir[k].dest.isLocalVar() && !indIncremented) {
                  const auto& d = ir[k].dest;
                  bool isTempDef = true;
                  if (d.name == indName)
                    isTempDef = false;
                  // 允许 dest == src1：Pass 5 复用同名临时后出现
                  // `ADD t, t, i`（t 的新定义 = 旧 t 值 + i）。线性组合用
                  // 映射中旧值参与，随后覆盖为新定义，语义正确。
                  for (const auto& acc : accVars) {
                    if (acc.name == d.name)
                      isTempDef = false;
                  }
                  if (isTempDef) {
                    auto linOf = [&](const Operand& op) -> std::optional<TempLin> {
                      if (op.isImm())
                        return TempLin{0, op.immVal};
                      if (op.isLocalVar()) {
                        if (op.name == indName)
                          return TempLin{1, 0};
                        auto it = tempLinMap.find(op.name);
                        if (it != tempLinMap.end())
                          return it->second;
                      }
                      return std::nullopt;
                    };
                    const auto l1 = linOf(ir[k].src1);
                    const auto l2 = linOf(ir[k].src2);
                    if (l1 && l2) {
                      TempLin tl;
                      if (ir[k].op == IROp::MUL) {
                        // t = L1 * L2：两边都含归纳变量则非线性
                        if (l1->indCoeff != 0 && l2->indCoeff != 0) {
                          // 不作为线性临时变量，交给后续检查
                        } else {
                          tl.indCoeff =
                              l1->indCoeff * l2->constOffset + l2->indCoeff * l1->constOffset;
                          tl.constOffset = l1->constOffset * l2->constOffset;
                          tempLinMap[d.name] = tl;
                          continue;
                        }
                      } else if (ir[k].op == IROp::ADD) {
                        tl.indCoeff = l1->indCoeff + l2->indCoeff;
                        tl.constOffset = l1->constOffset + l2->constOffset;
                        tempLinMap[d.name] = tl;
                        continue;
                      } else if (ir[k].op == IROp::SUB) {
                        tl.indCoeff = l1->indCoeff - l2->indCoeff;
                        tl.constOffset = l1->constOffset - l2->constOffset;
                        tempLinMap[d.name] = tl;
                        continue;
                      } else { // ASSIGN：t = L1
                        tempLinMap[d.name] = *l1;
                        continue;
                      }
                    }
                  }
                }
                // 检查是否为已识别累加变量的再次常量自增
                bool matched = false;
                for (auto& acc : accVars) {
                  if (isSelfInc(ir[k], acc.name, step)) {
                    acc.step += step;
                    matched = true;
                    break;
                  }
                }
                if (matched) {
                  continue;
                }
                // 检查是否为已识别累加变量加/减循环变量（s += i 或 s -= i）
                for (auto& acc : accVars) {
                  if ((ir[k].op == IROp::ADD || ir[k].op == IROp::SUB) && ir[k].dest.isLocalVar() &&
                      ir[k].dest.name == acc.name && ir[k].src1.isLocalVar() &&
                      ir[k].src1.name == acc.name && ir[k].src2.isLocalVar() &&
                      ir[k].src2.name == indName) {
                    if (!indIncremented) {
                      acc.indCoeff += (ir[k].op == IROp::ADD) ? 1 : -1;
                      matched = true;
                    } else {
                      bodyOk = false;
                    }
                    break;
                  }
                }
                if (matched) {
                  continue;
                }
                // 检查是否为累加变量加/减临时变量（s += t，t 是循环变量线性函数）
                for (auto& acc : accVars) {
                  if ((ir[k].op == IROp::ADD || ir[k].op == IROp::SUB) && ir[k].dest.isLocalVar() &&
                      ir[k].dest.name == acc.name && ir[k].src1.isLocalVar() &&
                      ir[k].src1.name == acc.name && ir[k].src2.isLocalVar()) {
                    auto tit = tempLinMap.find(ir[k].src2.name);
                    if (tit != tempLinMap.end() && !indIncremented) {
                      int sign = (ir[k].op == IROp::ADD) ? 1 : -1;
                      acc.indCoeff += sign * tit->second.indCoeff;
                      acc.step += sign * tit->second.constOffset;
                      matched = true;
                      break;
                    }
                  }
                }
                if (matched) {
                  continue;
                }
                // 新增累加变量：ADD/SUB dest, dest, #imm 形式
                if ((ir[k].op == IROp::ADD || ir[k].op == IROp::SUB) && ir[k].dest.isLocalVar() &&
                    ir[k].src1.isLocalVar() && ir[k].src1.name == ir[k].dest.name &&
                    ir[k].src2.isImm() && ir[k].dest.name != indName) {
                  AccVar acc;
                  acc.name = ir[k].dest.name;
                  acc.step = (ir[k].op == IROp::ADD) ? ir[k].src2.immVal : -ir[k].src2.immVal;
                  acc.indCoeff = 0;
                  accVars.push_back(acc);
                  continue;
                }
                // 新增累加变量：ADD/SUB dest, dest, indName 形式（s += i）
                if ((ir[k].op == IROp::ADD || ir[k].op == IROp::SUB) && ir[k].dest.isLocalVar() &&
                    ir[k].src1.isLocalVar() && ir[k].src1.name == ir[k].dest.name &&
                    ir[k].src2.isLocalVar() && ir[k].src2.name == indName &&
                    ir[k].dest.name != indName) {
                  if (!indIncremented) {
                    AccVar acc;
                    acc.name = ir[k].dest.name;
                    acc.step = 0;
                    acc.indCoeff = (ir[k].op == IROp::ADD) ? 1 : -1;
                    accVars.push_back(acc);
                    continue;
                  }
                  bodyOk = false;
                  break;
                }
                // 新增累加变量：ADD/SUB dest, dest, temp 形式（s += t，t 是临时变量）
                if ((ir[k].op == IROp::ADD || ir[k].op == IROp::SUB) && ir[k].dest.isLocalVar() &&
                    ir[k].src1.isLocalVar() && ir[k].src1.name == ir[k].dest.name &&
                    ir[k].src2.isLocalVar() && ir[k].dest.name != indName) {
                  auto tit = tempLinMap.find(ir[k].src2.name);
                  if (tit != tempLinMap.end() && !indIncremented) {
                    AccVar acc;
                    acc.name = ir[k].dest.name;
                    int sign = (ir[k].op == IROp::ADD) ? 1 : -1;
                    acc.indCoeff = sign * tit->second.indCoeff;
                    acc.step = sign * tit->second.constOffset;
                    accVars.push_back(acc);
                    continue;
                  }
                }
                bodyOk = false;
                break;
              }
              // 循环条件上限必须保持不变；否则用入口值计算固定迭代次数会错误。
              if (cond.src2.isLocalVar()) {
                for (std::size_t k = li + 2; k < condIdx; ++k) {
                  if (ir[k].dest.isLocalVar() && ir[k].dest.name == cond.src2.name) {
                    bodyOk = false;
                    break;
                  }
                }
              }

              // 上限：立即数或循环前赋常数的 k 变量
              int upper = 0;
              bool upperKnown = false;
              if (cond.src2.isImm()) {
                upper = cond.src2.immVal;
                upperKnown = true;
              } else if (cond.src2.isLocalVar()) {
                upperKnown = findConstInit(li, cond.src2.name, upper);
              }
              // i 初值
              int indInit = 0;
              const bool indInitKnown = findConstInit(li, indName, indInit);
              // 所有累加变量初值必须已知
              bool allAccInitKnown = !accVars.empty();
              std::vector<std::pair<std::string, int>> accFinals; // (name, final_value)
              for (const auto& acc : accVars) {
                int accInit = 0;
                if (!findConstInit(li, acc.name, accInit)) {
                  allAccInitKnown = false;
                  break;
                }
                accFinals.push_back({acc.name, accInit});
              }
              if (bodyOk && !accVars.empty() && allAccInitKnown && indInitKnown && upperKnown &&
                  indStep == 1) {
                // 迭代次数（编译期常数）
                int trips = isLT ? (upper - indInit) : (upper - indInit + 1);
                if (trips < 0) {
                  trips = 0;
                }
                const bool ran = isLT ? (indInit < upper) : (indInit <= upper);
                const int indFinal = ran ? upper : indInit;
                // 计算每个累加变量的最终值
                for (std::size_t a = 0; a < accVars.size(); ++a) {
                  accFinals[a].second += accVars[a].step * trips;
                  // 等差数列求和：s += i 时，i 从 indInit 到 indInit+trips-1
                  // sum = trips*indInit + trips*(trips-1)/2
                  if (accVars[a].indCoeff != 0 && trips > 0) {
                    long long sum = static_cast<long long>(trips) * indInit +
                                    static_cast<long long>(trips) * (trips - 1) / 2;
                    accFinals[a].second += accVars[a].indCoeff * static_cast<int>(sum);
                  }
                }
                std::unordered_map<std::string, int> finalConstants;
                for (const auto& final : accFinals) {
                  finalConstants[final.first] = final.second;
                }
                finalConstants[indName] = indFinal;
                std::size_t foldedEnd = bnezIdx + 1;
                int foldedReturn = 0;
                const bool foldedPostlude =
                    foldPostlude(bnezIdx + 1, finalConstants, foldedEnd, foldedReturn);

                // Initial values and a hoisted bound that feed only the removed
                // loop become dead. Remove just the nearest definition when it
                // has no earlier use between that definition and the loop.
                const auto eraseDeadInit = [&](const std::string& name) {
                  for (std::size_t pos = optimized.size(); pos > 0; --pos) {
                    const auto& candidate = optimized[pos - 1];
                    if (!candidate.dest.isLocalVar() || candidate.dest.name != name ||
                        candidate.op == IROp::RETURN || candidate.op == IROp::PARAM) {
                      continue;
                    }
                    bool used = false;
                    for (std::size_t use = pos; use < optimized.size(); ++use) {
                      const auto& later = optimized[use];
                      used = (later.src1.isLocalVar() && later.src1.name == name) ||
                             (later.src2.isLocalVar() && later.src2.name == name) ||
                             ((later.op == IROp::RETURN || later.op == IROp::PARAM) &&
                              later.dest.isLocalVar() && later.dest.name == name);
                      if (used) {
                        break;
                      }
                    }
                    if (!used) {
                      if (candidate.op == IROp::LOCAL_VAR_DECL) {
                        optimized[pos - 1].src1 = Operand::none();
                      } else {
                        optimized.erase(optimized.begin() + static_cast<std::ptrdiff_t>(pos - 1));
                      }
                    }
                    return;
                  }
                };
                if (foldedPostlude) {
                  for (const auto& final : accFinals) {
                    eraseDeadInit(final.first);
                  }
                  eraseDeadInit(indName);
                  if (cond.src2.isLocalVar()) {
                    eraseDeadInit(cond.src2.name);
                  }
                  optimized.push_back(IRInst(IROp::RETURN, Operand::imm(foldedReturn),
                                             Operand::none(), Operand::none()));
                  li = foldedEnd;
                  loopEliminated = true;
                  eliminatedHere = true;
                  continue;
                }
                // 循环后变量是否被使用（决定是否保留赋值）
                const auto usedAfter = [&](const std::string& name) {
                  for (std::size_t k = bnezIdx + 1; k < ir.size(); ++k) {
                    const auto& inst = ir[k];
                    if (inst.op == IROp::FUNC_BEGIN || inst.op == IROp::FUNC_END) {
                      break; // 跨函数不追踪
                    }
                    if (inst.src1.isLocalVar() && inst.src1.name == name) {
                      return true;
                    }
                    if (inst.src2.isLocalVar() && inst.src2.name == name) {
                      return true;
                    }
                    if ((inst.op == IROp::RETURN || inst.op == IROp::PARAM) &&
                        inst.dest.isLocalVar() && inst.dest.name == name) {
                      return true;
                    }
                  }
                  return false;
                };
                for (const auto& af : accFinals) {
                  if (usedAfter(af.first)) {
                    optimized.push_back(IRInst(IROp::ASSIGN, Operand::localVar(af.first),
                                               Operand::imm(af.second), Operand::none()));
                  }
                }
                if (usedAfter(indName)) {
                  optimized.push_back(IRInst(IROp::ASSIGN, Operand::localVar(indName),
                                             Operand::imm(indFinal), Operand::none()));
                }
                li = bnezIdx + 1;
                loopEliminated = true;
                eliminatedHere = true;
              }
            }
          }
        }
        if (!eliminatedHere) {
          optimized.push_back(ir[li]);
          ++li;
        }
      }
      if (loopEliminated) {
        ir = std::move(optimized);
        changed = true;
      }
    }
  } // 结束迭代优化循环

  // Pass 7: 循环展开（2 倍）—— 在迭代优化收敛后执行一次
  // 对未被 Pass 6 消除的反转循环，若循环体为直线代码（无内部分支），
  // 展开循环体 2 倍以减少分支开销。仅展开满足以下条件的循环：
  //   1. 反转循环结构：BRANCH Lc; LABEL Lb; <body>; LABEL Lc; <cond>; BNEZ Lb
  //   2. 循环体为直线代码（无 LABEL/BRANCH/BEQZ/BNEZ/CALL/RETURN）
  //   3. 条件为 LT/LE，计数变量自增步长为常量
  //   4. 循环体不超过 24 条指令（避免代码膨胀）
  // 对 i < bound, i = i + 1 且 bound 循环不变的规范循环，用原条件预检、
  // 成对迭代和单次余数处理把回边分支减半；其余循环保守地在两份循环体之间
  // 保留条件检查。
  {
    std::vector<IRInst> optimized;
    std::size_t i = 0;
    bool unrolled = false;
    while (i < ir.size()) {
      bool unrolledHere = false;
      // 查找反转循环
      if (ir[i].op == IROp::BRANCH && i + 1 < ir.size() && ir[i + 1].op == IROp::LABEL) {
        const std::string condLabel = ir[i].dest.name;
        const std::string bodyLabel = ir[i + 1].dest.name;
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
        // generate() may call optimizePass() again after constant-call
        // specialization.  Both unroll forms leave their first synthetic exit
        // label as the next label after the backedge; do not treat that already
        // expanded loop as a fresh candidate on the next call.
        bool alreadyUnrolled = false;
        for (std::size_t k = bnezIdx + 1; k < ir.size(); ++k) {
          if (ir[k].op == IROp::FUNC_END || ir[k].op == IROp::FUNC_BEGIN) {
            break;
          }
          if (ir[k].op != IROp::LABEL) {
            continue;
          }
          alreadyUnrolled = ir[k].dest.isLabel() && ir[k].dest.name.rfind("L_unroll_exit_", 0) == 0;
          break;
        }
        if (!alreadyUnrolled && condIdx < ir.size() && bnezIdx < ir.size() && condIdx > i + 2) {
          // 条件必须是 LT/LE 且 src1 为局部变量
          const IRInst& cond = ir[condIdx + 1];
          const bool isLT = cond.op == IROp::LT;
          const bool isLE = cond.op == IROp::LE;
          if ((isLT || isLE) && cond.src1.isLocalVar()) {
            const std::string indName = cond.src1.name;
            // 循环体 [i+2, condIdx) 必须是直线代码
            bool straightLine = true;
            std::size_t bodyLen = condIdx - (i + 2);
            if (bodyLen == 0 || bodyLen > 24) {
              straightLine = false;
            }
            // 检查循环体无分支/调用，并识别计数变量自增
            std::size_t indIncIdx = ir.size();
            int indIncStep = 0;
            int indWriteCount = 0;
            for (std::size_t k = i + 2; k < condIdx && straightLine; ++k) {
              const auto& inst = ir[k];
              if (inst.op == IROp::LABEL || inst.op == IROp::BRANCH || inst.op == IROp::BEQZ ||
                  inst.op == IROp::BNEZ || inst.op == IROp::CALL || inst.op == IROp::RETURN) {
                straightLine = false;
                break;
              }
              if (inst.dest.isLocalVar() && inst.dest.name == indName) {
                ++indWriteCount;
              }
              // 识别计数变量自增
              if ((inst.op == IROp::ADD || inst.op == IROp::SUB) && inst.dest.isLocalVar() &&
                  inst.dest.name == indName && inst.src1.isLocalVar() &&
                  inst.src1.name == indName && inst.src2.isImm()) {
                indIncStep = (inst.op == IROp::ADD) ? inst.src2.immVal : -inst.src2.immVal;
                indIncIdx = k;
              }
            }
            if (straightLine && indIncIdx < ir.size() && indIncStep != 0) {
              bool boundInvariant =
                  cond.src2.isImm() || cond.src2.isLocalVar() || cond.src2.isGlobalVar();
              if (!cond.src2.isImm()) {
                for (std::size_t k = i + 2; k < condIdx && boundInvariant; ++k) {
                  if (ir[k].dest.type == cond.src2.type && ir[k].dest.name == cond.src2.name) {
                    boundInvariant = false;
                  }
                }
              }

              // 对规范的 i < bound, i = i + 1 循环使用成对迭代。先以原条件
              // 保护零次循环和 bound == INT_MIN，再用 bound - 1 判断是否至少
              // 还剩两次迭代。热路径每两轮只保留一个回边分支，退出时再执行
              // 一次原条件来处理 0/1 个余数。上界必须在循环体内不变。
              const bool canPairWithoutMidGuard = isLT && indIncStep == 1 && indWriteCount == 1 &&
                                                  boundInvariant && bnezIdx == condIdx + 2;
              if (canPairWithoutMidGuard) {
                const std::string pairLimitName = "u" + std::to_string(i);
                const Operand pairLimit = Operand::localVar(pairLimitName);
                const std::string exitLabel = "L_unroll_exit_" + std::to_string(i);

                // pair_limit = bound - 1。即使 bound 为 INT_MIN，原条件预检也会
                // 在使用回绕后的 pair_limit 前退出，保持 RV32 整数语义。
                optimized.push_back(IRInst(IROp::SUB, pairLimit, cond.src2, Operand::imm(1)));

                // 原条件预检：零次循环直接退出。
                optimized.push_back(cond);
                optimized.push_back(
                    IRInst(IROp::BEQZ, Operand::label(exitLabel), cond.dest, Operand::none()));
                // 首次跳到成对循环的尾条件。
                optimized.push_back(ir[i]);
                // LABEL Lb
                optimized.push_back(ir[i + 1]);

                // 每次成对执行两份循环体。
                for (int copy = 0; copy < 2; ++copy) {
                  for (std::size_t k = i + 2; k < condIdx; ++k) {
                    optimized.push_back(ir[k]);
                  }
                }

                // LABEL Lc; i < pair_limit; BNEZ Lb
                optimized.push_back(ir[condIdx]);
                IRInst pairCond = cond;
                pairCond.src2 = pairLimit;
                optimized.push_back(std::move(pairCond));
                optimized.push_back(ir[bnezIdx]);

                // 若还剩恰好一次迭代，执行一份余数循环体。
                optimized.push_back(cond);
                optimized.push_back(
                    IRInst(IROp::BEQZ, Operand::label(exitLabel), cond.dest, Operand::none()));
                for (std::size_t k = i + 2; k < condIdx; ++k) {
                  optimized.push_back(ir[k]);
                }
                optimized.push_back(IRInst(IROp::LABEL, Operand::label(exitLabel), Operand::none(),
                                           Operand::none()));

                i = bnezIdx + 1;
                unrolledHere = true;
                unrolled = true;
                continue;
              }

              // 展开 2 倍：
              // 原始: BRANCH Lc; LABEL Lb; <body>; LABEL Lc; <cond>; BNEZ Lb
              // 展开: BRANCH Lc; LABEL Lb;
              //       <body_copy1>;              // 第一次执行
              //       <cond_mid>; BNEZ L_mid;    // 中间条件检查（满足则继续）
              //       BRANCH L_exit;             // 不满足则跳出
              //       LABEL L_mid;
              //       <body_copy2>;              // 第二次执行
              //       LABEL Lc; <cond>; BNEZ Lb; // 原循环尾条件
              //       LABEL L_exit;
              const std::string midLabel = "L_unroll_mid_" + std::to_string(i);
              const std::string exitLabel = "L_unroll_exit_" + std::to_string(i);

              // BRANCH Lc（首跳）
              optimized.push_back(ir[i]);
              // LABEL Lb
              optimized.push_back(ir[i + 1]);

              // body_copy1：复制循环体，保持原样
              for (std::size_t k = i + 2; k < condIdx; ++k) {
                optimized.push_back(ir[k]);
              }

              // 中间条件检查：复制条件块，BNEZ 改为跳到 midLabel
              for (std::size_t k = condIdx + 1; k < bnezIdx; ++k) {
                optimized.push_back(ir[k]);
              }
              // BNEZ midLabel（条件满足，继续第二次执行）
              optimized.push_back(
                  IRInst(IROp::BNEZ, Operand::label(midLabel), ir[bnezIdx].src1, Operand::none()));
              // BRANCH exitLabel（条件不满足，跳出循环）
              optimized.push_back(IRInst(IROp::BRANCH, Operand::label(exitLabel), Operand::none(),
                                         Operand::none()));
              // LABEL midLabel
              optimized.push_back(
                  IRInst(IROp::LABEL, Operand::label(midLabel), Operand::none(), Operand::none()));

              // body_copy2：再次复制循环体
              for (std::size_t k = i + 2; k < condIdx; ++k) {
                optimized.push_back(ir[k]);
              }

              // 原循环尾: LABEL Lc; <cond>; BNEZ Lb
              optimized.push_back(ir[condIdx]); // LABEL Lc
              for (std::size_t k = condIdx + 1; k <= bnezIdx; ++k) {
                optimized.push_back(ir[k]);
              }

              // LABEL exitLabel
              optimized.push_back(
                  IRInst(IROp::LABEL, Operand::label(exitLabel), Operand::none(), Operand::none()));

              i = bnezIdx + 1;
              unrolledHere = true;
              unrolled = true;
            }
          }
        }
      }
      if (!unrolledHere) {
        optimized.push_back(ir[i]);
        ++i;
      }
    }
    if (unrolled) {
      ir = std::move(optimized);
    }
  }
}

} // namespace toycc
