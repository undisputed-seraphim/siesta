cmake_minimum_required(VERSION 3.31)
project(echo LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT TARGET siesta)
	find_package(siesta REQUIRED)
endif()

# ── Shared generated-code lib ──────────────────────────────────

add_library(echo_gen STATIC)
siesta_generate(
	TARGET echo_gen
	SCHEMA "${CMAKE_CURRENT_SOURCE_DIR}/../echo.json"
	MODE BOTH
)

# ═══════════════════════════════════════════════════════════════
#  Flag Buckets
# ═══════════════════════════════════════════════════════════════

# ── Bucket A: Release (sanity-check / integration-test default)
#    Balanced: symbols for meaningful backtraces, moderate opt.
# ──────────────────────────────────────────────────────────────
set(FLAGS_RELEASE
	-O2 -g -DNDEBUG
)

# ── Bucket B: Debug / Profiling
#    No inlining, frame pointers, profiler hooks.
#    Used with: gperftools, perf record, heaptrack, valgrind.
# ──────────────────────────────────────────────────────────────
set(FLAGS_PROFILING
	-O0 -g -fno-omit-frame-pointer
)

# ── Bucket C: Benchmark / Performance
#    Max optimisation.  LTO, native arch, ICF, no interposition.
#    Not recommended for profiling — inlined frames blur stacks.
# ──────────────────────────────────────────────────────────────
set(FLAGS_BENCHMARK
	-O3 -DNDEBUG
	-march=native -mtune=native
	-flto=auto
	-fno-semantic-interposition
	-finline-functions
	--param inline-unit-growth=200
)

# ── Linker flags shared by all buckets ────────────────────────

set(LINK_FLAGS_RELEASE "")
set(LINK_FLAGS_PROFILING "")

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
	set(LINK_FLAGS_BENCHMARK
		-flto=auto
		-Wl,-O2
		-Wl,--gc-sections
	)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
	set(LINK_FLAGS_BENCHMARK
		-flto=auto
		-Wl,--icf=all
		-Wl,-O2
	)
endif()

# ═══════════════════════════════════════════════════════════════
#  Targets
# ═══════════════════════════════════════════════════════════════

# ── echo_server (release, default) ────────────────────────────
add_executable(echo_server "${CMAKE_CURRENT_SOURCE_DIR}/test_server.cpp")
target_link_libraries(echo_server PRIVATE echo_gen)
target_compile_options(echo_server PRIVATE ${FLAGS_RELEASE})
target_link_options(echo_server PRIVATE ${LINK_FLAGS_RELEASE})

# ── echo_server_prof (debug + profiling) ──────────────────────
add_executable(echo_server_prof "${CMAKE_CURRENT_SOURCE_DIR}/test_server.cpp")
target_link_libraries(echo_server_prof PRIVATE echo_gen)
target_compile_definitions(echo_server_prof PRIVATE ENABLE_PROFILER)
target_compile_options(echo_server_prof PRIVATE ${FLAGS_PROFILING})
target_link_options(echo_server_prof PRIVATE ${LINK_FLAGS_PROFILING})
find_library(PROFILER_LIB profiler REQUIRED)
target_link_libraries(echo_server_prof PRIVATE ${PROFILER_LIB})

# ── echo_server_bench (max performance) ───────────────────────
add_executable(echo_server_bench "${CMAKE_CURRENT_SOURCE_DIR}/test_server.cpp")
target_link_libraries(echo_server_bench PRIVATE echo_gen)
target_compile_options(echo_server_bench PRIVATE ${FLAGS_BENCHMARK})
target_link_options(echo_server_bench PRIVATE ${LINK_FLAGS_BENCHMARK})
