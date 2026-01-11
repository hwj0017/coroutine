# add_executable(test_coro)
# target_sources(test_coro PRIVATE testcoro.cpp)
# target_include_directories(test_coro PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
# add_executable(test_deque)
# target_sources(test_deque PRIVATE testdeque.cpp)
# target_include_directories(test_deque PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

# add_executable(test_scheduler)
# target_sources(test_scheduler PRIVATE testscheduler.cpp ../src/main.cpp ../src/scheduler.cpp ../src/all.cpp)
# target_include_directories(test_scheduler PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src ../include)
# target_link_libraries(test_scheduler PRIVATE uring)

# add_executable(test_simple_scheduler)
# target_sources(test_simple_scheduler PRIVATE testscheduler.cpp ../src/main.cpp ../src/all.cpp)
# target_include_directories(test_simple_scheduler PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src ../include)
# target_link_libraries(test_simple_scheduler PRIVATE uring)
# add_executable(test_io_uring)
# target_sources(test_io_uring PRIVATE testiouring.cpp)
# target_link_libraries(test_io_uring PRIVATE uring)

# add_executable(test_echoserver)
# target_sources(test_echoserver PRIVATE testechoserver.cpp)
# target_link_libraries(test_echoserver PRIVATE uring)

# add_executable(test_mutex)
# target_sources(test_mutex PRIVATE testmutex.cpp ../src/main.cpp ../src/scheduler.cpp)
# target_include_directories(test_mutex PRIVATE ../include)

add_executable(test_io)
target_sources(test_io PRIVATE testio.cpp)
target_include_directories(test_io PRIVATE ../include)
target_link_libraries(test_io PRIVATE coroutine)


