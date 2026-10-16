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
  src/tracee.cpp
  src/util.cpp
)
target_compile_options(minidbg PRIVATE -Wall -Wextra)
