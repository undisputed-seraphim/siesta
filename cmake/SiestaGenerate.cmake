# SPDX-License-Identifier: Apache-2.0
#
# siesta_generate(TARGET <name> SCHEMA <path> [MODE CLIENT|SERVER|BOTH] [NO_PYTHON])
#
# Runs siesta-generator on the OpenAPI schema. Appends the generated C++
# sources to <name> and creates nanobind modules for Python bindings.
#
# NO_PYTHON:        skip nanobind module generation and Python dependency checks
# REQUIRES:         find_package(siesta)

function(siesta_generate)
	cmake_parse_arguments(SG "NO_PYTHON" "TARGET;SCHEMA;MODE" "" ${ARGN})

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

	if(NOT TARGET siesta::siesta-generator AND NOT TARGET siesta-generator)
		message(FATAL_ERROR "siesta_generate requires the siesta-generator target")
	endif()

	# Use whichever target exists (in-tree: siesta-generator, installed: siesta::siesta-generator)
	if(TARGET siesta::siesta-generator)
		set(_gen_target siesta::siesta-generator)
	else()
		set(_gen_target siesta-generator)
	endif()
	if(TARGET siesta::siesta)
		set(_siesta_lib siesta::siesta)
	else()
		set(_siesta_lib siesta)
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
		if(NOT SG_NO_PYTHON)
			list(APPEND _all_outputs "${_gen_dir}/py_module.cpp")
		endif()
	endif()

	if(SG_MODE STREQUAL "SERVER" OR SG_MODE STREQUAL "BOTH")
		list(APPEND _all_outputs "${_gen_dir}/server.hpp")
		list(APPEND _all_outputs "${_gen_dir}/server.cpp")
		if(NOT SG_NO_PYTHON)
			list(APPEND _all_outputs "${_gen_dir}/server_py.cpp")
		endif()
	endif()

	if(NOT SG_NO_PYTHON)
		list(APPEND _all_outputs "${_gen_dir}/siesta_info.cmake")
	endif()

	set(_gen_args
		--input "${SG_SCHEMA}"
		--output "${_gen_dir}"
		--mode "${_gen_mode}"
	)
	if(SG_NO_PYTHON)
		list(APPEND _gen_args "--no-python")
	endif()

	add_custom_command(
		OUTPUT ${_all_outputs}
		COMMAND "$<TARGET_FILE:${_gen_target}>" ${_gen_args}
		DEPENDS "${SG_SCHEMA}"
		COMMENT "Generating siesta stubs: ${_schema_name}"
	)

	add_custom_target(${SG_TARGET}_gen DEPENDS ${_all_outputs})

	# Static library for shared type definitions
	add_library(${SG_TARGET}_defs STATIC "${_gen_dir}/openapi_defs.cpp")
	target_include_directories(${SG_TARGET}_defs PUBLIC "${_gen_dir}")
	target_link_libraries(${SG_TARGET}_defs PUBLIC ${_siesta_lib})
	add_dependencies(${SG_TARGET}_defs ${SG_TARGET}_gen)

	# Wire into user's target
	target_include_directories(${SG_TARGET} PRIVATE "${_gen_dir}")
	target_link_libraries(${SG_TARGET} PUBLIC ${_siesta_lib} ${SG_TARGET}_defs)

	if(SG_MODE STREQUAL "SERVER" OR SG_MODE STREQUAL "BOTH")
		target_sources(${SG_TARGET} PRIVATE "${_gen_dir}/server.cpp")
	endif()

	# Python bindings
	if(NOT SG_NO_PYTHON)
		if(NOT TARGET nanobind::nanobind)
			find_package(Python 3.10 REQUIRED COMPONENTS Interpreter Development)
			find_package(nanobind 2.12 REQUIRED)
		endif()

		# Resolve the generator executable path at configure time
		get_target_property(_gen_exe ${_gen_target} IMPORTED_LOCATION)
		if(NOT _gen_exe)
			foreach(_cfg IN ITEMS RELEASE DEBUG RELWITHDEBINFO MINSIZEREL)
				get_target_property(_gen_exe ${_gen_target} "IMPORTED_LOCATION_${_cfg}")
				if(_gen_exe)
					break()
				endif()
			endforeach()
		endif()
		# Fallback: for in-tree builds, look in the build tree
		if(NOT _gen_exe AND CMAKE_BINARY_DIR)
			set(_gen_exe "${CMAKE_BINARY_DIR}/generator/siesta-generator")
			if(NOT EXISTS "${_gen_exe}")
				set(_gen_exe "")
			endif()
		endif()
		if(NOT _gen_exe)
			message(FATAL_ERROR "siesta_generate: could not locate siesta-generator executable")
		endif()

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
			target_link_libraries(${SIESTA_CLIENT_MODULE} PRIVATE ${_siesta_lib})
		endif()

		if(DEFINED SIESTA_SERVER_MODULE AND (SG_MODE STREQUAL "SERVER" OR SG_MODE STREQUAL "BOTH"))
			nanobind_add_module(${SIESTA_SERVER_MODULE}
				"${_gen_dir}/server_py.cpp"
				"${_gen_dir}/server.cpp"
				"${_gen_dir}/openapi_defs.cpp"
			)
			target_compile_definitions(${SIESTA_SERVER_MODULE} PRIVATE NB_DOMAIN=siesta)
			target_include_directories(${SIESTA_SERVER_MODULE} PRIVATE "${_gen_dir}")
			target_link_libraries(${SIESTA_SERVER_MODULE} PRIVATE ${_siesta_lib})
		endif()
	endif()

endfunction()
