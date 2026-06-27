# SPDX-License-Identifier: Apache-2.0
# Custom targets for running benchmarks.
# Usage:
#   REQUESTS=100000 CONCURRENCY=4 cmake --build build --target bench_beast_rest
#   cmake --build build --target bench
#
# Each {backend}{format} combination with a benchmark binary gets a target.

if(DEFINED ENV{REQUESTS})
	set(BM_REQUESTS "$ENV{REQUESTS}")
else()
	set(BM_REQUESTS 100000)
endif()
if(DEFINED ENV{CONCURRENCY})
	set(BM_CONCURRENCY "$ENV{CONCURRENCY}")
else()
	set(BM_CONCURRENCY 1)
endif()
set(BM_HOST "127.0.0.1")

# --- Beast REST (echo.json) ---
if(TARGET echo_beast_benchmark)
	add_custom_target(bench_beast_rest
		COMMAND $<TARGET_FILE:echo_beast_benchmark>
			--host ${BM_HOST} --port 19910
			--requests ${BM_REQUESTS} --concurrency ${BM_CONCURRENCY}
			--warmup 100
		DEPENDS echo_beast_benchmark
		WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
		COMMENT "Beast REST benchmark (${BM_REQUESTS} req, ${BM_CONCURRENCY} conn)"
	)
endif()

# --- Stub targets for future {backend}×{format} combinations ---
# When benchmark binaries are added, they get real targets here.
# The naming convention is: bench_{backend}_{format}
# where format ∈ {rest, rpc_proto, rpc_openrpc}

macro(add_bench_stub name)
	if(NOT TARGET bench_${name})
		add_custom_target(bench_${name}
			COMMAND ${CMAKE_COMMAND} -E echo "bench_${name}: not yet implemented"
		)
	endif()
endmacro()

add_bench_stub(beast_rpc_proto)
add_bench_stub(beast_rpc_openrpc)

# --- Aggregator ---
add_custom_target(bench
	COMMENT "Running all benchmark targets"
)

if(TARGET bench_beast_rest)
	add_dependencies(bench bench_beast_rest)
endif()
foreach(n IN ITEMS beast_rpc_proto beast_rpc_openrpc)
	if(TARGET bench_${n})
		add_dependencies(bench bench_${n})
	endif()
endforeach()
