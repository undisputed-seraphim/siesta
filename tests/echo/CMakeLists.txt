cmake_minimum_required(VERSION 3.31)
project(echo LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT TARGET siesta)
	find_package(siesta REQUIRED)
endif()

# Shared generated-code lib — siesta_generate adds defs + server sources
add_library(echo_gen STATIC)
siesta_generate(
	TARGET echo_gen
	SCHEMA "${CMAKE_CURRENT_SOURCE_DIR}/../echo.json"
	MODE BOTH
)

# Standard release server
add_executable(echo_server "${CMAKE_CURRENT_SOURCE_DIR}/test_server.cpp")
target_link_libraries(echo_server PRIVATE echo_gen)

# Profiled server
add_executable(echo_server_prof "${CMAKE_CURRENT_SOURCE_DIR}/test_server.cpp")
target_link_libraries(echo_server_prof PRIVATE echo_gen)
find_library(PROFILER_LIB profiler REQUIRED)
target_link_libraries(echo_server_prof PRIVATE ${PROFILER_LIB})
target_compile_definitions(echo_server_prof PRIVATE ENABLE_PROFILER)
target_compile_options(echo_server_prof PRIVATE -g -fno-omit-frame-pointer)
