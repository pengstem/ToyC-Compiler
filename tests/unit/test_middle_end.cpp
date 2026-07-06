#include <iostream>
#include <sstream>

#include "ast_nodes.h"
#include "semantic_analyzer.h"
#include "ir_generator.h"

using namespace toycc;

// ============================================================
// 测试辅助
// ============================================================

static int g_testCount = 0;
static int g_passCount = 0;

void testHeader(const std::string& name) {
    std::cout << "  [" << (++g_testCount) << "] " << name << " ... ";
}

void testPass() {
    std::cout << "PASSED\n";
    g_passCount++;
}

void testFail(const std::string& msg) {
    std::cout << "FAILED: " << msg << "\n";
}

// ============================================================
// AST 构建辅助函数
// ============================================================

static std::shared_ptr<CompUnit> makeCompUnit() {
    return std::make_shared<CompUnit>();
}

static std::shared_ptr<FuncDef> makeFuncDef(
    Type retType, const std::string& name,
    std::vector<std::shared_ptr<FuncParam>> params,
    std::shared_ptr<Block> body)
{
    auto f = std::make_shared<FuncDef>();
    f->retType = retType;
    f->name = name;
    f->params = std::move(params);
    f->body = std::move(body);
    return f;
}

static std::shared_ptr<FuncParam> makeParam(Type type, const std::string& name) {
    auto p = std::make_shared<FuncParam>();
    p->type = type;
    p->name = name;
    return p;
}

static std::shared_ptr<Block> makeBlock(std::vector<std::shared_ptr<Stmt>> stmts) {
    auto b = std::make_shared<Block>();
    b->stmts = std::move(stmts);
    return b;
}

static std::shared_ptr<ExprStmt> makeExprStmt(std::shared_ptr<Expr> expr) {
    auto s = std::make_shared<ExprStmt>();
    s->expr = std::move(expr);
    return s;
}

static std::shared_ptr<DeclStmt> makeDeclStmt(std::shared_ptr<Decl> decl) {
    return std::make_shared<DeclStmt>(std::move(decl));
}

static std::shared_ptr<ReturnStmt> makeReturn(std::shared_ptr<Expr> val = nullptr) {
    auto s = std::make_shared<ReturnStmt>();
    s->value = std::move(val);
    return s;
}

static std::shared_ptr<IntLiteral> makeInt(int val) {
    return std::make_shared<IntLiteral>(val);
}

static std::shared_ptr<VarExpr> makeVar(const std::string& name) {
    return std::make_shared<VarExpr>(name);
}

static std::shared_ptr<BinaryExpr> makeBinary(
    BinOp op, std::shared_ptr<Expr> lhs, std::shared_ptr<Expr> rhs)
{
    return std::make_shared<BinaryExpr>(op, std::move(lhs), std::move(rhs));
}

static std::shared_ptr<CallExpr> makeCall(
    const std::string& name, std::vector<std::shared_ptr<Expr>> args)
{
    return std::make_shared<CallExpr>(name, std::move(args));
}

static std::shared_ptr<Decl> makeDecl(
    bool isConst, const std::string& name, std::shared_ptr<Expr> init = nullptr)
{
    auto d = std::make_shared<Decl>();
    d->isConst = isConst;
    d->name = name;
    d->initExpr = std::move(init);
    return d;
}

static std::shared_ptr<IfStmt> makeIf(
    std::shared_ptr<Expr> cond, std::shared_ptr<Stmt> thenBranch,
    std::shared_ptr<Stmt> elseBranch = nullptr)
{
    auto s = std::make_shared<IfStmt>();
    s->condition = std::move(cond);
    s->thenBranch = std::move(thenBranch);
    s->elseBranch = std::move(elseBranch);
    return s;
}

static std::shared_ptr<WhileStmt> makeWhile(
    std::shared_ptr<Expr> cond, std::shared_ptr<Stmt> body)
{
    auto s = std::make_shared<WhileStmt>();
    s->condition = std::move(cond);
    s->body = std::move(body);
    return s;
}

static std::shared_ptr<BreakStmt> makeBreak() {
    return std::make_shared<BreakStmt>();
}

static std::shared_ptr<ContinueStmt> makeContinue() {
    return std::make_shared<ContinueStmt>();
}

static std::shared_ptr<UnaryExpr> makeUnary(UnaryOp op, std::shared_ptr<Expr> e) {
    return std::make_shared<UnaryExpr>(op, std::move(e));
}

// ============================================================
// IR 工具
// ============================================================

std::string irToString(const std::vector<IRInst>& ir) {
    std::ostringstream oss;
    for (const auto& inst : ir) {
        oss << inst << "\n";
    }
    return oss.str();
}

bool irContains(const std::vector<IRInst>& ir, const std::string& substr) {
    return irToString(ir).find(substr) != std::string::npos;
}

// ============================================================
// 语义分析测试
// ============================================================

void testSema_basicMain() {
    testHeader("basic main function");
    auto comp = makeCompUnit();
    comp->globalDecls.push_back(makeFuncDef(
        Type::INT, "main", {},
        makeBlock({ makeReturn(makeInt(0)) })));
    SemanticAnalyzer sema;
    if (sema.analyze(*comp)) {
        testPass();
    } else {
        testFail("expected no errors");
    }
}

void testSema_voidReturnValue() {
    testHeader("void function should not return value");
    auto comp = makeCompUnit();
    comp->globalDecls.push_back(makeFuncDef(
        Type::VOID, "f", {},
        makeBlock({ makeReturn(makeInt(42)) })));
    SemanticAnalyzer sema;
    bool ok = sema.analyze(*comp);
    if (!ok && sema.getErrors().size() == 1) {
        testPass();
    } else {
        testFail("expected 1 error");
    }
}

void testSema_nonVoidMustReturn() {
    testHeader("non-void function must return value");
    auto comp = makeCompUnit();
    comp->globalDecls.push_back(makeFuncDef(
        Type::INT, "main", {},
        makeBlock({ makeReturn() })));
    SemanticAnalyzer sema;
    bool ok = sema.analyze(*comp);
    if (!ok && !sema.getErrors().empty()) {
        testPass();
    } else {
        testFail("expected error");
    }
}

