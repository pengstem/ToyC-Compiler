#ifndef TOYCC_SEMANTIC_ANALYZER_H
#define TOYCC_SEMANTIC_ANALYZER_H

#include <string>
#include <vector>
#include "ast_nodes.h"
#include "symbol_table.h"

namespace toycc {

// ============================================================
// 语义错误信息
// ============================================================

struct SemanticError {
    int line;
    int col;
    std::string message;
};

// ============================================================
// 语义分析器
// ============================================================

class SemanticAnalyzer {
public:
    SemanticAnalyzer();

    // 执行语义分析，返回 true 表示无错误
    bool analyze(CompUnit& compUnit);

    // 获取错误列表
    const std::vector<SemanticError>& getErrors() const { return errors; }

    // 获取符号表（供 IR 生成器使用）
    SymbolTable& getSymbolTable() { return symTable; }
    const SymbolTable& getSymbolTable() const { return symTable; }

    // 常量折叠：尝试对表达式进行编译期求值（public 供测试使用）
    bool tryConstFold(Expr* expr, int& result);

private:
    SymbolTable symTable;
    std::vector<SemanticError> errors;

    // 当前分析的函数信息（用于 return 检查等）
    Type currentFuncRetType = Type::INT;
    std::string currentFuncName;
    int whileDepth = 0;  // 用于 break/continue 检查

    // 错误报告
    void error(int line, int col, const std::string& msg);

    // 第一遍：收集全局声明（函数签名等）
    void collectGlobals(CompUnit& compUnit);

    // 分析全局声明
    void analyzeGlobalDecl(ASTNode* node);

    // 分析函数定义
    void analyzeFuncDef(FuncDef* funcDef);

    // 分析语句
    void analyzeStmt(Stmt* stmt);

    // 分析声明
    void analyzeDecl(Decl* decl);

    // 分析表达式，返回类型
    Type analyzeExpr(Expr* expr);

    // 检查表达式是否可在编译期求值
    bool isConstExpr(Expr* expr);
};

}  // namespace toycc

#endif  // TOYCC_SEMANTIC_ANALYZER_H