# 中端 API 对接文档

## 编译管线

```
源码 (.c) → [前端] → AST → [中端] → IR → [后端] → RISC-V 汇编
```

## 1. 前端 → 中端：构建 AST

### 1.1 头文件依赖

前端需要 `#include "ast_nodes.h"`，所有 AST 节点类定义在此文件中。

### 1.2 构建根节点

```cpp
#include "ast_nodes.h"

auto compUnit = std::make_shared<toycc::CompUnit>();

// 按声明顺序添加全局定义
compUnit->globalDecls.push_back(/* 全局变量或函数定义 */);
```

### 1.3 构建函数定义

```cpp
auto func = std::make_shared<toycc::FuncDef>();
func->retType = toycc::Type::INT;  // 或 Type::VOID
func->name = "main";

// 参数列表
auto param = std::make_shared<toycc::FuncParam>();
param->type = toycc::Type::INT;
param->name = "argc";
func->params.push_back(param);

// 函数体
auto body = std::make_shared<toycc::Block>();
body->stmts.push_back(/* 语句列表 */);
func->body = body;
```

### 1.4 构建语句

| 语句类型 | 类 | 关键字段 |
|----------|-----|---------|
| 复合语句 | `Block` | `stmts: vector<shared_ptr<Stmt>>` |
| 表达式语句 | `ExprStmt` | `expr: shared_ptr<Expr>` |
| 声明语句 | `DeclStmt` | `decl: shared_ptr<Decl>` |
| 返回语句 | `ReturnStmt` | `value: shared_ptr<Expr>` (void 时为 null) |
| if 语句 | `IfStmt` | `condition`, `thenBranch`, `elseBranch` (可为 null) |
| while 语句 | `WhileStmt` | `condition`, `body` |
| break 语句 | `BreakStmt` | (无字段) |
| continue 语句 | `ContinueStmt` | (无字段) |

### 1.5 构建表达式

| 表达式类型 | 类 | 关键字段 |
|-----------|-----|---------|
| 整数字面量 | `IntLiteral` | `value: int` |
| 变量引用 | `VarExpr` | `name: string` |
| 二元运算 | `BinaryExpr` | `op: BinOp`, `lhs`, `rhs` |
| 一元运算 | `UnaryExpr` | `op: UnaryOp`, `operand` |
| 函数调用 | `CallExpr` | `funcName: string`, `args` |

### 1.6 运算符枚举

```cpp
enum class BinOp {
    ADD, SUB, MUL, DIV, MOD,    // 算术
    LT, GT, LE, GE, EQ, NE,     // 关系
    AND, OR,                     // 逻辑
    ASSIGN                       // 赋值
};

enum class UnaryOp {
    POS, NEG, NOT
};
```

### 1.7 构建变量声明

```cpp
auto decl = std::make_shared<toycc::Decl>();
decl->isConst = false;         // const 变量设为 true
decl->varType = toycc::Type::INT;
decl->name = "x";
decl->initExpr = std::make_shared<toycc::IntLiteral>(42);  // 可为 null
```

---

## 2. 调用中端

```cpp
#include "semantic_analyzer.h"
#include "ir_generator.h"

// 步骤 1：语义分析
toycc::SemanticAnalyzer sema;
if (!sema.analyze(*compUnit)) {
    // 输出语义错误
    for (auto& e : sema.getErrors()) {
        std::cerr << "line " << e.line << ":" << e.col
                  << " error: " << e.message << "\n";
    }
    return 1;
}

// 步骤 2：生成 IR
toycc::IRGenerator irGen(sema.getSymbolTable());
auto ir = irGen.generate(*compUnit);
```

---

## 3. 中端 → 后端：消费 IR

### 3.1 IR 指令结构

```cpp
struct IRInst {
    IROp op;       // 操作码
    Operand dest;  // 目标操作数
    Operand src1;  // 源操作数 1
    Operand src2;  // 源操作数 2
};
```

### 3.2 操作数类型

| 类型 | 前缀 | 示例 | 含义 |
|------|------|------|------|
| `IMM` | `#` | `#42` | 立即数 |
| `LOCAL_VAR` | `%` | `%x` | 局部变量（分配在栈上） |
| `GLOBAL_VAR` | `@` | `@g` | 全局变量 |
| `LABEL` | `:` | `L1:` | 跳转标签 |
| `FUNC` | (无) | `main` | 函数名 |
| `PARAM` | `$` | `$n` | 函数参数引用 |
| `NONE` | `_` | (无) | 无操作数 |

