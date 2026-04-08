
# 查找源文件
file(GLOB_RECURSE COROUTINE_SOURCES
    "src/*.cpp"
    # 添加其他需要的源文件路径
)
# 创建静态库
add_library(coroutine STATIC ${COROUTINE_SOURCES})
# 包含头文件路径
target_include_directories(coroutine PUBLIC ${URING_INCLUDE_DIR})
target_include_directories(coroutine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(coroutine PUBLIC ${URING_LIBRARY} mimalloc)

# ... 项目定义 ...

add_subdirectory(benchmarks)

add_subdirectory(test)
