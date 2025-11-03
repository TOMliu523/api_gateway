cmake_minimum_required(VERSION 3.20.2)

set(NAME data_obj)

aux_source_directory(. SRC_DATA)
add_library(data_obj OBJECT ${SRC_DATA})

target_include_directories(${NAME} PRIVATE ${DPDK_INC} 
                                           ${COMMON_INC} 
                                           ${PROTOCOL_INC}
                                           ${CONF_DATA_INC} 
                                           ${CONF_CONTROL_INC} 
                                           ${DPDK_INCLUDE_DIRS})