void testSema_undefinedVar() {
    testHeader("undefined variable error");
    auto comp = makeCompUnit();
    comp->globalDecls.push_back(makeFuncDef(
        Type::INT, "main", {},
        makeBlock({ makeReturn(makeVar("x")) })));
    SemanticAnalyzer sema;
    bool ok = sema.analyze(*comp);
    if (!ok && !sema.getErrors().empty()) {
        testPass();
    } else {
        testFail("expected error");
    }
}

void testSema_constFold() {
    testHeader("constant folding: 1 + 2 * 3 - 4 = 3");
    auto expr = makeBinary(BinOp::SUB,
        makeBinary(BinOp::ADD, makeInt(1),
            makeBinary(BinOp::MUL, makeInt(2), makeInt(3))),
        makeInt(4));
    SemanticAnalyzer sema;
    int result = 0;
    bool folded = sema.tryConstFold(expr.get(), result);
    if (folded && result == 3) {
        testPass();
    } else {
        testFail("expected 3, got " + std::to_string(result));
    }
}

void testSema_duplicateVar() {
    testHeader("duplicate variable in same scope");
    auto comp = makeCompUnit();
    comp->globalDecls.push_back(makeDecl(false, "g", makeInt(1)));
    comp->globalDecls.push_back(makeDecl(false, "g", makeInt(2)));
    comp->globalDecls.push_back(makeFuncDef(
        Type::INT, "main", {},
        makeBlock({ makeReturn(makeInt(0)) })));
    SemanticAnalyzer sema;
    bool ok = sema.analyze(*comp);
    if (!ok && !sema.getErrors().empty()) {
        testPass();
    } else {
        testFail("expected error");
    }
}

void testSema_breakOutsideLoop() {
    testHeader("break outside loop");
    auto comp = makeCompUnit();
    comp->globalDecls.push_back(makeFuncDef(
        Type::INT, "main", {},
        makeBlock({ makeBreak() })));
    SemanticAnalyzer sema;
    bool ok = sema.analyze(*comp);
    if (!ok && !sema.getErrors().empty()) {
        testPass();
    } else {
        testFail("expected error");
    }
}

void testSema_assignToConst() {
    testHeader("global const declaration");
    auto comp = makeCompUnit();
    comp->globalDecls.push_back(makeDecl(true, "c", makeInt(10)));
    comp->globalDecls.push_back(makeFuncDef(
        Type::INT, "main", {},
        makeBlock({ makeReturn(makeVar("c")) })));
    SemanticAnalyzer sema;
    if (sema.analyze(*comp)) {
        testPass();
    } else {
        testFail("unexpected error");
    }
}

// ============================================================
// IR 生成测试
// ============================================================

void testIR_simpleReturn() {
    testHeader("IR: simple return 42");
    auto comp = makeCompUnit();
    comp->globalDecls.push_back(makeFuncDef(
        Type::INT, "main", {},
        makeBlock({ makeReturn(makeInt(42)) })));
    SemanticAnalyzer sema;
    sema.analyze(*comp);
    IRGenerator irGen(sema.getSymbolTable());
    auto ir = irGen.generate(*comp);
    if (irContains(ir, "FUNC_BEGIN main") &&
        irContains(ir, "RETURN #42") &&
        irContains(ir, "FUNC_END main")) {
        testPass();
    } else {
        testFail("IR structure mismatch:\n" + irToString(ir));
    }
}

void testIR_arithmetic() {
    testHeader("IR: arithmetic expression (1+2)*3");
    auto comp = makeCompUnit();
    comp->globalDecls.push_back(makeFuncDef(
        Type::INT, "main", {},
        makeBlock({ makeReturn(
            makeBinary(BinOp::MUL,
                makeBinary(BinOp::ADD, makeInt(1), makeInt(2)),
                makeInt(3))) })));
    SemanticAnalyzer sema;
    sema.analyze(*comp);
    IRGenerator irGen(sema.getSymbolTable());
    auto ir = irGen.generate(*comp);
    if (irContains(ir, "ADD") && irContains(ir, "MUL") && irContains(ir, "RETURN")) {
        testPass();
    } else {
        testFail("missing arithmetic IR:\n" + irToString(ir));
    }
}

void testIR_shortCircuitAnd() {
    testHeader("IR: short-circuit && (1 && 0)");
    auto comp = makeCompUnit();
    comp->globalDecls.push_back(makeFuncDef(
        Type::INT, "main", {},
        makeBlock({ makeReturn(
            makeBinary(BinOp::AND, makeInt(1), makeInt(0))) })));
    SemanticAnalyzer sema;
    sema.analyze(*comp);
    IRGenerator irGen(sema.getSymbolTable());
    auto ir = irGen.generate(*comp);
    if (irContains(ir, "BEQZ") && irContains(ir, "ASSIGN") && irContains(ir, "BRANCH")) {
        testPass();
    } else {
        testFail("short-circuit AND should use BEQZ:\n" + irToString(ir));
    }
}

void testIR_shortCircuitOr() {
    testHeader("IR: short-circuit || (1 || 0)");
    auto comp = makeCompUnit();
    comp->globalDecls.push_back(makeFuncDef(
        Type::INT, "main", {},
        makeBlock({ makeReturn(
            makeBinary(BinOp::OR, makeInt(1), makeInt(0))) })));
    SemanticAnalyzer sema;
    sema.analyze(*comp);
    IRGenerator irGen(sema.getSymbolTable());
    auto ir = irGen.generate(*comp);
    if (irContains(ir, "BNEZ") && irContains(ir, "ASSIGN")) {
        testPass();
    } else {
        testFail("short-circuit OR should use BNEZ:\n" + irToString(ir));
    }
}

void testIR_ifStatement() {
    testHeader("IR: if statement");
    auto comp = makeCompUnit();
    comp->globalDecls.push_back(makeFuncDef(
        Type::INT, "main", {},
        makeBlock({
            makeIf(makeInt(1), makeReturn(makeInt(100))),
            makeReturn(makeInt(0))
        })));
    SemanticAnalyzer sema;
    sema.analyze(*comp);
    IRGenerator irGen(sema.getSymbolTable());
    auto ir = irGen.generate(*comp);
    if (irContains(ir, "BEQZ") && irContains(ir, "LABEL") && irContains(ir, "RETURN #100")) {
        testPass();
    } else {
        testFail("if IR structure mismatch:\n" + irToString(ir));
    }
}

