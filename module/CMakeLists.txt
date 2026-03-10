cmake_minimum_required(VERSION 3.28)
project(ModuleTest LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 显式指定编译器为 Clang 18（如果你的环境默认不是它）
# set(CMAKE_CXX_COMPILER clang++-18) 

add_executable(app)

target_sources(app
    PRIVATE
    main.cpp
    PUBLIC
    FILE_SET cxx_modules TYPE CXX_MODULES FILES
    math.cppm
)
