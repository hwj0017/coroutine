# 查找源文件
file(GLOB_RECURSE COROUTINE_SOURCES
    "src/*.cpp"
    # 添加其他需要的源文件路径
)
# 创建静态库
add_library(http STATIC ${COROUTINE_SOURCES})
# 设置头文件目录
target_include_directories(http PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(http PUBLIC tcp)
add_subdirectory(test)