void testIR_whileLoop() {
    testHeader("IR: while loop");
    auto comp = makeCompUnit();
    comp->globalDecls.push_back(makeFuncDef(
        Type::INT, "main", {},
        makeBlock({
            makeWhile(makeInt(1), makeBreak()),
            makeReturn(makeInt(0))
        })));
    SemanticAnalyzer sema;
    sema.analyze(*comp);
    IRGenerator irGen(sema.getSymbolTable());
    auto ir = irGen.generate(*comp);
    if (irContains(ir, "BEQZ") && irContains(ir, "BRANCH") && irContains(ir, "LABEL")) {
        testPass();
    } else {
        testFail("while IR structure mismatch:\n" + irToString(ir));
    }
}

void testIR_functionCall() {
    testHeader("IR: function call");
    auto comp = makeCompUnit();
    comp->globalDecls.push_back(makeFuncDef(
        Type::INT, "add", {makeParam(Type::INT, "a"), makeParam(Type::INT, "b")},
        makeBlock({ makeReturn(
            makeBinary(BinOp::ADD, makeVar("a"), makeVar("b"))) })));
    comp->globalDecls.push_back(makeFuncDef(
        Type::INT, "main", {},
        makeBlock({ makeReturn(
            makeCall("add", {makeInt(1), makeInt(2)})) })));
    SemanticAnalyzer sema;
    sema.analyze(*comp);
    IRGenerator irGen(sema.getSymbolTable());
    auto ir = irGen.generate(*comp);
    if (irContains(ir, "FUNC_BEGIN add") &&
        irContains(ir, "FUNC_END add") &&
        irContains(ir, "PARAM") &&
        irContains(ir, "CALL") &&
        irContains(ir, "FUNC_BEGIN main")) {
        testPass();
    } else {
        testFail("function call IR mismatch:\n" + irToString(ir));
    }
}

void testIR_voidFunction() {
    testHeader("IR: void function with no return value");
    auto comp = makeCompUnit();
    comp->globalDecls.push_back(makeFuncDef(
        Type::VOID, "f", {},
        makeBlock({})));
    comp->globalDecls.push_back(makeFuncDef(
        Type::INT, "main", {},
        makeBlock({
            makeExprStmt(makeCall("f", {})),
            makeReturn(makeInt(1))
        })));
    SemanticAnalyzer sema;
    sema.analyze(*comp);
    IRGenerator irGen(sema.getSymbolTable());
    auto ir = irGen.generate(*comp);
    if (irContains(ir, "FUNC_BEGIN f") &&
        irContains(ir, "RETURN") &&
        irContains(ir, "CALL")) {
        testPass();
    } else {
        testFail("void function IR mismatch:\n" + irToString(ir));
    }
}

void testIR_scopeVariable() {
    testHeader("IR: variable declaration with init");
    auto comp = makeCompUnit();
    comp->globalDecls.push_back(makeFuncDef(
        Type::INT, "main", {},
        makeBlock({
            makeDeclStmt(makeDecl(false, "x", makeInt(42))),
            makeReturn(makeVar("x"))
        })));
    SemanticAnalyzer sema;
    sema.analyze(*comp);
    IRGenerator irGen(sema.getSymbolTable());
    auto ir = irGen.generate(*comp);
    if (irContains(ir, "LOCAL_VAR_DECL %x")) {
        testPass();
    } else {
        testFail("variable decl IR missing:\n" + irToString(ir));
    }
}

void testIR_nestedScope() {
    testHeader("IR: nested block scope");
    auto comp = makeCompUnit();
    comp->globalDecls.push_back(makeFuncDef(
        Type::INT, "main", {},
        makeBlock({
            makeDeclStmt(makeDecl(false, "a", makeInt(1))),
            makeBlock({
                makeExprStmt(makeBinary(BinOp::ASSIGN, makeVar("a"),
                    makeBinary(BinOp::ADD, makeVar("a"), makeInt(2)))),
                makeDeclStmt(makeDecl(false, "a", makeInt(3))),
                makeExprStmt(makeBinary(BinOp::ASSIGN, makeVar("a"),
                    makeBinary(BinOp::ADD, makeVar("a"), makeInt(4)))),
            }),
            makeExprStmt(makeBinary(BinOp::ASSIGN, makeVar("a"),
                makeBinary(BinOp::ADD, makeVar("a"), makeInt(5)))),
            makeReturn(makeVar("a"))
        })));
    SemanticAnalyzer sema;
    sema.analyze(*comp);
    IRGenerator irGen(sema.getSymbolTable());
    auto ir = irGen.generate(*comp);
    if (irContains(ir, "LOCAL_VAR_DECL %a") && irContains(ir, "ASSIGN %a")) {
        testPass();
    } else {
        testFail("nested scope IR mismatch:\n" + irToString(ir));
    }
}

// ============================================================
// 一元表达式测试
// ============================================================

// --- 语义分析 ---

void testSema_unaryNeg() {
    testHeader("unary negate in expression");
    auto comp = makeCompUnit();
    comp->globalDecls.push_back(makeFuncDef(
        Type::INT, "main", {},
        makeBlock({ makeReturn(makeUnary(UnaryOp::NEG, makeInt(42))) })));
    SemanticAnalyzer sema;
    if (sema.analyze(*comp)) {
        testPass();
    } else {
        testFail("unexpected error");
    }
}

void testSema_unaryNot() {
    testHeader("unary logical NOT in expression");
    auto comp = makeCompUnit();
    comp->globalDecls.push_back(makeFuncDef(
        Type::INT, "main", {},
        makeBlock({ makeReturn(makeUnary(UnaryOp::NOT, makeInt(0))) })));
    SemanticAnalyzer sema;
    if (sema.analyze(*comp)) {
        testPass();
    } else {
        testFail("unexpected error");
    }
}

void testSema_unaryPos() {
    testHeader("unary plus in expression");
    auto comp = makeCompUnit();
    comp->globalDecls.push_back(makeFuncDef(
        Type::INT, "main", {},
        makeBlock({ makeReturn(makeUnary(UnaryOp::POS, makeInt(5))) })));
    SemanticAnalyzer sema;
    if (sema.analyze(*comp)) {
        testPass();
    } else {
        testFail("unexpected error");
    }
}

// --- IR 生成 ---

