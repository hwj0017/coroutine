add_executable(httpserver)
target_sources(httpserver PRIVATE httpserver.cpp)
target_include_directories(httpserver PRIVATE ../include)
target_link_libraries(httpserver PRIVATE http)

add_executable(httpclient)
target_sources(httpclient PRIVATE httpclient.cpp)
