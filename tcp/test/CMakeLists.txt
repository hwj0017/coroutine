add_executable(echoserver)
target_sources(echoserver PRIVATE echoserver.cpp)
target_include_directories(echoserver PRIVATE ../include ${CMAKE_SOURCE_DIR}/coroutine/include)
target_link_libraries(echoserver PRIVATE tcp)

add_executable(echoclient)
target_sources(echoclient PRIVATE echoclient.cpp)
target_include_directories(echoclient PRIVATE ../include ${CMAKE_SOURCE_DIR}/coroutine/include)
target_link_libraries(echoclient PRIVATE tcp)
