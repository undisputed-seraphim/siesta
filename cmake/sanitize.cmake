# SPDX-License-Identifier: Apache-2.0
# Custom targets for sanitizer and memory-checking runs.
#
# Usage:
#   cmake --build build --target valgrind_beast_rest
#   cmake --build build --target valgrind
#   cmake --build build --target asan
#
# Environment:
#   VALGRIND_REQUESTS    request count (default: 1000)

if(DEFINED ENV{VALGRIND_REQUESTS})
	set(VALGRIND_REQUESTS "$ENV{VALGRIND_REQUESTS}")
else()
	set(VALGRIND_REQUESTS 1000)
endif()

# --- Valgrind ---
find_program(VALGRIND_EXE valgrind)
if(VALGRIND_EXE AND TARGET echo_beast_benchmark)
	add_custom_target(valgrind_beast_rest
		COMMAND ${VALGRIND_EXE}
			--leak-check=full
			--show-leak-kinds=all
			--track-origins=yes
			--error-exitcode=1
			$<TARGET_FILE:echo_beast_benchmark>
			--requests ${VALGRIND_REQUESTS}
			--concurrency 1
			--warmup 10
		DEPENDS echo_beast_benchmark
		COMMENT "Valgrind beast/rest (${VALGRIND_REQUESTS} req)"
	)

	add_custom_target(valgrind
		COMMENT "Running valgrind targets"
	)
	add_dependencies(valgrind valgrind_beast_rest)
else()
	add_custom_target(valgrind
		COMMAND ${CMAKE_COMMAND} -E echo "valgrind not found"
	)
endif()

# --- AddressSanitizer ---
add_custom_target(asan
	COMMAND ${CMAKE_COMMAND} -E echo "ASan: rebuild with -DSIESTA_ENABLE_ASAN=ON and run ctest"
)