void testIR_unaryNeg() {
    testHeader("IR: unary negate (-42)");
    auto comp = makeCompUnit();
    comp->globalDecls.push_back(makeFuncDef(
        Type::INT, "main", {},
        makeBlock({ makeReturn(makeUnary(UnaryOp::NEG, makeInt(42))) })));
    SemanticAnalyzer sema;
    sema.analyze(*comp);
    IRGenerator irGen(sema.getSymbolTable());
    auto ir = irGen.generate(*comp);
    if (irContains(ir, "SUB") && irContains(ir, "#0")) {
        testPass();
    } else {
        testFail("negate should use SUB with 0:\n" + irToString(ir));
    }
}

void testIR_unaryNot() {
    testHeader("IR: unary logical NOT (!0)");
    auto comp = makeCompUnit();
    comp->globalDecls.push_back(makeFuncDef(
        Type::INT, "main", {},
        makeBlock({ makeReturn(makeUnary(UnaryOp::NOT, makeInt(0))) })));
    SemanticAnalyzer sema;
    sema.analyze(*comp);
    IRGenerator irGen(sema.getSymbolTable());
    auto ir = irGen.generate(*comp);
    if (irContains(ir, "NOT")) {
        testPass();
    } else {
        testFail("NOT IR missing:\n" + irToString(ir));
    }
}

// ============================================================
// 比较运算符测试
// ============================================================

void testIR_compareLT() {
    testHeader("IR: less than (3 < 5)");
    auto comp = makeCompUnit();
    comp->globalDecls.push_back(makeFuncDef(
        Type::INT, "main", {},
        makeBlock({ makeReturn(
            makeBinary(BinOp::LT, makeInt(3), makeInt(5))) })));
    SemanticAnalyzer sema;
    sema.analyze(*comp);
    IRGenerator irGen(sema.getSymbolTable());
    auto ir = irGen.generate(*comp);
    if (irContains(ir, "LT")) {
        testPass();
    } else {
        testFail("LT IR missing:\n" + irToString(ir));
    }
}

void testIR_compareGT() {
    testHeader("IR: greater than (5 > 3)");
    auto comp = makeCompUnit();
    comp->globalDecls.push_back(makeFuncDef(
        Type::INT, "main", {},
        makeBlock({ makeReturn(
            makeBinary(BinOp::GT, makeInt(5), makeInt(3))) })));
    SemanticAnalyzer sema;
    sema.analyze(*comp);
    IRGenerator irGen(sema.getSymbolTable());
    auto ir = irGen.generate(*comp);
    if (irContains(ir, "GT")) {
        testPass();
    } else {
        testFail("GT IR missing:\n" + irToString(ir));
    }
}

void testIR_compareLE() {
    testHeader("IR: less or equal (3 <= 3)");
    auto comp = makeCompUnit();
    comp->globalDecls.push_back(makeFuncDef(
        Type::INT, "main", {},
        makeBlock({ makeReturn(
            makeBinary(BinOp::LE, makeInt(3), makeInt(3))) })));
    SemanticAnalyzer sema;
    sema.analyze(*comp);
    IRGenerator irGen(sema.getSymbolTable());
    auto ir = irGen.generate(*comp);
    if (irContains(ir, "LE")) {
        testPass();
    } else {
        testFail("LE IR missing:\n" + irToString(ir));
    }
}

void testIR_compareGE() {
    testHeader("IR: greater or equal (5 >= 5)");
    auto comp = makeCompUnit();
    comp->globalDecls.push_back(makeFuncDef(
        Type::INT, "main", {},
        makeBlock({ makeReturn(
            makeBinary(BinOp::GE, makeInt(5), makeInt(5))) })));
    SemanticAnalyzer sema;
    sema.analyze(*comp);
    IRGenerator irGen(sema.getSymbolTable());
    auto ir = irGen.generate(*comp);
    if (irContains(ir, "GE")) {
        testPass();
    } else {
        testFail("GE IR missing:\n" + irToString(ir));
    }
}

void testIR_compareEQ() {
    testHeader("IR: equal (3 == 3)");
    auto comp = makeCompUnit();
    comp->globalDecls.push_back(makeFuncDef(
        Type::INT, "main", {},
        makeBlock({ makeReturn(
            makeBinary(BinOp::EQ, makeInt(3), makeInt(3))) })));
    SemanticAnalyzer sema;
    sema.analyze(*comp);
    IRGenerator irGen(sema.getSymbolTable());
    auto ir = irGen.generate(*comp);
    if (irContains(ir, "EQ")) {
        testPass();
    } else {
        testFail("EQ IR missing:\n" + irToString(ir));
    }
}

void testIR_compareNE() {
    testHeader("IR: not equal (3 != 5)");
    auto comp = makeCompUnit();
    comp->globalDecls.push_back(makeFuncDef(
        Type::INT, "main", {},
        makeBlock({ makeReturn(
            makeBinary(BinOp::NE, makeInt(3), makeInt(5))) })));
    SemanticAnalyzer sema;
    sema.analyze(*comp);
    IRGenerator irGen(sema.getSymbolTable());
    auto ir = irGen.generate(*comp);
    if (irContains(ir, "NE")) {
        testPass();
    } else {
        testFail("NE IR missing:\n" + irToString(ir));
    }
}

// ============================================================
// 常量折叠进阶测试
// ============================================================

void testSema_constFoldLogical() {
    testHeader("const fold: 1 || 0 && 1 = 1");
    auto expr = makeBinary(BinOp::OR, makeInt(1),
        makeBinary(BinOp::AND, makeInt(0), makeInt(1)));
    SemanticAnalyzer sema;
    int result = 0;
    bool folded = sema.tryConstFold(expr.get(), result);
    if (folded && result == 1) {
        testPass();
    } else {
        testFail("expected 1, got " + std::to_string(result));
    }
}

void testSema_constFoldCompare() {
    testHeader("const fold: 5 > 3 && 2 < 4 = 1");
    auto expr = makeBinary(BinOp::AND,
        makeBinary(BinOp::GT, makeInt(5), makeInt(3)),
        makeBinary(BinOp::LT, makeInt(2), makeInt(4)));
    SemanticAnalyzer sema;
    int result = 0;
    bool folded = sema.tryConstFold(expr.get(), result);
    if (folded && result == 1) {
        testPass();
    } else {
        testFail("expected 1, got " + std::to_string(result));
    }
}

void testSema_constFoldUnary() {
    testHeader("const fold: -(-(10)) = 10");
    auto expr = makeUnary(UnaryOp::NEG,
        makeUnary(UnaryOp::NEG, makeInt(10)));
    SemanticAnalyzer sema;
    int result = 0;
    bool folded = sema.tryConstFold(expr.get(), result);
    if (folded && result == 10) {
        testPass();
    } else {
        testFail("expected 10, got " + std::to_string(result));
    }
}

