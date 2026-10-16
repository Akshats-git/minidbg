cmake_minimum_required(VERSION 3.16)
project(minidbg LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

# The debugger itself: plain C++17, only Linux system headers.
add_executable(minidbg
  src/main.cpp
  src/debugger.cpp
  src/elf.cpp
  src/registers.cpp
  src/tracee.cpp
  src/util.cpp
)
target_compile_options(minidbg PRIVATE -Wall -Wextra)

# ---------------------------------------------------------------------------
# Example programs to debug. They are built with -g -O0 -fno-omit-frame-pointer
# so that "bt" works (it follows the rbp chain). Output goes to build/examples/.
# ---------------------------------------------------------------------------
foreach(name loop factorial segfault)
  add_executable(${name} examples/${name}.c)
  target_compile_options(${name} PRIVATE -g -O0 -fno-omit-frame-pointer)
  set_target_properties(${name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/examples)
endforeach()

# asm_demo imitates the output of a compiler that emits NASM: assembled with
# nasm, linked with gcc. Only built when nasm is installed (sudo apt install nasm).
include(CheckLanguage)
set(CMAKE_ASM_NASM_OBJECT_FORMAT elf64)
check_language(ASM_NASM)
if(CMAKE_ASM_NASM_COMPILER)
  enable_language(ASM_NASM)
  add_executable(asm_demo examples/asm_demo.asm)
  set_target_properties(asm_demo PROPERTIES
    LINKER_LANGUAGE C                        # link with gcc, not bare ld
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/examples)
else()
  message(STATUS "nasm not found: skipping the asm_demo example")
endif()
