# 1. 自动生成代码并定义变量
# 这条命令会告诉 CMake：如果 service.proto 变了，就运行 protoc
# 生成的文件存放在 ${CMAKE_CURRENT_BINARY_DIR} 中
# 2. 创建一个专门存放 Proto 生成代码的库
# 这样做的好处是 service.pb.cc 只会被编译一次
add_library(rpc_proto STATIC service.proto)

protobuf_generate(
    TARGET rpc_proto # 绑定到目标
    LANGUAGE cpp # 指定语言
    OUT_VAR PROTO_SOURCES # 输出变量名
)



# 链接 protobuf 运行库，并导出生成的头文件目录
target_link_libraries(rpc_proto PUBLIC protobuf::libprotobuf)
target_include_directories(rpc_proto PUBLIC ${CMAKE_CURRENT_BINARY_DIR})

# --- 下面是你的三个可执行文件 ---

# 3. 编译 rpcserver
add_executable(rpcserver rpcserver.cpp)
target_include_directories(rpcserver PRIVATE ../include)
# 链接 rpc 业务库和刚才生成的 rpc_proto 库
target_link_libraries(rpcserver PRIVATE rpc rpc_proto)

# 4. 编译 rpcclient
add_executable(rpcclient rpcclient.cpp)
target_link_libraries(rpcclient PRIVATE rpc rpc_proto)

# 5. 编译 rpcbenchmark
add_executable(rpcbenchmark rpcbenchmark.cpp)
target_link_libraries(rpcbenchmark PRIVATE rpc rpc_proto)