void testSema_constFoldNot() {
    testHeader("const fold: !0 = 1");
    auto expr = makeUnary(UnaryOp::NOT, makeInt(0));
    SemanticAnalyzer sema;
    int result = 0;
    bool folded = sema.tryConstFold(expr.get(), result);
    if (folded && result == 1) {
        testPass();
    } else {
        testFail("expected 1, got " + std::to_string(result));
    }
}

void testSema_constFoldDivMod() {
    testHeader("const fold: 10 / 3 * 3 + 10 % 3 = 10");
    auto expr = makeBinary(BinOp::ADD,
        makeBinary(BinOp::MUL,
            makeBinary(BinOp::DIV, makeInt(10), makeInt(3)),
            makeInt(3)),
        makeBinary(BinOp::MOD, makeInt(10), makeInt(3)));
    SemanticAnalyzer sema;
    int result = 0;
    bool folded = sema.tryConstFold(expr.get(), result);
    if (folded && result == 10) {
        testPass();
    } else {
        testFail("expected 10, got " + std::to_string(result));
    }
}

void testSema_constFoldDivByZero() {
    testHeader("const fold: division by zero rejected");
    auto expr = makeBinary(BinOp::DIV, makeInt(5), makeInt(0));
    SemanticAnalyzer sema;
    int result = 0;
    bool folded = sema.tryConstFold(expr.get(), result);
    if (!folded) {
        testPass();
    } else {
        testFail("division by zero should fail");
    }
}

void testSema_constFoldComplex() {
    testHeader("const fold: (1+2*3) > (10-5) && 4 == 4 = 1");
    auto expr = makeBinary(BinOp::AND,
        makeBinary(BinOp::GT,
            makeBinary(BinOp::ADD, makeInt(1),
                makeBinary(BinOp::MUL, makeInt(2), makeInt(3))),
            makeBinary(BinOp::SUB, makeInt(10), makeInt(5))),
        makeBinary(BinOp::EQ, makeInt(4), makeInt(4)));
    SemanticAnalyzer sema;
    int result = 0;
    bool folded = sema.tryConstFold(expr.get(), result);
    if (folded && result == 1) {
        testPass();
    } else {
        testFail("expected 1, got " + std::to_string(result));
    }
}

// ============================================================
// 作用域与变量屏蔽测试
// ============================================================

void testIR_varShadowing() {
    testHeader("IR: variable shadowing in nested block");
    auto comp = makeCompUnit();
    comp->globalDecls.push_back(makeFuncDef(
        Type::INT, "main", {},
        makeBlock({
            makeDeclStmt(makeDecl(false, "a", makeInt(1))),
            makeBlock({
                makeDeclStmt(makeDecl(false, "a", makeInt(100))),
                makeExprStmt(makeBinary(BinOp::ASSIGN, makeVar("a"),
                    makeBinary(BinOp::ADD, makeVar("a"), makeInt(1)))),
            }),
            makeReturn(makeVar("a"))  // 外层 a 仍为 1
        })));
    SemanticAnalyzer sema;
    sema.analyze(*comp);
    IRGenerator irGen(sema.getSymbolTable());
    auto ir = irGen.generate(*comp);
    // 内层 a 和外层 a 应该是不同的变量
    if (irContains(ir, "LOCAL_VAR_DECL %a") && irContains(ir, "ASSIGN %a")) {
        testPass();
    } else {
        testFail("shadowing IR mismatch:\n" + irToString(ir));
    }
}

void testSema_varNameShadowFunc() {
    testHeader("local var shadows function name");
    auto comp = makeCompUnit();
    comp->globalDecls.push_back(makeFuncDef(
        Type::INT, "main", {},
        makeBlock({
            makeDeclStmt(makeDecl(false, "main", makeInt(10))),
            makeReturn(makeVar("main"))
        })));
    SemanticAnalyzer sema;
    if (sema.analyze(*comp)) {
        testPass();
    } else {
        testFail("unexpected error");
    }
}

void testSema_multiLevelScope() {
    testHeader("three-level nested scope");
    auto comp = makeCompUnit();
    comp->globalDecls.push_back(makeFuncDef(
        Type::INT, "main", {},
        makeBlock({
            makeDeclStmt(makeDecl(false, "x", makeInt(1))),
            makeBlock({
                makeDeclStmt(makeDecl(false, "x", makeInt(2))),
                makeBlock({
                    makeDeclStmt(makeDecl(false, "x", makeInt(3))),
                    makeReturn(makeVar("x"))
                })
            })
        })));
    SemanticAnalyzer sema;
    if (sema.analyze(*comp)) {
        testPass();
    } else {
        testFail("unexpected error");
    }
}

// ============================================================
// 控制流进阶测试
// ============================================================

void testIR_ifElse() {
    testHeader("IR: if-else statement");
    auto comp = makeCompUnit();
    comp->globalDecls.push_back(makeFuncDef(
        Type::INT, "main", {},
        makeBlock({
            makeIf(makeInt(1),
                makeReturn(makeInt(100)),
                makeReturn(makeInt(200))),
            makeReturn(makeInt(0))
        })));
    SemanticAnalyzer sema;
    sema.analyze(*comp);
    IRGenerator irGen(sema.getSymbolTable());
    auto ir = irGen.generate(*comp);
    if (irContains(ir, "BEQZ") && irContains(ir, "BRANCH") &&
        irContains(ir, "RETURN #100") && irContains(ir, "RETURN #200")) {
        testPass();
    } else {
        testFail("if-else IR mismatch:\n" + irToString(ir));
    }
}

void testIR_nestedIf() {
    testHeader("IR: nested if");
    auto comp = makeCompUnit();
    comp->globalDecls.push_back(makeFuncDef(
        Type::INT, "main", {},
        makeBlock({
            makeIf(makeInt(1),
                makeIf(makeInt(2), makeReturn(makeInt(42)))),
            makeReturn(makeInt(0))
        })));
    SemanticAnalyzer sema;
    sema.analyze(*comp);
    IRGenerator irGen(sema.getSymbolTable());
    auto ir = irGen.generate(*comp);
    if (irContains(ir, "BEQZ") && irContains(ir, "RETURN #42")) {
        testPass();
    } else {
        testFail("nested if IR mismatch:\n" + irToString(ir));
    }
}

