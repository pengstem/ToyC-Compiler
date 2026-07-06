#ifndef TOYCC_SYMBOL_TABLE_H
#define TOYCC_SYMBOL_TABLE_H

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include "ast_nodes.h"

namespace toycc {

// ============================================================
// 符号信息
// ============================================================

struct Symbol {
    std::string name;
    Type type = Type::INT;   // INT 或 VOID（仅函数返回类型）
    bool isConst = false;
    bool isFunction = false;
    bool isGlobal = false;
    bool isParam = false;
    int constValue = 0;       // 若 isConst 且已折叠，存储编译期求值结果
    std::vector<Type> paramTypes;  // 仅函数有效

    // 辅助判断
    bool isInt() const { return type == Type::INT; }
    bool isVoid() const { return type == Type::VOID; }
};

// ============================================================
// 作用域
// ============================================================

class Scope {
public:
    bool addSymbol(const Symbol& sym) {
        // 同作用域内不允许重名
        auto it = symbols.find(sym.name);
        if (it != symbols.end()) {
            return false;
        }
        symbols[sym.name] = sym;
        return true;
    }

    Symbol* lookup(const std::string& name) {
        auto it = symbols.find(name);
        if (it != symbols.end()) {
            return &it->second;
        }
        return nullptr;
    }

    const std::unordered_map<std::string, Symbol>& getSymbols() const {
        return symbols;
    }

private:
    std::unordered_map<std::string, Symbol> symbols;
};

// ============================================================
// 符号表
// ============================================================

class SymbolTable {
public:
    SymbolTable() {
        // 创建全局作用域
        enterScope();
    }

    // 进入新作用域（如进入 Block）
    void enterScope() {
        scopes.push_back(std::make_unique<Scope>());
    }

    // 退出当前作用域
    void exitScope() {
        if (scopes.size() > 1) {
            scopes.pop_back();
        }
    }

    // 在当前作用域添加符号
    // 返回 false 表示当前作用域内已存在同名符号
    bool addSymbol(const Symbol& sym) {
        return scopes.back()->addSymbol(sym);
    }

    // 从内层向外层查找符号（支持作用域嵌套与变量屏蔽）
    Symbol* lookup(const std::string& name) {
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
            Symbol* sym = (*it)->lookup(name);
            if (sym) {
                return sym;
            }
        }
        return nullptr;
    }

    // 仅在当前作用域查找（不向上查找）
    Symbol* lookupCurrent(const std::string& name) {
        return scopes.back()->lookup(name);
    }

    // 在当前作用域（全局作用域）查找函数
    Symbol* lookupFunction(const std::string& name) {
        // 函数只能在全局作用域定义
        if (!scopes.empty()) {
            Symbol* sym = scopes.front()->lookup(name);
            if (sym && sym->isFunction) {
                return sym;
            }
        }
        return nullptr;
    }

    // 获取当前作用域层级
    int scopeLevel() const {
        return static_cast<int>(scopes.size());
    }

    // 是否在全局作用域
    bool isGlobalScope() const {
        return scopes.size() == 1;
    }

    // 获取所有全局符号
    const std::unordered_map<std::string, Symbol>& getGlobalSymbols() const {
        return scopes.front()->getSymbols();
    }

private:
    std::vector<std::unique_ptr<Scope>> scopes;
};

}  // namespace toycc

#endif  // TOYCC_SYMBOL_TABLE_H