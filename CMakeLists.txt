cmake_minimum_required(VERSION 3.28) # 必须 3.28+ 才能支持 CXX_MODULES
project(coroutine LANGUAGES CXX)

find_package(protobuf CONFIG REQUIRED)
find_package(mimalloc REQUIRED)
# 1. 查找头文件路径
find_path(URING_INCLUDE_DIR NAMES liburing.h)
# 2. 查找库文件路径
find_library(URING_LIBRARY NAMES uring)
# 1. 强制要求 C++20
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF) # 建议关闭扩展，保持标准兼容
# 关闭 C++ 模块的自动扫描
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_CXX_FLAGS_DEBUG "-g")
set(CMAKE_CXX_FLAGS_RELEASE "-g -O3 -DNDEBUG")

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    # # message(STATUS "Enable AddressSanitizer (ASan)")

    # # 1. 增加编译选项
    # add_compile_options(-fsanitize=address -fno-omit-frame-pointer -g)

    # # 2. 增加链接选项（ASan 必须链接运行时库）
    # add_link_options(-fsanitize=address)
endif()
add_subdirectory(coroutine)
add_subdirectory(tcp)
add_subdirectory(rpc)
add_subdirectory(http)
