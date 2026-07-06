#include <iostream>
#include <string_view>

#include "ast_nodes.h"
#include "semantic_analyzer.h"
#include "ir_generator.h"

using namespace toycc;

// ============================================================
// ToyC Compiler — 主入口
//
// 编译管线:
//   源码 → [前端: flex+bison] → AST → [中端: 语义分析] → IR → [后端: 代码生成] → RISC-V 汇编
//
// 当前阶段: 中端（语义分析 + IR 生成）已就绪，前后端开发中。
// ============================================================

namespace {

void printUsage(std::string_view prog) {
    std::cerr << "Usage: " << prog << " [options] <input.c>\n"
              << "Options:\n"
              << "  --emit-ir      输出 IR（三地址码）\n"
              << "  --help         显示帮助信息\n";
}

[[maybe_unused]] void printIR(const std::vector<IRInst>& ir) {
    for (const auto& inst : ir) {
        std::cout << inst << "\n";
    }
}

}  // anonymous namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        // 无参数时运行冒烟测试（CI 使用）
        std::cout << "ToyC Compiler v0.0.1 — Middle-end ready.\n";
        std::cout << "Run with --help for usage.\n";
        return 0;
    }

    std::string_view arg = argv[1];
    if (arg == "--help" || arg == "-h") {
        printUsage(argv[0]);
        return 0;
    }

    // TODO: 前端完成后，从 argv[1] 读取源文件进行解析
    // CompUnit ast = parse(inputFile);
    //
    // SemanticAnalyzer sema;
    // if (!sema.analyze(ast)) {
    //     for (auto& err : sema.getErrors()) {
    //         std::cerr << "Error at line " << err.line << ": " << err.message << "\n";
    //     }
    //     return 1;
    // }
    //
    // std::vector<IRInst> ir = IRGenerator(sema.getSymbolTable()).generate(ast);
    // printIR(ir);

    std::cerr << "Front-end not yet integrated. AST construction is required.\n";
    return 1;
}