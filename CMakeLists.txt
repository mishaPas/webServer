cmake_minimum_required(VERSION 3.16)
project(cpp_web_server CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_library(server_lib
src/http.cpp
src/server.cpp
)

target_include_directories(server_lib PUBLIC include)
target_link_libraries(server_lib PUBLIC Threads::Threads)
target_compile_options(server_lib PRIVATE -Wall -Wextra -Wpedantic)

add_executable(server src/main.cpp)
target_link_libraries(server PRIVATE server_lib)
