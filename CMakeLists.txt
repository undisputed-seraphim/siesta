cmake_minimum_required(VERSION 3.0)
project(siesta VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(simdjson 3.1 REQUIRED)
message(STATUS "simdjson version ${simdjson_VERSION}")

file(GLOB src "${CMAKE_CURRENT_SOURCE_DIR}/src/*")
add_executable(siesta ${src})
target_include_directories(siesta PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(siesta PUBLIC simdjson::simdjson)

#find_package(PkgConfig)
#pkg_search_module(nghttp2 REQUIRED libnghttp2)
#pkg_search_module(nghttp2_asio REQUIRED libnghttp2_asio)
#file(GLOB output_src ${CMAKE_CURRENT_SOURCE_DIR}/output/*)
#add_library(output STATIC ${output_src})
#target_include_directories(output PRIVATE ${nghttp2_INCLUDE_DIRS} ${nghttp2_asio_INCLUDE_DIRS})
#target_link_libraries(output PRIVATE ${nghttp2_LIBRARIES} ${nghttp2_asio_LIBRARIES})

#find_package(Boost 1.81 REQUIRED COMPONENTS system json)
#file(GLOB output_src ${CMAKE_CURRENT_SOURCE_DIR}/output/*)
#add_library(output STATIC ${output_src})
#target_link_libraries(output PRIVATE Boost::headers Boost::json Boost::system)
