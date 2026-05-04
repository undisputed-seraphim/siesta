cmake_minimum_required(VERSION 3.31)
project(openai_compile LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT TARGET siesta)
	find_package(siesta REQUIRED)
endif()

add_executable(openai_compile_test EXCLUDE_FROM_ALL
	"${CMAKE_CURRENT_SOURCE_DIR}/compile_test.cpp"
)

siesta_generate(
	TARGET openai_compile_test
	SCHEMA "${CMAKE_CURRENT_SOURCE_DIR}/../openai.json"
	MODE CLIENT
	NO_PYTHON
)
