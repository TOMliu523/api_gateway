# cmake
cmake_minimum_required(VERSION 3.20.2)

project(${DPDK})

aux_source_directory(. NO_SRC_LIST)
add_library(${DPDK} STATIC ${NO_SRC_LIST})
target_include_directories(${DPDK} PRIVATE ${DPDK_INC})
target_include_directories(${DPDK} PRIVATE ${COMMON_INC})
target_include_directories(${DPDK} PRIVATE ${DPDK_INCLUDE_DIRS})
