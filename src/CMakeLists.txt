# cmake
cmake_minimum_required(VERSION 3.20.2)

set(COMMON common)

find_package(PkgConfig REQUIRED)
pkg_check_modules(DPDK REQUIRED IMPORTED_TARGET libdpdk)
pkg_check_modules(JEMALLOC REQUIRED IMPORTED_TARGET jemalloc)

add_subdirectory(${COMMON})

add_executable(${PROJECT_NAME} main.c)
target_include_directories(${PROJECT_NAME} PRIVATE ${COMMON_INC})
target_include_directories(${PROJECT_NAME} PRIVATE ${TP_INC})

target_link_libraries(${PROJECT_NAME} PRIVATE
                      ${COMMON}
                      PkgConfig::DPDK
                      PkgConfig::JEMALLOC)

install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)
