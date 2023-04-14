cmake_minimum_required(VERSION 3.25)
project(siesta VERSION 0.1.0)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

find_package(Boost 1.81 REQUIRED COMPONENTS system json program_options)
find_package(simdjson 3.3 REQUIRED)
message(STATUS "simdjson version ${simdjson_VERSION}")
find_package(fmt 9 REQUIRED)
message(STATUS "fmt ${fmt_VERSION}")

#include(nghttp3)

file(GLOB src "${CMAKE_CURRENT_SOURCE_DIR}/src/*")
add_executable(siesta ${src})
target_compile_options(siesta PUBLIC "-fexperimental-library")
target_link_libraries(siesta PUBLIC
	simdjson::simdjson
	Boost::boost
	Boost::program_options
	Boost::json
	Boost::system
	fmt::fmt
)

#find_package(PkgConfig)
#pkg_search_module(nghttp2 REQUIRED libnghttp2)
#pkg_search_module(nghttp2_asio REQUIRED libnghttp2_asio)
#file(GLOB output_src ${CMAKE_CURRENT_SOURCE_DIR}/output/*)
#add_library(output STATIC ${output_src})
#target_include_directories(output PRIVATE ${nghttp2_INCLUDE_DIRS} ${nghttp2_asio_INCLUDE_DIRS})
#target_link_libraries(output PRIVATE ${nghttp2_LIBRARIES} ${nghttp2_asio_LIBRARIES})

file(GLOB output_src ${CMAKE_CURRENT_SOURCE_DIR}/output/*)
add_library(output SHARED ${output_src})
target_include_directories(output PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(output PRIVATE Boost::headers Boost::json Boost::system fmt::fmt)
