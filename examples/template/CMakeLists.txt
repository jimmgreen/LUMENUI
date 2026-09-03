cmake_minimum_required(VERSION 3.25)
project(lumen_app LANGUAGES CXX RC)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../CMakeLists.txt")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../.." lumen)
else()
    include(FetchContent)
    FetchContent_Declare(lumen
        GIT_REPOSITORY https://github.com/jimmgreen/LUMENUI.git
        GIT_TAG HEAD)
    FetchContent_MakeAvailable(lumen)
endif()

lumen_add_executable(lumen_app main.cpp app.rc)
