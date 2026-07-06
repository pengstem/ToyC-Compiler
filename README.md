# ToyC 编译器

这是一个使用 C++20 编写的 ToyC 编译器项目。按照课程要求，编译器需要从标准输入 `stdin` 读取 ToyC 源代码，并向标准输出 `stdout` 输出 RISC-V32 汇编代码。

## 环境要求

基础构建依赖：

- CMake 3.20 或更新版本
- 支持 C++20 的 C++ 编译器
- 使用当前 `Unix Makefiles` presets 时需要 Make
- clang-format，可选但建议安装

导入测试依赖：

- Python 3
- 宿主机 C 编译器，例如 `clang`、`gcc` 或 `cc`
- RISC-V GCC driver，例如 `riscv64-linux-gnu-gcc`
- `qemu-riscv32`

导入测试 runner 会使用 `-nostdlib` 链接生成的 RISC-V32 汇编，并在需要时自动补一个很小的 `_start` 启动壳，所以不依赖 RV32 libc。

## 快速开始

配置并构建 debug 版本：

```sh
cmake --preset debug
cmake --build --preset debug
```

手动运行编译器：

```sh
build/debug/toycc < tests/testcases/lv1/0_main.c > /tmp/0_main.s
```

查看已注册测试：

```sh
ctest --preset debug -N
```

运行测试：

```sh
ctest --preset debug
```

当前 `src/main.cpp` 仍然只是骨架。只要编译器还没有输出有效的 RISC-V `main` 函数，导入测试就会在 RISC-V 链接阶段失败，常见错误是 `undefined reference to main`。

## 项目结构

```text
.
|-- CMakeLists.txt
|-- CMakePresets.json
|-- docs/
|-- src/
|   `-- main.cpp
`-- tests/
    |-- run_toyc_case.py
    |-- toyc-strict.cases
    `-- testcases/
```

重要文件：

- `src/`：编译器实现代码。
- `docs/`：课程任务说明和 ToyC 语言定义。
- `tests/testcases/`：从上游 SysY 测试仓库导入的源文件。
- `tests/toyc-strict.cases`：默认启用的严格 ToyC 兼容测试清单。
- `tests/run_toyc_case.py`：CTest 用来运行单个测试用例的脚本。

## 构建 Preset

当前提供这些 configure/build presets：

- `debug`：普通开发构建。
- `release`：优化构建，默认开启 LTO。
- `relwithdebinfo`：带调试信息的优化构建。
- `sanitize`：开启 AddressSanitizer 和 UBSan 的 debug 构建。
- `strict`：开启 warnings as errors 的 debug 构建。

示例：

```sh
cmake --preset strict
cmake --build --preset strict

cmake --preset sanitize
cmake --build --preset sanitize
```

## 测试流程

CTest 会注册一个 smoke test，以及 `tests/toyc-strict.cases` 中每个用例对应的导入测试。

每个导入测试的执行流程如下：

```text
宿主机 C 编译器编译 case.c，得到期望 stdout 和退出码
toycc < case.c > generated.s
RISC-V GCC 将 generated.s 链接为 actual.elf
qemu-riscv32 运行 actual.elf
runner 对比 stdout 和退出码
```

如果后续编译器支持了某个当前排除的特性，可以把对应源码路径加入 `tests/toyc-strict.cases`，或者为该特性新增单独的 manifest 和 CTest label。

## 格式化

格式化 C++ 源码：

```sh
cmake --build --preset debug --target format
```

检查格式：

```sh
cmake --build --preset debug --target check-format
```

## 常用 CMake 选项

这些选项可以在 configure 时通过 `-D` 传入：

- `TOYCC_BUILD_TESTS=ON|OFF`
- `TOYCC_ENABLE_IMPORTED_TESTS=ON|OFF`
- `TOYCC_WARNINGS_AS_ERRORS=ON|OFF`
- `TOYCC_ENABLE_SANITIZERS=ON|OFF`
- `TOYCC_TEST_CASE_LIST=/path/to/cases`
- `TOYCC_TEST_ROOT=/path/to/testcases`
- `TOYCC_TEST_TIMEOUT=5`
- `TOYCC_RISCV_ARCH=rv32im`
- `TOYCC_RISCV_ABI=ilp32`
- `TOYCC_TEST_STARTUP=auto|on|off`

示例：

```sh
cmake -S . -B build/no-imported-tests \
  -DTOYCC_BUILD_TESTS=ON \
  -DTOYCC_ENABLE_IMPORTED_TESTS=OFF
```