void testIR_multipleIfElse() {
    testHeader("IR: if-else-if chain");
    auto comp = makeCompUnit();
    comp->globalDecls.push_back(makeFuncDef(
        Type::INT, "main", {},
        makeBlock({
            makeIf(makeInt(0),
                makeReturn(makeInt(1)),
                makeIf(makeInt(1),
                    makeReturn(makeInt(2)),
                    makeReturn(makeInt(3)))),
            makeReturn(makeInt(0))
        })));
    SemanticAnalyzer sema;
    sema.analyze(*comp);
    IRGenerator irGen(sema.getSymbolTable());
    auto ir = irGen.generate(*comp);
    if (irContains(ir, "RETURN #1") && irContains(ir, "RETURN #2") &&
        irContains(ir, "RETURN #3")) {
        testPass();
    } else {
        testFail("multiple if-else IR mismatch:\n" + irToString(ir));
    }
}

void testIR_continueInWhile() {
    testHeader("IR: continue in while");
    auto comp = makeCompUnit();
    comp->globalDecls.push_back(makeFuncDef(
        Type::INT, "main", {},
        makeBlock({
            makeWhile(makeInt(1),
                makeBlock({
                    makeContinue(),
                    makeReturn(makeInt(0))
                })),
            makeReturn(makeInt(1))
        })));
    SemanticAnalyzer sema;
    sema.analyze(*comp);
    IRGenerator irGen(sema.getSymbolTable());
    auto ir = irGen.generate(*comp);
    if (irContains(ir, "BRANCH") && irContains(ir, "BEQZ")) {
        testPass();
    } else {
        testFail("continue IR mismatch:\n" + irToString(ir));
    }
}

void testIR_whileIf() {
    testHeader("IR: while containing if");
    auto comp = makeCompUnit();
    comp->globalDecls.push_back(makeFuncDef(
        Type::INT, "main", {},
        makeBlock({
            makeWhile(
                makeBinary(BinOp::LT, makeVar("i"), makeInt(10)),
                makeIf(makeInt(1), makeBreak())),
            makeReturn(makeInt(0))
        })));
    // Need to declare i first
    comp->globalDecls[0] = makeFuncDef(
        Type::INT, "main", {},
        makeBlock({
            makeDeclStmt(makeDecl(false, "i", makeInt(0))),
            makeWhile(
                makeBinary(BinOp::LT, makeVar("i"), makeInt(10)),
                makeIf(makeInt(1), makeBreak())),
            makeReturn(makeInt(0))
        }));
    SemanticAnalyzer sema;
    sema.analyze(*comp);
    IRGenerator irGen(sema.getSymbolTable());
    auto ir = irGen.generate(*comp);
    if (irContains(ir, "LT") && irContains(ir, "BEQZ")) {
        testPass();
    } else {
        testFail("while-if IR mismatch:\n" + irToString(ir));
    }
}

void testIR_nestedWhile() {
    testHeader("IR: nested while loops");
    auto comp = makeCompUnit();
    comp->globalDecls.push_back(makeFuncDef(
        Type::INT, "main", {},
        makeBlock({
            makeWhile(makeInt(1),
                makeWhile(makeInt(0), makeBreak())),
            makeReturn(makeInt(0))
        })));
    SemanticAnalyzer sema;
    sema.analyze(*comp);
    IRGenerator irGen(sema.getSymbolTable());
    auto ir = irGen.generate(*comp);
    // 两个 while 各需要 BEQZ
    auto irStr = irToString(ir);
    size_t first = irStr.find("BEQZ");
    if (first != std::string::npos && irStr.find("BEQZ", first + 1) != std::string::npos) {
        testPass();
    } else {
        testFail("nested while should have 2 BEQZ:\n" + irStr);
    }
}

void testSema_continueOutsideLoop() {
    testHeader("continue outside loop error");
    auto comp = makeCompUnit();
    comp->globalDecls.push_back(makeFuncDef(
        Type::INT, "main", {},
        makeBlock({ makeContinue() })));
    SemanticAnalyzer sema;
    bool ok = sema.analyze(*comp);
    if (!ok && !sema.getErrors().empty()) {
        testPass();
    } else {
        testFail("expected error");
    }
}

void testSema_breakInNestedLoop() {
    testHeader("break in nested while (valid)");
    auto comp = makeCompUnit();
    comp->globalDecls.push_back(makeFuncDef(
        Type::INT, "main", {},
        makeBlock({
            makeWhile(makeInt(1),
                makeWhile(makeInt(1), makeBreak())),
            makeReturn(makeInt(0))
        })));
    SemanticAnalyzer sema;
    if (sema.analyze(*comp)) {
        testPass();
    } else {
        testFail("unexpected error");
    }
}

// ============================================================
// 函数调用进阶测试
// ============================================================

void testIR_multiParams() {
    testHeader("IR: function with 3 params");
    auto comp = makeCompUnit();
    comp->globalDecls.push_back(makeFuncDef(
        Type::INT, "sum3",
        {makeParam(Type::INT, "a"), makeParam(Type::INT, "b"), makeParam(Type::INT, "c")},
        makeBlock({ makeReturn(
            makeBinary(BinOp::ADD,
                makeBinary(BinOp::ADD, makeVar("a"), makeVar("b")),
                makeVar("c"))) })));
    comp->globalDecls.push_back(makeFuncDef(
        Type::INT, "main", {},
        makeBlock({ makeReturn(
            makeCall("sum3", {makeInt(1), makeInt(2), makeInt(3)})) })));
    SemanticAnalyzer sema;
    sema.analyze(*comp);
    IRGenerator irGen(sema.getSymbolTable());
    auto ir = irGen.generate(*comp);
    auto irStr = irToString(ir);
    size_t first = irStr.find("PARAM");
    if (first != std::string::npos &&
        irStr.find("PARAM", first + 1) != std::string::npos &&
        irStr.find("PARAM", irStr.find("PARAM", first + 1) + 1) != std::string::npos) {
        testPass();
    } else {
        testFail("expected 3 PARAM instructions:\n" + irStr);
    }
}

