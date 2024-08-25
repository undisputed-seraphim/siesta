# SPDX-License-Identifier: Apache-2.0
if (NOT BUILD_TESTING)
	#return()
endif()
enable_testing()

find_package(Catch2 2.13 REQUIRED)
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

#add_custom_command(OUTPUT )