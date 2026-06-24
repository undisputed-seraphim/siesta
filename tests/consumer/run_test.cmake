# run_test.cmake — invoked by CTest
# Required variables: BUILD_DIR, SOURCE_DIR

if(NOT BUILD_DIR OR NOT SOURCE_DIR)
	message(FATAL_ERROR "BUILD_DIR and SOURCE_DIR must be set")
endif()

set(INSTALL_PREFIX "${BUILD_DIR}/_consumer_test/install")
set(CONSUMER_BUILD "${BUILD_DIR}/_consumer_test/build")

# Step 1: Install siesta to temp prefix
file(REMOVE_RECURSE "${INSTALL_PREFIX}")
execute_process(
	COMMAND ${CMAKE_COMMAND} --install "${BUILD_DIR}" --prefix "${INSTALL_PREFIX}"
	RESULT_VARIABLE rc
	OUTPUT_VARIABLE out
	ERROR_VARIABLE err
)
if(NOT rc EQUAL 0)
	message(FATAL_ERROR "Install failed:\n${out}\n${err}")
endif()

# Propagate sanitizer flags from parent build
file(STRINGS "${BUILD_DIR}/CMakeCache.txt" _asan_line REGEX "^SIESTA_ENABLE_ASAN:BOOL=ON$")
set(_extra_args "")
if(_asan_line)
	list(APPEND _extra_args "-DCMAKE_CXX_FLAGS=-fsanitize=address")
	list(APPEND _extra_args "-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address")
endif()

# Step 2: Configure consumer project
file(REMOVE_RECURSE "${CONSUMER_BUILD}")
execute_process(
	COMMAND ${CMAKE_COMMAND}
		-B "${CONSUMER_BUILD}"
		-S "${SOURCE_DIR}"
		-DCMAKE_PREFIX_PATH=${INSTALL_PREFIX}
		-DCMAKE_BUILD_TYPE=Release
		${_extra_args}
		-GNinja
	RESULT_VARIABLE rc
	OUTPUT_VARIABLE out
	ERROR_VARIABLE err
)
if(NOT rc EQUAL 0)
	message(FATAL_ERROR "Configure failed:\n${out}\n${err}")
endif()

# Step 3: Build consumer project
execute_process(
	COMMAND ${CMAKE_COMMAND} --build "${CONSUMER_BUILD}"
	RESULT_VARIABLE rc
	OUTPUT_VARIABLE out
	ERROR_VARIABLE err
)
if(NOT rc EQUAL 0)
	message(FATAL_ERROR "Build failed:\n${out}\n${err}")
endif()

# Step 4: Run smoke test
execute_process(
	COMMAND "${CONSUMER_BUILD}/smoke"
	RESULT_VARIABLE rc
	OUTPUT_VARIABLE out
	ERROR_VARIABLE err
	TIMEOUT 30
)
if(NOT rc EQUAL 0)
	message(FATAL_ERROR "Smoke test failed (rc=${rc}):\n${out}\n${err}")
endif()

message(STATUS "Consumer smoke test passed")
