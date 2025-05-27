# cmake
cmake_minimum_required(VERSION 3.20.2)

set(API api)
set(COMMON common)
set(API_INC ${INC}/api)
set(COMMON_INC ${INC}/common)

find_package(PkgConfig REQUIRED)
pkg_check_modules(DPDK REQUIRED IMPORTED_TARGET libdpdk)
pkg_check_modules(JEMALLOC REQUIRED IMPORTED_TARGET jemalloc)

add_subdirectory(${COMMON})
add_subdirectory(${API})

add_executable(${PROJECT_NAME} main.c)
target_include_directories(${PROJECT_NAME} PRIVATE ${COMMON_INC})
target_include_directories(${PROJECT_NAME} PRIVATE ${API_INC})
target_include_directories(${PROJECT_NAME} PRIVATE ${TP_INC})

target_link_libraries(${PROJECT_NAME} PRIVATE
                      ${API}
                      ${COMMON}
                      PkgConfig::DPDK
                      PkgConfig::JEMALLOC)

install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)
