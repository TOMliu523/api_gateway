# api_gateway
cmake_minimum_required(VERSION 3.20)
project(api_gateway)

set(CMAKE_C_FLAGS "-std=gnu99 -Werror -mssse3 -ggdb -mrtm")
#set(CMAKE_C_FLAGS "-std=gnu99 -Werror -mssse3 -Ofast -mrtm")
set(CMAKE_VERBOSE_MAKEFILE ON)

set(SRC ${PROJECT_SOURCE_DIR}/src)
set(INC ${PROJECT_SOURCE_DIR}/inc)
set(TP ${PROJECT_SOURCE_DIR}/thirdparty)
set(SCRPT ${PROJECT_SOURCE_DIR}/scripts)

set(TP_INSTALL ${TP}/install)

set(TP_BIN ${TP_INSTALL}/bin)
set(TP_LIB ${TP_INSTALL}/lib)
set(TP_LIB64 ${TP_INSTALL}/lib64)
set(TP_INC ${TP_INSTALL}/include)
set(DPDK_LIB ${TP_INSTALL}/lib/x86_64-linux-gnu)

set(ENV{PKG_CONFIG_PATH} "${DPDK_LIB}/pkgconfig:${TP_LIB}/pkgconfig:${TP_LIB64}/pkgconfig:$ENV{PKG_CONFIG_PATH}")

execute_process(COMMAND
                ./tp_install.sh "${TP_INSTALL}"
                WORKING_DIRECTORY ${TP})

add_subdirectory(${SRC})

# install conf
install(DIRECTORY ${PROJECT_SOURCE_DIR}/conf DESTINATION .)

# install bin
install(PROGRAMS ${TP_BIN}/sysrepoctl DESTINATION bin)
install(PROGRAMS ${TP_BIN}/sysrepocfg DESTINATION bin)
install(PROGRAMS ${TP_BIN}/dpdk-devbind.py DESTINATION bin)

#install scripts
install(PROGRAMS ${SCRPT}/setup.sh DESTINATION .)

# install lib
file(GLOB LIB "${TP_LIB}/*.so" "${TP_LIB}/*.so*")
install(FILES ${LIB} DESTINATION lib)

# install lib64
file(GLOB LIB64 "${TP_LIB64}/*.so" "${TP_LIB64}/*.so*")
install(FILES ${LIB64} DESTINATION lib64)