void testIR_recursion() {
    testHeader("IR: recursive function");
    auto comp = makeCompUnit();
    comp->globalDecls.push_back(makeFuncDef(
        Type::INT, "fib", {makeParam(Type::INT, "n")},
        makeBlock({
            makeIf(
                makeBinary(BinOp::LT, makeVar("n"), makeInt(2)),
                makeReturn(makeVar("n"))),
            makeReturn(makeBinary(BinOp::ADD,
                makeCall("fib", {makeBinary(BinOp::SUB, makeVar("n"), makeInt(1))}),
                makeCall("fib", {makeBinary(BinOp::SUB, makeVar("n"), makeInt(2))})))
        })));
    SemanticAnalyzer sema;
    if (sema.analyze(*comp)) {
        testPass();
    } else {
        testFail("unexpected error in recursion");
    }
}

void testSema_funcParamCountMismatch() {
    testHeader("function call with wrong arg count");
    auto comp = makeCompUnit();
    comp->globalDecls.push_back(makeFuncDef(
        Type::INT, "add", {makeParam(Type::INT, "a"), makeParam(Type::INT, "b")},
        makeBlock({ makeReturn(
            makeBinary(BinOp::ADD, makeVar("a"), makeVar("b"))) })));
    comp->globalDecls.push_back(makeFuncDef(
        Type::INT, "main", {},
        makeBlock({ makeReturn(
            makeCall("add", {makeInt(1)})) })));  // 只传 1 个参数
    SemanticAnalyzer sema;
    bool ok = sema.analyze(*comp);
    if (!ok && !sema.getErrors().empty()) {
        testPass();
    } else {
        testFail("expected error for wrong arg count");
    }
}

void testSema_callUndefinedFunc() {
    testHeader("call undefined function");
    auto comp = makeCompUnit();
    comp->globalDecls.push_back(makeFuncDef(
        Type::INT, "main", {},
        makeBlock({ makeReturn(makeCall("noSuchFunc", {})) })));
    SemanticAnalyzer sema;
    bool ok = sema.analyze(*comp);
    if (!ok && !sema.getErrors().empty()) {
        testPass();
    } else {
        testFail("expected error for undefined function");
    }
}

// ============================================================
// 赋值与表达式语义测试
// ============================================================

void testSema_assignToNonLvalue() {
    testHeader("assign to non-lvalue (literal)");
    auto comp = makeCompUnit();
    comp->globalDecls.push_back(makeFuncDef(
        Type::INT, "main", {},
        makeBlock({
            makeExprStmt(makeBinary(BinOp::ASSIGN, makeInt(5), makeInt(10))),
            makeReturn(makeInt(0))
        })));
    SemanticAnalyzer sema;
    bool ok = sema.analyze(*comp);
    if (!ok && !sema.getErrors().empty()) {
        testPass();
    } else {
        testFail("expected error for assign to literal");
    }
}

void testIR_assignAndRead() {
    testHeader("IR: assign then read variable");
    auto comp = makeCompUnit();
    comp->globalDecls.push_back(makeFuncDef(
        Type::INT, "main", {},
        makeBlock({
            makeDeclStmt(makeDecl(false, "x", makeInt(0))),
            makeExprStmt(makeBinary(BinOp::ASSIGN, makeVar("x"), makeInt(42))),
            makeReturn(makeVar("x"))
        })));
    SemanticAnalyzer sema;
    sema.analyze(*comp);
    IRGenerator irGen(sema.getSymbolTable());
    auto ir = irGen.generate(*comp);
    if (irContains(ir, "ASSIGN %x, #42")) {
        testPass();
    } else {
        testFail("assign IR missing:\n" + irToString(ir));
    }
}

void testIR_multipleVars() {
    testHeader("IR: multiple local variables");
    auto comp = makeCompUnit();
    comp->globalDecls.push_back(makeFuncDef(
        Type::INT, "main", {},
        makeBlock({
            makeDeclStmt(makeDecl(false, "a", makeInt(1))),
            makeDeclStmt(makeDecl(false, "b", makeInt(2))),
            makeDeclStmt(makeDecl(false, "c", makeInt(3))),
            makeReturn(makeBinary(BinOp::ADD,
                makeBinary(BinOp::ADD, makeVar("a"), makeVar("b")),
                makeVar("c")))
        })));
    SemanticAnalyzer sema;
    sema.analyze(*comp);
    IRGenerator irGen(sema.getSymbolTable());
    auto ir = irGen.generate(*comp);
    if (irContains(ir, "LOCAL_VAR_DECL %a") &&
        irContains(ir, "LOCAL_VAR_DECL %b") &&
        irContains(ir, "LOCAL_VAR_DECL %c")) {
        testPass();
    } else {
        testFail("multiple vars IR mismatch:\n" + irToString(ir));
    }
}

// ============================================================
// 短路求值进阶测试
// ============================================================

void testIR_shortCircuitNested() {
    testHeader("IR: nested short-circuit (1||0) && (0||1)");
    auto comp = makeCompUnit();
    comp->globalDecls.push_back(makeFuncDef(
        Type::INT, "main", {},
        makeBlock({ makeReturn(
            makeBinary(BinOp::AND,
                makeBinary(BinOp::OR, makeInt(1), makeInt(0)),
                makeBinary(BinOp::OR, makeInt(0), makeInt(1)))) })));
    SemanticAnalyzer sema;
    sema.analyze(*comp);
    IRGenerator irGen(sema.getSymbolTable());
    auto ir = irGen.generate(*comp);
    auto irStr = irToString(ir);
    size_t first = irStr.find("BNEZ");
    if (first != std::string::npos &&
        irStr.find("BNEZ", first + 1) != std::string::npos &&
        irStr.find("BEQZ") != std::string::npos) {
        testPass();
    } else {
        testFail("nested short-circuit IR mismatch:\n" + irStr);
    }
}

void testIR_shortCircuitWithCompare() {
    testHeader("IR: short-circuit with compare (3>2) && (5<10)");
    auto comp = makeCompUnit();
    comp->globalDecls.push_back(makeFuncDef(
        Type::INT, "main", {},
        makeBlock({ makeReturn(
            makeBinary(BinOp::AND,
                makeBinary(BinOp::GT, makeInt(3), makeInt(2)),
                makeBinary(BinOp::LT, makeInt(5), makeInt(10)))) })));
    SemanticAnalyzer sema;
    sema.analyze(*comp);
    IRGenerator irGen(sema.getSymbolTable());
    auto ir = irGen.generate(*comp);
    if (irContains(ir, "GT") && irContains(ir, "LT") && irContains(ir, "BEQZ")) {
        testPass();
    } else {
        testFail("short-circuit with compare IR mismatch:\n" + irToString(ir));
    }
}

