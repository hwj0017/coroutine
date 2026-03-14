# benchmarks/CMakeLists.txt

# 2. 定义压测源文件
set(BENCH_SOURCES
    main.cpp
)



# 3. 创建可执行文件
add_executable(manual_bench ${BENCH_SOURCES})

# 4. 配置编译选项 (针对压测优化)
if(MSVC)
    # Windows 环境优化
    target_compile_options(manual_bench PRIVATE /O2 /Oi /Ot /Gy)
else()
    # Linux/macOS 环境优化
    target_compile_options(manual_bench PRIVATE
        -g
        -O3 # 最高等级优化
        -march=native # 针对当前 CPU 指令集优化 (如 AVX, BMI2)
        -flto # 链接时优化，这对协程内联至关重要
        -fno-exceptions # 如果你的库不依赖异常，关闭它可以提升性能
    )
    # 链接时也需要开启 LTO
    set_property(TARGET manual_bench PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

# 5. 链接你的协程库和系统库
target_link_libraries(manual_bench
    PRIVATE
    coroutine # 你的核心库目标
)
add_executable(check)
target_sources(check PRIVATE check.cpp)
target_include_directories(check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src ../include)

# 6. 设置输出名称，方便识别
set_target_properties(manual_bench PROPERTIES OUTPUT_NAME "coro_bench_tool")

add_executable(mem)
target_sources(mem PRIVATE mem.cpp)
target_include_directories(mem PRIVATE ../include)
target_link_libraries(mem PRIVATE coroutine)
