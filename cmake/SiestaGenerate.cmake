# SPDX-License-Identifier: Apache-2.0
#
# siesta_generate(TARGET <name> SCHEMA <path> [MODE CLIENT|SERVER|BOTH])
#
# Runs siesta-generator on the OpenAPI schema. Appends the generated C++
# sources to <name> and creates nanobind modules for Python bindings.
#
# Requires find_package(siesta) to have been called first.

function(siesta_generate)
	cmake_parse_arguments(SG "" "TARGET;SCHEMA;MODE" "" ${ARGN})

	if(NOT SG_TARGET)
		message(FATAL_ERROR "siesta_generate: TARGET is required")
	endif()
	if(NOT SG_SCHEMA)
		message(FATAL_ERROR "siesta_generate: SCHEMA is required")
	endif()
	if(NOT SG_MODE)
		set(SG_MODE BOTH)
	endif()
	if(NOT SG_MODE MATCHES "^(CLIENT|SERVER|BOTH)$")
		message(FATAL_ERROR "siesta_generate: MODE must be CLIENT, SERVER, or BOTH (got '${SG_MODE}')")
	endif()

	if(NOT TARGET siesta::siesta-generator)
		message(FATAL_ERROR "siesta_generate requires find_package(siesta)")
	endif()

	get_filename_component(_schema_name "${SG_SCHEMA}" NAME_WE)
	set(_gen_dir "${CMAKE_CURRENT_BINARY_DIR}/siesta_gen/${_schema_name}")

	set(_gen_mode "both")
	if(SG_MODE STREQUAL "CLIENT")
		set(_gen_mode "client")
	elseif(SG_MODE STREQUAL "SERVER")
		set(_gen_mode "server")
	else()
		set(_gen_mode "both")
	endif()

	set(_all_outputs
		"${_gen_dir}/openapi_defs.hpp"
		"${_gen_dir}/openapi_defs.cpp"
	)

	if(SG_MODE STREQUAL "CLIENT" OR SG_MODE STREQUAL "BOTH")
		list(APPEND _all_outputs "${_gen_dir}/client.hpp")
		list(APPEND _all_outputs "${_gen_dir}/py_module.cpp")
	endif()

	if(SG_MODE STREQUAL "SERVER" OR SG_MODE STREQUAL "BOTH")
		list(APPEND _all_outputs "${_gen_dir}/server.hpp")
		list(APPEND _all_outputs "${_gen_dir}/server.cpp")
		list(APPEND _all_outputs "${_gen_dir}/server_py.cpp")
	endif()

	list(APPEND _all_outputs "${_gen_dir}/siesta_info.cmake")

	add_custom_command(
		OUTPUT ${_all_outputs}
		COMMAND "$<TARGET_FILE:siesta::siesta-generator>"
			--input "${SG_SCHEMA}"
			--output "${_gen_dir}"
			--mode "${_gen_mode}"
		DEPENDS "${SG_SCHEMA}"
		COMMENT "Generating siesta stubs: ${_schema_name}"
	)

	add_custom_target(${SG_TARGET}_gen DEPENDS ${_all_outputs})

	# Static library for shared type definitions
	add_library(${SG_TARGET}_defs STATIC "${_gen_dir}/openapi_defs.cpp")
	target_include_directories(${SG_TARGET}_defs PUBLIC "${_gen_dir}")
	target_link_libraries(${SG_TARGET}_defs PUBLIC siesta::siesta)
	add_dependencies(${SG_TARGET}_defs ${SG_TARGET}_gen)

	# Wire into user's target
	target_include_directories(${SG_TARGET} PRIVATE "${_gen_dir}")
	target_link_libraries(${SG_TARGET} PUBLIC siesta::siesta ${SG_TARGET}_defs)

	if(SG_MODE STREQUAL "SERVER" OR SG_MODE STREQUAL "BOTH")
		target_sources(${SG_TARGET} PRIVATE "${_gen_dir}/server.cpp")
	endif()

	# Python bindings
	find_package(Python 3.10 REQUIRED COMPONENTS Interpreter Development)
	find_package(nanobind 2.12 REQUIRED)

	# Resolve the generator executable path at configure time
	get_target_property(_gen_exe siesta::siesta-generator IMPORTED_LOCATION)
	if(NOT _gen_exe)
		# Try config-specific property
		foreach(_cfg IN ITEMS RELEASE DEBUG RELWITHDEBINFO MINSIZEREL)
			get_target_property(_gen_exe siesta::siesta-generator "IMPORTED_LOCATION_${_cfg}")
			if(_gen_exe)
				break()
			endif()
		endforeach()
	endif()
	if(NOT _gen_exe)
		message(FATAL_ERROR "siesta_generate: could not locate siesta-generator executable")
	endif()

	# Query module names from the generator at configure time
	execute_process(
		COMMAND "${_gen_exe}"
			--input "${SG_SCHEMA}"
			--print-module-names
		OUTPUT_VARIABLE _module_names
		OUTPUT_STRIP_TRAILING_WHITESPACE
		RESULT_VARIABLE _mod_rc
	)
	if(NOT _mod_rc EQUAL 0)
		message(FATAL_ERROR "siesta_generate: failed to query module names")
	endif()
	string(REGEX MATCH "client=([^\n]+)" _match "${_module_names}")
	if(_match)
		set(SIESTA_CLIENT_MODULE "${CMAKE_MATCH_1}")
	endif()
	string(REGEX MATCH "server=([^\n]+)" _match "${_module_names}")
	if(_match)
		set(SIESTA_SERVER_MODULE "${CMAKE_MATCH_1}")
	endif()

	if(DEFINED SIESTA_CLIENT_MODULE AND (SG_MODE STREQUAL "CLIENT" OR SG_MODE STREQUAL "BOTH"))
		nanobind_add_module(${SIESTA_CLIENT_MODULE}
			"${_gen_dir}/py_module.cpp"
			"${_gen_dir}/openapi_defs.cpp"
		)
		target_compile_definitions(${SIESTA_CLIENT_MODULE} PRIVATE NB_DOMAIN=siesta)
		target_include_directories(${SIESTA_CLIENT_MODULE} PRIVATE "${_gen_dir}")
		target_link_libraries(${SIESTA_CLIENT_MODULE} PRIVATE siesta::siesta)
	endif()

	if(DEFINED SIESTA_SERVER_MODULE AND (SG_MODE STREQUAL "SERVER" OR SG_MODE STREQUAL "BOTH"))
		nanobind_add_module(${SIESTA_SERVER_MODULE}
			"${_gen_dir}/server_py.cpp"
			"${_gen_dir}/server.cpp"
			"${_gen_dir}/openapi_defs.cpp"
		)
		target_compile_definitions(${SIESTA_SERVER_MODULE} PRIVATE NB_DOMAIN=siesta)
		target_include_directories(${SIESTA_SERVER_MODULE} PRIVATE "${_gen_dir}")
		target_link_libraries(${SIESTA_SERVER_MODULE} PRIVATE siesta::siesta)
	endif()

endfunction()
