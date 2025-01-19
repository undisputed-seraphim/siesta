# SPDX-License-Identifier: Apache-2.0
if (NOT BUILD_TESTING)
	#return()
endif()
enable_testing()

find_package(Catch2 3.8 REQUIRED)
message(STATUS "Catch2 ${Catch2_VERSION}")

include(CTest)
include(Catch)

file(GLOB_RECURSE test_srcs "${CMAKE_CURRENT_SOURCE_DIR}/siesta/*")
add_executable(siesta_test ${test_srcs})
target_include_directories(siesta_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(siesta_test
	PRIVATE
	siesta
	Catch2::Catch2WithMain
)

catch_discover_tests(siesta_test)

add_custom_command(
OUTPUT
	${CMAKE_BINARY_DIR}/test_echo/echo_client.hpp
	${CMAKE_BINARY_DIR}/test_echo/echo_defs.cpp
	${CMAKE_BINARY_DIR}/test_echo/echo_defs.hpp
	${CMAKE_BINARY_DIR}/test_echo/echo_server.cpp
	${CMAKE_BINARY_DIR}/test_echo/echo_server.hpp
COMMAND
	${CMAKE_BINARY_DIR}/generator/siesta-generator -i ${CMAKE_CURRENT_SOURCE_DIR}/tests/echo.json -o ${CMAKE_BINARY_DIR}/test_echo/
DEPENDS
	siesta-generator
)

file(GLOB_RECURSE samplesrc "${CMAKE_CURRENT_SOURCE_DIR}/samples/*")
if (samplesrc)
	#add_library(samples SHARED ${samplesrc})
	#target_link_libraries(samples PUBLIC siesta Boost::boost)
endif()
