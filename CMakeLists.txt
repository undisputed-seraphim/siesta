cmake_minimum_required(VERSION 3.25)
project(siesta VERSION 0.1.0)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

set(Boost_USE_STATIC_LIBS ON)
find_package(Boost 1.81 REQUIRED COMPONENTS system json program_options)
find_package(simdjson 3.6 REQUIRED)
message(STATUS "simdjson version ${simdjson_VERSION}")
find_package(fmt 9 REQUIRED)
message(STATUS "fmt ${fmt_VERSION}")

#include(nghttp3)

file(GLOB_RECURSE lib_src "${CMAKE_CURRENT_SOURCE_DIR}/src/*")
add_library(siesta STATIC ${lib_src})
target_include_directories(siesta PUBLIC
	"${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(siesta PUBLIC
	Boost::boost
	Boost::json
	Boost::system
	fmt::fmt
)

file(GLOB_RECURSE generator_src "${CMAKE_CURRENT_SOURCE_DIR}/generator/*")
add_executable(siesta-generator ${generator_src})
target_link_libraries(siesta-generator PUBLIC
	simdjson::simdjson
	Boost::boost
	Boost::program_options
	Boost::json
	Boost::system
	fmt::fmt
)

add_subdirectory(output)
