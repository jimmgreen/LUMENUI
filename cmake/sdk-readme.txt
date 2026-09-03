LUMEN SDK — Windows x64（MSVC Release /MD）
==========================================

include/lumen/     公共头
lib/               lumen.lib  lumen_main.lib  lumatext.lib
bin/lumatext.dll   放到 exe 同目录（lumen_add_executable 会拷）
lib/cmake/lumen/   find_package(lumen CONFIG)

CMake：

  cmake_minimum_required(VERSION 3.25)
  project(myapp LANGUAGES CXX)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_PREFIX_PATH "${CMAKE_CURRENT_LIST_DIR}/path/to/this-sdk")
  find_package(lumen CONFIG REQUIRED)
  lumen_add_executable(myapp main.cpp)

需要 MSVC 2022+ x64、C++20、Windows 10 1903+。请用 Release、/MD，不要混 Debug CRT。
源码与许可证：https://github.com/jimmgreen/LUMENUI
