# cmake
cmake_minimum_required(VERSION 3.20.2)

add_executable(${PROJECT_NAME} main.c)
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)
