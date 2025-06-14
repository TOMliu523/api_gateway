# cmake
cmake_minimum_required(VERSION 3.20)

set(API api)
set(DATA data)
set(DPDK dpdk)
set(COMMON common)
set(DATAPLANE dataplane)

set(API_INC ${INC}/api)
set(DPDK_INC ${INC}/dpdk)
set(COMMON_INC ${INC}/common)
set(DATAPLANE_INC ${INC}/dataplane)

find_package(PkgConfig REQUIRED)
pkg_check_modules(DPDK REQUIRED IMPORTED_TARGET libdpdk)
pkg_check_modules(LIBYANG REQUIRED IMPORTED_TARGET libyang)
pkg_check_modules(SYSREPO REQUIRED IMPORTED_TARGET sysrepo)
pkg_check_modules(JANSSON REQUIRED IMPORTED_TARGET jansson)
pkg_check_modules(JEMALLOC REQUIRED IMPORTED_TARGET jemalloc)
pkg_check_modules(OPENSSL REQUIRED IMPORTED_TARGET openssl)

add_subdirectory(${COMMON})
add_subdirectory(${API})
add_subdirectory(${DPDK})
add_subdirectory(${DATAPLANE})

add_executable(${PROJECT_NAME} main.c)
target_include_directories(${PROJECT_NAME} PRIVATE ${API_INC})
target_include_directories(${PROJECT_NAME} PRIVATE ${DPDK_INC})
target_include_directories(${PROJECT_NAME} PRIVATE ${COMMON_INC})
target_include_directories(${PROJECT_NAME} PRIVATE ${DATAPLANE_INC})
target_include_directories(${PROJECT_NAME} PRIVATE ${DPDK_INCLUDE_DIRS})

target_link_libraries(${PROJECT_NAME} PRIVATE
                      -Wl,--whole-archive
                      -L.
                      ${API}
                      ${COMMON}
                      ${DPDK}
                      ${DATAPLANE}
                      -L${TP_LIB} -lmongoose
                      -Wl,--no-whole-archive
                      PkgConfig::LIBYANG
                      PkgConfig::SYSREPO
                      PkgConfig::JANSSON
                      PkgConfig::OPENSSL
                      ${DPDK_STATIC_LDFLAGS}
                      PkgConfig::JEMALLOC)

install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)
