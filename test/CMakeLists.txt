
add_executable(test_deque)
target_sources(test_deque PRIVATE testdeque.cpp)
target_include_directories(test_deque PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

add_executable(test_scheduler)
target_sources(test_scheduler PRIVATE testscheduler.cpp ../src/scheduler.cpp ../src/coroutine.cpp ../src/schedulerinterface.cpp ../src/main.cpp)
target_include_directories(test_scheduler PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src ../include)

add_executable(test_io_uring)
target_sources(test_io_uring PRIVATE testiouring.cpp)
target_link_libraries(test_io_uring PRIVATE uring)

add_executable(test_echoserver)
target_sources(test_echoserver PRIVATE testechoserver.cpp)
target_link_libraries(test_echoserver PRIVATE uring)
