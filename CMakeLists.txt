cmake_minimum_required(VERSION 3.10)
project(coroutine)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Debug)
file(GLOB SOURCES src/*.cpp)
message(${SOURCES})
add_library(coroutine STATIC ${SOURCES})
target_include_directories(coroutine PUBLIC "include")

add_executable(testcoroutine test/testcoroutine.cpp ${SOURCES})
target_include_directories(testcoroutine PUBLIC "include")