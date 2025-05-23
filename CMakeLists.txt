# api_gateway
cmake_minimum_required(VERSION 3.9)
project(api_gateway)

set(CMAKE_C_FLAGS "-std=c99 -ggdb")
set(CMAKE_VERBOSE_MAKEFILE ON)

add_subdirectory(src)
