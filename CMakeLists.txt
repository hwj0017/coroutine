cmake_minimum_required(VERSION 3.28) # 必须 3.28+ 才能支持 CXX_MODULES
project(coroutine LANGUAGES CXX)

# 1. 强制要求 C++20
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF) # 建议关闭扩展，保持标准兼容
# 关闭 C++ 模块的自动扫描
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_CXX_FLAGS_DEBUG "-g")
set(CMAKE_CXX_FLAGS_RELEASE "-g -O3 -DNDEBUG")
add_subdirectory(coroutine)
add_subdirectory(tcp)
add_subdirectory(rpc)
add_subdirectory(http)
