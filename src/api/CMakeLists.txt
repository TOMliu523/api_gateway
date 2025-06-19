# cmake
cmake_minimum_required(VERSION 3.20.2)

project(${API})

aux_source_directory(. NO_SRC_LIST)
add_library(${API} STATIC ${NO_SRC_LIST})
target_include_directories(${API} PRIVATE ${TP_INC})
target_include_directories(${API} PRIVATE ${API_INC})
target_include_directories(${API} PRIVATE ${DPDK_INC})
target_include_directories(${API} PRIVATE ${COMMON_INC})
