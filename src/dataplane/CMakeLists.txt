# cmake
cmake_minimum_required(VERSION 3.20.2)

project(${DATAPLANE})

aux_source_directory(. NO_SRC_LIST)
add_library(${DATAPLANE} STATIC ${NO_SRC_LIST})
target_include_directories(${DATAPLANE} PRIVATE ${DPDK_INC})
target_include_directories(${DATAPLANE} PRIVATE ${COMMON_INC})
target_include_directories(${DATAPLANE} PRIVATE ${DATAPLANE_INC})
target_include_directories(${DATAPLANE} PRIVATE ${DPDK_INCLUDE_DIRS})
