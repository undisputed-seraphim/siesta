
file(GLOB_RECURSE generator_src "${CMAKE_CURRENT_SOURCE_DIR}/src/*")
add_executable(siesta-generator ${generator_src})
target_link_libraries(siesta-generator PUBLIC
	simdjson::simdjson
	Boost::boost
	Boost::program_options
	Boost::json
	Boost::system
	fmt::fmt
)