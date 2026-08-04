#include "ast_nodes.h"
#include "code_generator.h"
#include "ir_generator.h"
#include "parser_driver.h"
#include "semantic_analyzer.h"

#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

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

[[maybe_unused]] void printIR(const std::vector<IRInst>& ir) {
  for (const auto& inst : ir) {
    std::cout << inst << "\n";
  }
}

std::string readAll(std::istream& input) {
  std::string content;
  std::string line;
  while (std::getline(input, line)) {
    content += line + "\n";
  }
  return content;
}

bool hasStdinInput() {
  return !std::cin.eof() && std::cin.peek() != std::char_traits<char>::eof();
}

int compileSource(const std::string& source, std::ostream& out, bool emitIR) {
  ParserDriver driver;
  if (!driver.parse(source)) {
    std::cerr << "Parse error: " << driver.getError() << "\n";
    return 1;
  }

  auto ast = driver.getAST();
  if (!ast) {
    std::cerr << "No AST produced.\n";
    return 1;
  }

  SemanticAnalyzer sema;
  if (!sema.analyze(*ast)) {
    std::cerr << "Semantic analysis failed:\n";
    for (const auto& err : sema.getErrors()) {
      std::cerr << "  line " << err.line << ":" << err.col << " " << err.message << "\n";
    }
    return 1;
  }

  IRGenerator irGen(sema.getSymbolTable());
  auto ir = irGen.generate(*ast);

  if (emitIR) {
    printIR(ir);
    return 0;
  }

  CodeGenerator codeGen;
  codeGen.generate(ir, out);
  return 0;
}

} // anonymous namespace

int main(int argc, char* argv[]) {
  if (argc < 2) {
    if (hasStdinInput()) {
      std::string source = readAll(std::cin);
      return compileSource(source, std::cout, false);
    }
    return 0;
  }

  std::string_view arg = argv[1];
  if (arg == "--help" || arg == "-h") {
    std::cerr << "Usage: " << argv[0] << " [options] <input.c>\n"
              << "Options:\n"
              << "  --emit-ir      输出 IR（三地址码）\n"
              << "  --help         显示帮助信息\n";
    return 0;
  }

  bool emitIR = false;
  std::string inputPath;
  for (int i = 1; i < argc; ++i) {
    std::string_view option = argv[i];
    if (option == "--emit-ir") {
      emitIR = true;
    } else if (!option.starts_with("-")) {
      inputPath = argv[i];
    }
  }

  if (inputPath.empty()) {
    std::cerr << "No input file provided.\n";
    return 1;
  }

  std::ifstream input(inputPath);
  if (!input.is_open()) {
    std::cerr << "Failed to read input file: " << inputPath << "\n";
    return 1;
  }

  std::string source = readAll(input);
  return compileSource(source, std::cout, emitIR);
}
