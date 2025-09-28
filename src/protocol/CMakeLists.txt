# cmake
cmake_minimum_required(VERSION 3.20.2)

set(NAME ${PROTOCOL})
project(${NAME})

set(L2 l2)
set(L3 l3)
set(L4 l4)

add_subdirectory(${L2})
add_subdirectory(${L3})
#add_subdirectory(${L4})

aux_source_directory(. NO_SRC_LIST)
add_library(${NAME} STATIC ${NO_SRC_LIST})
target_include_directories(${NAME} PRIVATE ${DPDK_INC})
target_include_directories(${NAME} PRIVATE ${COMMON_INC})
target_include_directories(${NAME} PRIVATE ${PROTOCOL_INC})
target_include_directories(${NAME} PRIVATE ${CONF_INC})
target_include_directories(${NAME} PRIVATE ${DPDK_INCLUDE_DIRS})

target_link_libraries(${PROJECT_NAME} PRIVATE
                      -Wl,--whole-archive
                      -L.
                      ${L2}
                      ${L3}
                      -Wl,--no-whole-archive)
