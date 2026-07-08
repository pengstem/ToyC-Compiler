cmake_minimum_required(VERSION 3.20)

project(
  toycc
  VERSION 0.0.1
  DESCRIPTION "A ToyC to RISC-V32 compiler"
  LANGUAGES CXX
)

# ================================
# Basic settings
# ================================

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Required by clangd.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set_property(
  CACHE CMAKE_BUILD_TYPE
  PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel
)

# ================================
# Project options
# ================================

option(TOYCC_ENABLE_WARNINGS "Enable compiler warnings" ON)
option(TOYCC_WARNINGS_AS_ERRORS "Treat warnings as errors" ON)
option(TOYCC_ENABLE_SANITIZERS "Enable AddressSanitizer and UBSan" OFF)
option(TOYCC_ENABLE_LTO "Enable link-time optimization" OFF)
# Auto-enable LTO for Release builds
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  set(TOYCC_ENABLE_LTO ON)
endif()
option(TOYCC_ENABLE_NATIVE_ARCH "Enable -march=native" ON)
option(TOYCC_BUILD_TESTS "Build tests" ON)
option(TOYCC_ENABLE_IMPORTED_TESTS "Enable imported ToyC-compatible test cases" ON)
option(TOYCC_ENABLE_FORMAT_TARGETS "Enable clang-format targets" ON)

set(TOYCC_TEST_ROOT
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/testcases"
  CACHE PATH
  "Directory containing imported test cases"
)

set(TOYCC_TEST_CASE_LIST
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/toyc-strict.cases"
  CACHE FILEPATH
  "Manifest of ToyC-compatible imported test cases"
)

set(TOYCC_TEST_TIMEOUT
  "5"
  CACHE STRING
  "Per-step timeout in seconds for each ToyC test case"
)

set(TOYCC_RISCV_ARCH
  "rv32im"
  CACHE STRING
  "RISC-V ISA used when linking generated assembly in tests"
)

set(TOYCC_RISCV_ABI
  "ilp32"
  CACHE STRING
  "RISC-V ABI used when linking generated assembly in tests"
)

set(TOYCC_TEST_STARTUP
  "auto"
  CACHE STRING
  "How tests provide a RISC-V _start wrapper: auto, on, or off"
)

set_property(
  CACHE TOYCC_TEST_STARTUP
  PROPERTY STRINGS auto on off
)

set(TOYCC_HOST_CC "" CACHE FILEPATH "Host C compiler used to build reference outputs")
set(TOYCC_RISCV_CC "" CACHE FILEPATH "RISC-V GCC driver used to link generated assembly")
set(TOYCC_QEMU_RISCV32 "" CACHE FILEPATH "qemu-riscv32 executable used to run tests")

# ================================
# Sources
# ================================

