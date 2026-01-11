
# 查找源文件
file(GLOB_RECURSE COROUTINE_SOURCES
    "src/*.cpp"
    # 添加其他需要的源文件路径
)
message(STATUS "COROUTINE_SOURCES: ${COROUTINE_SOURCES}")
# 创建静态库
add_library(coroutine STATIC ${COROUTINE_SOURCES})
# 设置头文件目录
target_include_directories(coroutine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(coroutine PUBLIC uring)
# 为特定目标设置输出路径（可选，如果上面的全局设置不生效）
set_target_properties(coroutine PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/lib
)
add_subdirectory(test)