### 3.3 指令语义

#### 算术/逻辑/关系

```
dest = src1 op src2

ADD  %t0, %a, #1     →  %t0 = a + 1
SUB  %t0, #0, %x     →  %t0 = 0 - x
MUL  %t0, %a, %b     →  %t0 = a * b
DIV  %t0, %a, %b     →  %t0 = a / b
MOD  %t0, %a, %b     →  %t0 = a % b
NOT  %t0, %x         →  %t0 = !x (0 变 1, 非 0 变 0)
LT   %t0, %a, %b     →  %t0 = a < b  ? 1 : 0
GT   %t0, %a, %b     →  %t0 = a > b  ? 1 : 0
LE   %t0, %a, %b     →  %t0 = a <= b ? 1 : 0
GE   %t0, %a, %b     →  %t0 = a >= b ? 1 : 0
EQ   %t0, %a, %b     →  %t0 = a == b ? 1 : 0
NE   %t0, %a, %b     →  %t0 = a != b ? 1 : 0
```

#### 数据移动

```
ASSIGN  %x, #42      →  x = 42
ASSIGN  %x, %y       →  x = y
```

#### 控制流

```
LABEL  L1:           →  定义标签 L1
BRANCH L1:           →  无条件跳转到 L1
BEQZ   L1:, %x       →  if x == 0 goto L1
BNEZ   L1:, %x       →  if x != 0 goto L1
```

#### 函数

```
FUNC_BEGIN  main     →  函数定义开始
LOCAL_VAR_DECL %x, $n  → 声明局部变量 x，初始值为参数 n
LOCAL_VAR_DECL %x, #42 → 声明局部变量 x，初始值为 42
LOCAL_VAR_DECL %x      → 声明局部变量 x（无初始值）
GLOBAL_VAR_DECL @g, #0 → 声明全局变量 g，初始值为 0
PARAM  #42, #0       →  传递第 0 个参数，值为 42
CALL   %t0, add      →  %t0 = add()
CALL   _, print      →  print() (void 函数)
RETURN %x            →  return x
RETURN _             →  return (void 函数)
FUNC_END  main       →  函数定义结束
```

### 3.4 短路求值示例

源码 `a && b` 生成的 IR：

```
    %t0 = a          (或子表达式的结果)
    BEQZ L_false:, %t0
    %t1 = b
    BEQZ L_false:, %t1
    ASSIGN %result, #1
    BRANCH L_end:
L_false:
    ASSIGN %result, #0
L_end:
```

源码 `a || b` 生成的 IR：

```
    %t0 = a
    BNEZ L_true:, %t0
    %t1 = b
    BNEZ L_true:, %t1
    ASSIGN %result, #0
    BRANCH L_end:
L_true:
    ASSIGN %result, #1
L_end:
```

### 3.5 后端遍历 IR

```cpp
#include "ir.h"

void processIR(const std::vector<toycc::IRInst>& ir) {
    for (auto& inst : ir) {
        switch (inst.op) {
            case toycc::IROp::ADD:
                // 生成 RISC-V add 指令
                break;
            case toycc::IROp::BEQZ:
                // 生成 RISC-V beqz 指令
                break;
            // ... 其他指令
        }
    }
}
```

---

## 4. 语义分析检查项

中端会报告以下语义错误：

| 检查项 | 错误信息 |
|--------|---------|
| 函数重复定义 | `function 'X' already defined` |
| 变量重复声明 | `variable 'X' already declared in this scope` |
| 未定义变量 | `undefined variable 'X'` |
| 未定义函数 | `undefined function 'X'` |
| void 函数返回值 | `void function 'X' should not return a value` |
| 非 void 函数无返回值 | `non-void function 'X' must return a value` |
| 循环外 break | `'break' statement not in loop` |
| 循环外 continue | `'continue' statement not in loop` |
| 参数数量不匹配 | `function 'X' expects N arguments, but M were given` |
| const 赋值 | `cannot assign to const variable 'X'` |
| 非左值赋值 | `assignment target must be a variable` |
| const 初始化非编译期 | `const variable 'X' must be initialized with a constant expression` |