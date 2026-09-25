# cmake
cmake_minimum_required(VERSION 3.20)

project(${COMMON})

aux_source_directory(. NO_SRC_LIST)
add_library(${COMMON} STATIC ${NO_SRC_LIST})
target_include_directories(${COMMON} PRIVATE ${COMMON_INC})
target_include_directories(${COMMON} PRIVATE ${DPDK_INCLUDE_DIRS})
