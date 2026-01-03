add_executable(echoserver)
target_sources(echoserver PRIVATE echoserver.cpp ${CMAKE_SOURCE_DIR}/src/net/tcp/tcpserver.cpp ${CMAKE_SOURCE_DIR}/src/net/tcp/tcpconnection.cpp ${CMAKE_SOURCE_DIR}/src/coroutine/main.cpp ${CMAKE_SOURCE_DIR}/src/coroutine/all.cpp)
target_link_libraries(echoserver PRIVATE uring)
target_include_directories(echoserver PRIVATE ${CMAKE_SOURCE_DIR}/include)

add_executable(echoclient)
target_sources(echoclient PRIVATE echoclient.cpp)
