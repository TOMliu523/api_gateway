# api_gateway
cmake_minimum_required(VERSION 3.9)
project(api_gateway)

set(CMAKE_C_FLAGS "-std=c99 -ggdb")
set(CMAKE_VERBOSE_MAKEFILE ON)

set(TP thirdparty)
execute_process(COMMAND ./tp_install.sh
                WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/${TP})

add_subdirectory(src)