// ============================================================
// 全局变量测试
// ============================================================

void testIR_globalVar() {
    testHeader("IR: global variable declaration");
    auto comp = makeCompUnit();
    comp->globalDecls.push_back(makeDecl(false, "g_x", makeInt(100)));
    comp->globalDecls.push_back(makeFuncDef(
        Type::INT, "main", {},
        makeBlock({ makeReturn(makeVar("g_x")) })));
    SemanticAnalyzer sema;
    sema.analyze(*comp);
    IRGenerator irGen(sema.getSymbolTable());
    auto ir = irGen.generate(*comp);
    if (irContains(ir, "GLOBAL_VAR_DECL @g_x")) {
        testPass();
    } else {
        testFail("global var IR missing:\n" + irToString(ir));
    }
}

void testIR_globalConst() {
    testHeader("IR: global const variable");
    auto comp = makeCompUnit();
    comp->globalDecls.push_back(makeDecl(true, "MAX", makeInt(100)));
    comp->globalDecls.push_back(makeFuncDef(
        Type::INT, "main", {},
        makeBlock({ makeReturn(makeVar("MAX")) })));
    SemanticAnalyzer sema;
    sema.analyze(*comp);
    IRGenerator irGen(sema.getSymbolTable());
    auto ir = irGen.generate(*comp);
    // const 全局变量应被折叠为立即数
    if (irContains(ir, "RETURN #100")) {
        testPass();
    } else {
        testFail("global const should be folded:\n" + irToString(ir));
    }
}

// ============================================================
// 表达式语句测试
// ============================================================

void testIR_emptyExprStmt() {
    testHeader("IR: empty expression statement");
    auto comp = makeCompUnit();
    comp->globalDecls.push_back(makeFuncDef(
        Type::INT, "main", {},
        makeBlock({
            makeExprStmt(nullptr),  // 空语句 ;
            makeReturn(makeInt(0))
        })));
    SemanticAnalyzer sema;
    sema.analyze(*comp);
    IRGenerator irGen(sema.getSymbolTable());
    auto ir = irGen.generate(*comp);
    if (irContains(ir, "FUNC_BEGIN") && irContains(ir, "RETURN")) {
        testPass();
    } else {
        testFail("empty stmt IR mismatch:\n" + irToString(ir));
    }
}

void testIR_exprAsStmt() {
    testHeader("IR: expression as statement (a+b;)");
    auto comp = makeCompUnit();
    comp->globalDecls.push_back(makeFuncDef(
        Type::INT, "main", {},
        makeBlock({
            makeDeclStmt(makeDecl(false, "x", makeInt(1))),
            makeExprStmt(makeBinary(BinOp::ADD, makeVar("x"), makeInt(2))),
            makeReturn(makeInt(0))
        })));
    SemanticAnalyzer sema;
    sema.analyze(*comp);
    IRGenerator irGen(sema.getSymbolTable());
    auto ir = irGen.generate(*comp);
    if (irContains(ir, "ADD") && irContains(ir, "RETURN #0")) {
        testPass();
    } else {
        testFail("expr as stmt IR mismatch:\n" + irToString(ir));
    }
}

// ============================================================
// main 入口
// ============================================================

int main() {
    std::cout << "=== ToyC 中端测试 ===\n\n";

    std::cout << "--- 语义分析: 基础 ---\n";
    testSema_basicMain();
    testSema_voidReturnValue();
    testSema_nonVoidMustReturn();
    testSema_undefinedVar();
    testSema_duplicateVar();
    testSema_breakOutsideLoop();
    testSema_continueOutsideLoop();
    testSema_breakInNestedLoop();
    testSema_assignToConst();
    testSema_varNameShadowFunc();
    testSema_multiLevelScope();

    std::cout << "\n--- 语义分析: 常量折叠 ---\n";
    testSema_constFold();
    testSema_constFoldLogical();
    testSema_constFoldCompare();
    testSema_constFoldUnary();
    testSema_constFoldNot();
    testSema_constFoldDivMod();
    testSema_constFoldDivByZero();
    testSema_constFoldComplex();

    std::cout << "\n--- 语义分析: 一元表达式 ---\n";
    testSema_unaryNeg();
    testSema_unaryNot();
    testSema_unaryPos();

    std::cout << "\n--- 语义分析: 函数调用 ---\n";
    testSema_funcParamCountMismatch();
    testSema_callUndefinedFunc();

    std::cout << "\n--- 语义分析: 赋值 ---\n";
    testSema_assignToNonLvalue();

    std::cout << "\n--- IR 生成: 一元表达式 ---\n";
    testIR_unaryNeg();
    testIR_unaryNot();

    std::cout << "\n--- IR 生成: 比较运算符 ---\n";
    testIR_compareLT();
    testIR_compareGT();
    testIR_compareLE();
    testIR_compareGE();
    testIR_compareEQ();
    testIR_compareNE();

    std::cout << "\n--- IR 生成: 基础 ---\n";
    testIR_simpleReturn();
    testIR_arithmetic();
    testIR_emptyExprStmt();
    testIR_exprAsStmt();

    std::cout << "\n--- IR 生成: 变量与作用域 ---\n";
    testIR_scopeVariable();
    testIR_multipleVars();
    testIR_assignAndRead();
    testIR_varShadowing();
    testIR_nestedScope();
    testIR_globalVar();
    testIR_globalConst();

    std::cout << "\n--- IR 生成: 短路求值 ---\n";
    testIR_shortCircuitAnd();
    testIR_shortCircuitOr();
    testIR_shortCircuitNested();
    testIR_shortCircuitWithCompare();

    std::cout << "\n--- IR 生成: 控制流 ---\n";
    testIR_ifStatement();
    testIR_ifElse();
    testIR_nestedIf();
    testIR_multipleIfElse();
    testIR_whileLoop();
    testIR_continueInWhile();
    testIR_whileIf();
    testIR_nestedWhile();

    std::cout << "\n--- IR 生成: 函数调用 ---\n";
    testIR_functionCall();
    testIR_multiParams();
    testIR_recursion();
    testIR_voidFunction();

    std::cout << "\n=== 结果: " << g_passCount << "/" << g_testCount
              << " 通过 ===\n";

    return (g_passCount == g_testCount) ? 0 : 1;
}