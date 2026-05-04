# SPDX-License-Identifier: Apache-2.0

file(GLOB_RECURSE generator_src "${CMAKE_CURRENT_SOURCE_DIR}/src/*")
add_executable(siesta-generator ${generator_src})
target_include_directories(siesta-generator PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/")
target_link_libraries(siesta-generator PRIVATE
	simdjson::simdjson_static
	Boost::boost
	Boost::program_options
	Boost::json
	Boost::system
)

include(GNUInstallDirs)
install(TARGETS siesta-generator EXPORT ${PROJECT_NAME}_Targets RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
