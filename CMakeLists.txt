# api_gateway
cmake_minimum_required(VERSION 3.9)
project(api_gateway)

set(CMAKE_C_FLAGS "-std=gnu99 -ggdb -Werror")
set(CMAKE_VERBOSE_MAKEFILE ON)

set(SRC ${PROJECT_SOURCE_DIR}/src)
set(INC ${PROJECT_SOURCE_DIR}/inc)
set(TP ${PROJECT_SOURCE_DIR}/thirdparty)
set(INSTALL ${PROJECT_SOURCE_DIR}/install)

set(TP_LIB ${INSTALL}/lib)
set(TP_INC ${INSTALL}/include)
set(COMMON_INC ${INC}/common)
set(DPDK_LIB ${INSTALL}/lib/x86_64-linux-gnu)

set(ENV{PKG_CONFIG_PATH} "${DPDK_LIB}/pkgconfig:${TP_LIB}//pkgconfig:$ENV{PKG_CONFIG_PATH}")

execute_process(COMMAND ./tp_install.sh WORKING_DIRECTORY ${TP})

add_subdirectory(${SRC})
