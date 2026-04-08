# 查找源文件
file(GLOB_RECURSE COROUTINE_SOURCES
    "src/*.cpp"
    # 添加其他需要的源文件路径
)
# 创建静态库
# 暂时没有源文件
add_library(tcp STATIC ${COROUTINE_SOURCES})
# 设置头文件目录
target_include_directories(tcp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(tcp PUBLIC coroutine)

add_subdirectory(test)
