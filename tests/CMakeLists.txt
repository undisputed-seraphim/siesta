if (NOT BUILD_TESTING)
	#return()
endif()

find_package(Catch2 REQUIRED)
message(STATUS "Catch2 ${Catch2_VERSION}")

file(GLOB_RECURSE test_srcs "${CMAKE_CURRENT_SOURCE_DIR}/siesta/*")
add_executable(siesta_test ${test_srcs})
target_link_libraries(siesta_test PUBLIC siesta)
add_test(NAME siesta_test COMMAND $<TARGET_FILE:siesta_test>)