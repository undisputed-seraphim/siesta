# SPDX-License-Identifier: Apache-2.0
# Custom targets for CPU and heap profiling.
# Requires gperftools (libprofiler). Output goes to build/profiles/.
#
# Usage:
#   cmake --build build --target profile_beast_rest
#   cmake --build build --target profile_heap_beast_rest
#   cmake --build build --target profile
#
# Environment:
#   PROFILE_REQUESTS    request count (default: 50000)
#   HEAP_INTERVAL       tcmalloc sample interval in bytes (default: 524288)

set(PROFILES_DIR "${CMAKE_BINARY_DIR}/profiles")

if(DEFINED ENV{PROFILE_REQUESTS})
	set(PROFILE_REQUESTS "$ENV{PROFILE_REQUESTS}")
else()
	set(PROFILE_REQUESTS 50000)
endif()
if(NOT DEFINED HEAP_INTERVAL)
	set(HEAP_INTERVAL 524288)
endif()

if(NOT PROFILER_LIB)
	add_custom_target(profile
		COMMAND ${CMAKE_COMMAND} -E echo "gperftools not found — profiling targets disabled"
	)
	return()
endif()

# --- Beast REST CPU profile (-O0, line-level) ---
if(TARGET echo_beast_server_prof AND TARGET echo_beast_benchmark)
	add_custom_target(profile_beast_rest
		COMMAND ${CMAKE_COMMAND} -E make_directory ${PROFILES_DIR}
		COMMAND ${CMAKE_COMMAND} -E env
			CPUPROFILE=${PROFILES_DIR}/cpu.prof
			CPUPROFILE_FREQUENCY=500
			$<TARGET_FILE:echo_beast_server_prof> 127.0.0.1 19911
			>/dev/null 2>&1 &
		COMMAND ${CMAKE_COMMAND} -E sleep 0.5
		COMMAND $<TARGET_FILE:echo_beast_benchmark>
			--host 127.0.0.1 --port 19911
			--requests ${PROFILE_REQUESTS} --concurrency 1 --warmup 100
		COMMAND pkill -INT -f echo_beast_server_prof 2>/dev/null || true
		DEPENDS echo_beast_server_prof echo_beast_benchmark
		WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
		COMMENT "CPU profile beast/rest → ${PROFILES_DIR}/cpu.prof"
	)
endif()

# --- Beast REST heap profile (tcmalloc) ---
find_library(TCMALLOC_LIB tcmalloc)
if(TCMALLOC_LIB AND TARGET echo_beast_server AND TARGET echo_beast_benchmark)
	add_custom_target(profile_heap_beast_rest
		COMMAND ${CMAKE_COMMAND} -E make_directory ${PROFILES_DIR}
		COMMAND ${CMAKE_COMMAND} -E env
			LD_PRELOAD=${TCMALLOC_LIB}
			HEAPPROFILE=${PROFILES_DIR}/heap
			HEAP_PROFILE_ALLOCATION_INTERVAL=${HEAP_INTERVAL}
			$<TARGET_FILE:echo_beast_server> 127.0.0.1 19912
			>${PROFILES_DIR}/heap_stderr.log 2>&1 &
		COMMAND ${CMAKE_COMMAND} -E sleep 0.5
		COMMAND $<TARGET_FILE:echo_beast_benchmark>
			--host 127.0.0.1 --port 19912
			--requests ${PROFILE_REQUESTS} --concurrency 1 --warmup 100
		COMMAND pkill -INT -f echo_beast_server 2>/dev/null || true
		DEPENDS echo_beast_server echo_beast_benchmark
		WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
		COMMENT "Heap profile beast/rest → ${PROFILES_DIR}/heap.*.heap"
	)
endif()

# --- Aggregator ---
add_custom_target(profile
	COMMENT "Running profiling targets"
)
if(TARGET profile_beast_rest)
	add_dependencies(profile profile_beast_rest)
endif()
if(TARGET profile_heap_beast_rest)
	add_dependencies(profile profile_heap_beast_rest)
endif()
