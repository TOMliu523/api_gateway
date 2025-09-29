set(NAME control_obj)

aux_source_directory(. SRC_CONTROL)
add_library(control_obj OBJECT ${SRC_CONTROL})

target_include_directories(${NAME} PRIVATE ${DPDK_INC}
                                           ${COMMON_INC}
                                           ${CONF_CONTROL_INC}
                                           ${DPDK_INCLUDE_DIRS})
