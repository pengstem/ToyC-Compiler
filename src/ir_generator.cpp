#include "ir_generator.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdint>
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
  constexpr std::size_t kLoopInlineLimit = 96;

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
      const std::size_t inlineLimit = isLoopCall(ci) ? kLoopInlineLimit : kInlineLimit;
      if (bodySize(f) > inlineLimit)
        continue;

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
    // 则复用之前的结果。用户变量可被重新赋值，须跟踪重定义并使相关条目失效。
    {
      std::unordered_map<std::string, std::string> rename;   // 原名 → 复用名（仅临时变量）
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

  // Pass 4.5: 删除结果完全不可观察的纯计数循环。
  // DCE 会先清掉循环体中的死算术，但归纳变量与回边本身互相活跃，普通
  // 指令级活跃性无法删掉它们。这里仅处理无内部控制流、调用、内存写入或
  // 全局写入的反转循环；若体内所有被定义的局部值在循环后均不再使用，
  // 整个循环可按 C 的 as-if 规则删除。
  {
    bool removedLoop = true;
    while (removedLoop) {
      removedLoop = false;
      std::unordered_map<std::string, int> labelReferences;
      for (const auto& inst : ir) {
        if ((inst.op == IROp::BRANCH || inst.op == IROp::BEQZ || inst.op == IROp::BNEZ) &&
            inst.dest.isLabel()) {
          ++labelReferences[inst.dest.name];
        }
      }

      for (std::size_t index = 0; index + 1 < ir.size() && !removedLoop; ++index) {
        if (ir[index].op != IROp::BRANCH || !ir[index].dest.isLabel() ||
            ir[index + 1].op != IROp::LABEL || !ir[index + 1].dest.isLabel()) {
          continue;
        }
        const std::string condLabel = ir[index].dest.name;
        const std::string bodyLabel = ir[index + 1].dest.name;
        if (labelReferences[condLabel] != 1 || labelReferences[bodyLabel] != 1) {
          continue;
        }

        std::size_t condIndex = index + 2;
        while (condIndex < ir.size() &&
               !(ir[condIndex].op == IROp::LABEL && ir[condIndex].dest.isLabel() &&
                 ir[condIndex].dest.name == condLabel)) {
          ++condIndex;
        }
        if (condIndex + 2 >= ir.size() || ir[condIndex + 2].op != IROp::BNEZ ||
            !ir[condIndex + 2].dest.isLabel() || ir[condIndex + 2].dest.name != bodyLabel) {
          continue;
        }

        bool pureStraightBody = condIndex > index + 2;
        std::unordered_set<std::string> definitions;
        for (std::size_t body = index + 2; body < condIndex && pureStraightBody; ++body) {
          const auto& inst = ir[body];
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
            pureStraightBody = false;
            continue;
          }
          if (!inst.dest.isLocalVar()) {
            pureStraightBody = false;
            continue;
          }
          definitions.insert(inst.dest.name);
        }
        if (!pureStraightBody || definitions.empty()) {
          continue;
        }

        std::size_t loopEnd = condIndex + 3;
        bool liveAfter = false;
        for (std::size_t after = loopEnd; after < ir.size() && !liveAfter; ++after) {
          const auto& inst = ir[after];
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
        ir.erase(ir.begin() + static_cast<std::ptrdiff_t>(index),
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
        if (condIdx < ir.size() && bnezIdx < ir.size() && condIdx > i + 2) {
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
