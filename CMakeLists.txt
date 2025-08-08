cmake_minimum_required(VERSION 3.10)
project(coroutine)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Debug)

add_subdirectory(test)
include_directories(include)