

add_executable(test_scheduler)
target_sources(test_scheduler PRIVATE testscheduler.cpp)
target_include_directories(test_scheduler PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src ../include)
target_link_libraries(test_scheduler PRIVATE coroutine)

add_executable(test_mutex)
target_sources(test_mutex PRIVATE testmutex.cpp)
target_include_directories(test_mutex PRIVATE ../include)
target_link_libraries(test_mutex PRIVATE coroutine)

add_executable(test_channel)
target_sources(test_channel PRIVATE testchannel.cpp)
target_include_directories(test_channel PRIVATE ../include)
target_link_libraries(test_channel PRIVATE coroutine)


