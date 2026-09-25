# cmake
cmake_minimum_required(VERSION 3.20.2)

set(NAME ${PROTOCOL})
project(${NAME})

aux_source_directory(. NO_SRC_LIST)
add_library(${NAME} STATIC ${NO_SRC_LIST})
target_include_directories(${NAME} PRIVATE ${DPDK_INC})
target_include_directories(${NAME} PRIVATE ${COMMON_INC})
target_include_directories(${NAME} PRIVATE ${PROTOCOL_INC})
target_include_directories(${NAME} PRIVATE ${DPDK_INCLUDE_DIRS})