file(GLOB_RECURSE TOYCC_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

file(GLOB_RECURSE TOYCC_HEADERS CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
)

add_executable(toycc
  ${TOYCC_SOURCES}
  ${TOYCC_HEADERS}
)

target_include_directories(toycc
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

# ================================
# Compiler warnings
# ================================

if(TOYCC_ENABLE_WARNINGS)
  if(MSVC)
    target_compile_options(toycc PRIVATE /W4)
  else()
    target_compile_options(toycc PRIVATE
      -Wall
      -Wextra
      -Wpedantic
      -Wconversion
      -Wshadow
      -Wnon-virtual-dtor
      -Wold-style-cast
      -Woverloaded-virtual
      -Wnull-dereference
      -Wdouble-promotion
      -Wformat=2
    )
  endif()
endif()

if(TOYCC_WARNINGS_AS_ERRORS)
  if(MSVC)
    target_compile_options(toycc PRIVATE /WX)
  else()
    target_compile_options(toycc PRIVATE -Werror)
  endif()
endif()

# ================================
<<<<<<< HEAD
# MSVC UTF-8 encoding
# ================================

if(MSVC)
  target_compile_options(toycc PRIVATE /utf-8)
endif()

# ================================
=======
>>>>>>> f729edc9519e2598f7e7a22384d045f99f08b39a
# Build-type flags
# ================================

if(NOT MSVC)
  target_compile_options(toycc PRIVATE
    $<$<CONFIG:Debug>:-O0 -g3 -fno-omit-frame-pointer>
    $<$<CONFIG:Release>:-O3 -DNDEBUG>
    $<$<CONFIG:RelWithDebInfo>:-O2 -g -DNDEBUG>
    $<$<CONFIG:MinSizeRel>:-Os -DNDEBUG>
  )
endif()

# ================================
# Sanitizers
# ================================

if(TOYCC_ENABLE_SANITIZERS)
  if(MSVC)
    message(WARNING "Sanitizers are not configured for MSVC.")
  else()
    target_compile_options(toycc PRIVATE
      -fsanitize=address,undefined
      -fno-omit-frame-pointer
    )
    target_link_options(toycc PRIVATE
      -fsanitize=address,undefined)
  endif()
endif()

# ================================
# LTO
# ================================

if(TOYCC_ENABLE_LTO)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT TOYCC_LTO_SUPPORTED OUTPUT TOYCC_LTO_ERROR)

  if(TOYCC_LTO_SUPPORTED)
    set_property(TARGET toycc PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
  else()
    message(WARNING "LTO is not supported: ${TOYCC_LTO_ERROR}")
  endif()
endif()

# ================================
# Native arch
# ================================

if(TOYCC_ENABLE_NATIVE_ARCH)
  if(NOT MSVC)
    target_compile_options(toycc PRIVATE -march=native)
  endif()
endif()

# ================================
# clang-format
# ================================

if(TOYCC_ENABLE_FORMAT_TARGETS)
  find_program(CLANG_FORMAT_EXE NAMES clang-format)

  if(CLANG_FORMAT_EXE)
    add_custom_target(format
      COMMAND ${CLANG_FORMAT_EXE} -i ${TOYCC_SOURCES} ${TOYCC_HEADERS}
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      COMMENT "Formatting source files with clang-format"
    )

    add_custom_target(check-format
      COMMAND ${CLANG_FORMAT_EXE} --dry-run --Werror ${TOYCC_SOURCES} ${TOYCC_HEADERS}
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      COMMENT "Checking source formatting with clang-format"
    )
  else()
    message(WARNING "clang-format not found. format/check-format targets disabled.")
  endif()
endif()

# ================================
# Testing
# ================================

if(TOYCC_BUILD_TESTS)
  enable_testing()

  add_test(
    NAME toycc-smoke-test
    COMMAND $<TARGET_FILE:toycc>
  )

  # 中端单元测试
  add_executable(toycc-test-middle-end
    tests/unit/test_middle_end.cpp
    src/semantic_analyzer.cpp
    src/ir_generator.cpp
<<<<<<< HEAD
    src/code_generator.cpp
=======
>>>>>>> f729edc9519e2598f7e7a22384d045f99f08b39a
  )
  target_include_directories(toycc-test-middle-end
    PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}/src"
  )
  target_compile_options(toycc-test-middle-end PRIVATE
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:
      -Wall -Wextra -Wpedantic
      -Wconversion -Wshadow -Wnon-virtual-dtor
      -Wold-style-cast -Woverloaded-virtual
      -Wnull-dereference -Wdouble-promotion -Wformat=2
    >
  )
  if(TOYCC_WARNINGS_AS_ERRORS)
    target_compile_options(toycc-test-middle-end PRIVATE
      $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Werror>
    )
  endif()
  if(NOT MSVC)
    target_compile_options(toycc-test-middle-end PRIVATE
      -O0 -g3 -fno-omit-frame-pointer
    )
  endif()
  if(TOYCC_ENABLE_NATIVE_ARCH AND NOT MSVC)
    target_compile_options(toycc-test-middle-end PRIVATE -march=native)
  endif()

<<<<<<< HEAD
  if(MSVC)
    target_compile_options(toycc-test-middle-end PRIVATE /utf-8)
  endif()

=======
>>>>>>> f729edc9519e2598f7e7a22384d045f99f08b39a
  add_test(
    NAME toycc-middle-end-tests
    COMMAND $<TARGET_FILE:toycc-test-middle-end>
  )

  add_custom_target(check
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    DEPENDS toycc toycc-test-middle-end
    USES_TERMINAL
  )

  if(TOYCC_ENABLE_IMPORTED_TESTS)
    find_package(Python3 COMPONENTS Interpreter QUIET)

    if(NOT TOYCC_HOST_CC)
      find_program(TOYCC_HOST_CC_DETECTED NAMES clang gcc cc)
      if(TOYCC_HOST_CC_DETECTED)
        set(TOYCC_HOST_CC "${TOYCC_HOST_CC_DETECTED}" CACHE FILEPATH
          "Host C compiler used to build reference outputs" FORCE)
      endif()
    endif()

    if(NOT TOYCC_RISCV_CC)
      find_program(TOYCC_RISCV_CC_DETECTED
        NAMES riscv64-linux-gnu-gcc riscv32-linux-gnu-gcc riscv64-unknown-elf-gcc)
      if(TOYCC_RISCV_CC_DETECTED)
        set(TOYCC_RISCV_CC "${TOYCC_RISCV_CC_DETECTED}" CACHE FILEPATH
          "RISC-V GCC driver used to link generated assembly" FORCE)
      endif()
    endif()

    if(NOT TOYCC_QEMU_RISCV32)
      find_program(TOYCC_QEMU_RISCV32_DETECTED NAMES qemu-riscv32)
      if(TOYCC_QEMU_RISCV32_DETECTED)
        set(TOYCC_QEMU_RISCV32 "${TOYCC_QEMU_RISCV32_DETECTED}" CACHE FILEPATH
          "qemu-riscv32 executable used to run tests" FORCE)
      endif()
    endif()

    set(TOYCC_IMPORTED_TESTS_READY ON)

    if(NOT Python3_EXECUTABLE)
      message(WARNING "Python3 interpreter not found. Imported ToyC tests are disabled.")
      set(TOYCC_IMPORTED_TESTS_READY OFF)
    endif()

    if(NOT TOYCC_HOST_CC)
      message(WARNING "Host C compiler not found. Imported ToyC tests are disabled.")
      set(TOYCC_IMPORTED_TESTS_READY OFF)
    endif()

    if(NOT TOYCC_RISCV_CC)
      message(WARNING "RISC-V GCC driver not found. Imported ToyC tests are disabled.")
      set(TOYCC_IMPORTED_TESTS_READY OFF)
    endif()

    if(NOT TOYCC_QEMU_RISCV32)
      message(WARNING "qemu-riscv32 not found. Imported ToyC tests are disabled.")
      set(TOYCC_IMPORTED_TESTS_READY OFF)
    endif()

    if(NOT EXISTS "${TOYCC_TEST_CASE_LIST}")
      message(WARNING "Test case manifest not found: ${TOYCC_TEST_CASE_LIST}")
      set(TOYCC_IMPORTED_TESTS_READY OFF)
    endif()

    if(TOYCC_IMPORTED_TESTS_READY)
      file(STRINGS "${TOYCC_TEST_CASE_LIST}" TOYCC_IMPORTED_CASES)

      foreach(TOYCC_CASE_REL IN LISTS TOYCC_IMPORTED_CASES)
        string(STRIP "${TOYCC_CASE_REL}" TOYCC_CASE_REL)

        if(TOYCC_CASE_REL STREQUAL "" OR TOYCC_CASE_REL MATCHES "^#")
          continue()
        endif()

        set(TOYCC_CASE_PATH "${TOYCC_TEST_ROOT}/${TOYCC_CASE_REL}")

        if(NOT EXISTS "${TOYCC_CASE_PATH}")
          message(WARNING "Imported test case not found: ${TOYCC_CASE_PATH}")
          continue()
        endif()

        set(TOYCC_TEST_ID "${TOYCC_CASE_REL}")
        string(REPLACE "/" "." TOYCC_TEST_ID "${TOYCC_TEST_ID}")
        string(REGEX REPLACE "\\.c$" "" TOYCC_TEST_ID "${TOYCC_TEST_ID}")

        add_test(
          NAME "toycc.${TOYCC_TEST_ID}"
          COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_toyc_case.py"
            --compiler "$<TARGET_FILE:toycc>"
            --case "${TOYCC_CASE_PATH}"
            --work-dir "${CMAKE_CURRENT_BINARY_DIR}/toycc-tests/${TOYCC_TEST_ID}"
            --host-cc "${TOYCC_HOST_CC}"
            --riscv-cc "${TOYCC_RISCV_CC}"
            --qemu "${TOYCC_QEMU_RISCV32}"
            --timeout "${TOYCC_TEST_TIMEOUT}"
            --riscv-arch "${TOYCC_RISCV_ARCH}"
            --riscv-abi "${TOYCC_RISCV_ABI}"
            --startup "${TOYCC_TEST_STARTUP}"
        )

        set_tests_properties("toycc.${TOYCC_TEST_ID}" PROPERTIES
          LABELS "toycc;imported;strict"
          TIMEOUT "${TOYCC_TEST_TIMEOUT}"
        )
      endforeach()
    endif()
  endif()
endif()

# ================================
# Install
# ================================

install(TARGETS toycc
  RUNTIME DESTINATION bin
)
