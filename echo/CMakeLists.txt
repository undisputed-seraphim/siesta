cmake_minimum_required(VERSION 3.31)
project(echo LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(siesta REQUIRED)

add_executable(echo_server test/test_server.cpp)

siesta_generate(
	TARGET echo_server
	SCHEMA ${CMAKE_CURRENT_SOURCE_DIR}/../tests/echo_v3.json
	MODE BOTH
)
