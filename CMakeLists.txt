cmake_minimum_required(VERSION 3.10)
project(cpp_web_server CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(server src/main.cpp)
