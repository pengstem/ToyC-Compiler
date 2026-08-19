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

int compileSource(const std::string& source, std::ostream& out, bool emitIR, IRStage irStage) {
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
  auto ir = irGen.generate(*ast, irStage);

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
  bool emitIR = false;
  IRStage irStage = IRStage::OPTIMIZED;
  std::string inputPath;

  for (int i = 1; i < argc; ++i) {
    std::string_view option = argv[i];
    if (option == "-h" || option == "--help") {
      std::cerr << "Usage: " << argv[0] << " [options] <input.c>\n"
                << "Options:\n"
                << "  --emit-ir      输出 IR（三地址码）\n"
                << "  --emit-ir-inline 输出仅内联后的 IR\n"
                << "  --emit-ir-raw  输出优化前 IR\n"
                << "  -opt           优化选项（当前接受但暂未实现）\n"
                << "  --help         显示帮助信息\n"
                << "源码可通过命令行文件参数或标准输入提供。\n";
      return 0;
    }
    if (option == "--emit-ir") {
      emitIR = true;
    } else if (option == "--emit-ir-inline") {
      emitIR = true;
      irStage = IRStage::INLINED;
    } else if (option == "--emit-ir-raw") {
      emitIR = true;
      irStage = IRStage::RAW;
    } else if (option == "-opt") {
      // 性能测试会传 -opt，当前接受但暂未实现优化
    } else if (!option.starts_with("-")) {
      inputPath = argv[i];
    }
    // 其他以 '-' 开头的未知选项静默忽略，保持向前兼容
  }

  // 优先使用命令行指定的输入文件
  if (!inputPath.empty()) {
    std::ifstream input(inputPath);
    if (!input.is_open()) {
      std::cerr << "Failed to read input file: " << inputPath << "\n";
      return 1;
    }
    std::string source = readAll(input);
    return compileSource(source, std::cout, emitIR, irStage);
  }

  // 无文件参数时回退到 stdin（支持 `toycc [options] < input.c` 调用方式）
  if (hasStdinInput()) {
    std::string source = readAll(std::cin);
    return compileSource(source, std::cout, emitIR, irStage);
  }

  // 既无文件参数也无 stdin 输入（如裸跑 toycc 做 smoke test）：静默退出
  return 0;
}
