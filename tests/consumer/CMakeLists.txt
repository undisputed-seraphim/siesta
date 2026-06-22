cmake_minimum_required(VERSION 3.31)
project(siesta_consumer_smoke LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(siesta REQUIRED)

add_library(ping_api STATIC)
siesta_generate(
	TARGET ping_api
	SCHEMA "${CMAKE_CURRENT_SOURCE_DIR}/ping.json"
	MODE BOTH
	BACKEND beast
	MODULE_NAME Ping
	NO_PYTHON
)

add_executable(smoke smoke.cpp)
target_link_libraries(smoke PRIVATE ping_api)
