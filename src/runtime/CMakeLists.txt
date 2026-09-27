# cmake
cmake_minimum_required(VERSION 3.20)

project(${API})

aux_source_directory(. NO_SRC_LIST)
add_library(${RUNTIME} STATIC ${NO_SRC_LIST})
target_include_directories(${RUNTIME} PRIVATE ${TP_INC})
target_include_directories(${RUNTIME} PRIVATE ${COMMON_INC})
target_include_directories(${RUNTIME} PRIVATE ${RUNTIME_INC})
