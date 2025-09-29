cmake_minimum_required(VERSION 3.20.2)

set(NAME conf)
project(${NAME})

add_subdirectory(control)
add_subdirectory(data)

add_library(${NAME} STATIC
    $<TARGET_OBJECTS:control_obj>
    $<TARGET_OBJECTS:data_obj>
)

target_include_directories(${NAME} PRIVATE ${DPDK_INC} 
                                           ${COMMON_INC} 
                                           ${CONF_DATA_INC} 
                                           ${CONF_CONTROL_INC} 
                                           ${DPDK_INCLUDE_DIRS})
