# SPDX-License-Identifier: Apache-2.0

file(GLOB_RECURSE generator_rpc_src "${CMAKE_CURRENT_SOURCE_DIR}/src/*")

# Shared sources from generator/ — AST, parsing, dependency analysis, utilities.
# These are frontend/middle-end modules reused by the RPC compiler.
file(GLOB_RECURSE shared_generator_src
	"${CMAKE_SOURCE_DIR}/generator/src/Frontend/openapi.cpp"
	"${CMAKE_SOURCE_DIR}/generator/src/Frontend/openapi3.cpp"
	"${CMAKE_SOURCE_DIR}/generator/src/Frontend/AST.cpp"
	"${CMAKE_SOURCE_DIR}/generator/src/Frontend/SchemaParser.cpp"
	"${CMAKE_SOURCE_DIR}/generator/src/IR/DependencyGraph.cpp"
	"${CMAKE_SOURCE_DIR}/generator/src/Support/Utils.cpp"
)

add_executable(siesta-openrpc-generator ${generator_rpc_src} ${shared_generator_src})

target_include_directories(siesta-openrpc-generator PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/src/"
	"${CMAKE_SOURCE_DIR}/generator/src/"
)
target_link_libraries(siesta-openrpc-generator PRIVATE
	simdjson::simdjson_static
	Boost::boost
	Boost::program_options
	Boost::json
	Boost::system
)
