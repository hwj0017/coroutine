add_executable(rpcserver)
target_sources(rpcserver PRIVATE rpcserver.cpp service.pb.cc)
target_include_directories(rpcserver PRIVATE ../include)
target_link_libraries(rpcserver PRIVATE rpc)

add_executable(rpcclient)
target_sources(rpcclient PRIVATE rpcclient.cpp service.pb.cc)
target_link_libraries(rpcclient PRIVATE rpc)
