#include "ast_nodes.h"
#include "ir_generator.h"
#include "semantic_analyzer.h"

#include <iostream>
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

/// 从 stdin 读取全部内容
std::string readStdin() {
  std::string content;
  std::string line;
  while (std::getline(std::cin, line)) {
    content += line + "\n";
  }
  return content;
}

bool hasStdinInput() {
  // 检查 stdin 是否有数据（非管道/重定向时可能阻塞，仅用于 CI 环境）
  return !std::cin.eof() && std::cin.peek() != std::char_traits<char>::eof();
}

} // anonymous namespace

int main(int argc, char* argv[]) {
  // 无参数时：检查是否有 stdin 输入（CI 集成测试通过管道传入源码）
  if (argc < 2) {
    if (hasStdinInput()) {
      // CI 集成测试模式：读取源码，但前端尚未实现
      std::string source = readStdin();
      std::cerr << "Front-end not yet integrated. "
                << "Received " << source.size() << " bytes from stdin.\n";
      return 1;
    }
    // 冒烟测试（CI 使用）
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

  // TODO: 前端完成后，从 argv[1] 读取源文件进行解析
  std::cerr << "Front-end not yet integrated. AST construction is required.\n";
  return 1;
}