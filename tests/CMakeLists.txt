cmake_minimum_required(VERSION 3.31)
project(siesta_tests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# -- Dependencies --------------------------------------------------
if(NOT TARGET Catch2::Catch2)
	find_package(Catch2 3.7 REQUIRED)
endif()
message(STATUS "Catch2 ${Catch2_VERSION}")

if(NOT TARGET nanobind::nanobind)
	find_package(Python 3.10 REQUIRED COMPONENTS Interpreter Development)
	find_package(nanobind 2.12 REQUIRED)
endif()

if(NOT TARGET siesta)
	find_package(siesta REQUIRED)
endif()

if(NOT COMMAND siesta_generate)
	include(SiestaGenerate)
endif()

include(CTest)
include(Catch)
enable_testing()

# ══════════════════════════════════════════════════════════════════
#  Generated code library
# ══════════════════════════════════════════════════════════════════

add_library(echo_gen STATIC EXCLUDE_FROM_ALL)
siesta_generate(
	TARGET echo_gen
	SCHEMA "${CMAKE_CURRENT_SOURCE_DIR}/echo.json"
	MODE BOTH
	BACKEND beast
	MODULE_NAME Echo_API
)
set_target_properties(Echo_API PROPERTIES EXCLUDE_FROM_ALL TRUE)
set_target_properties(Echo_API_server PROPERTIES EXCLUDE_FROM_ALL TRUE)

# ══════════════════════════════════════════════════════════════════
#  Flag Buckets
# ══════════════════════════════════════════════════════════════════

# -- Bucket A: Release (sanity-check / integration-test default)
set(FLAGS_RELEASE
	-O2 -g -DNDEBUG
)

# -- Bucket B: Debug / Profiling
set(FLAGS_PROFILING
	-O0 -g -fno-omit-frame-pointer
)

# -- Bucket C: Benchmark / Performance
set(FLAGS_BENCHMARK
	-O3 -DNDEBUG
	-march=native -mtune=native
	-flto=auto
	-fno-semantic-interposition
	-finline-functions
	--param inline-unit-growth=200
)

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

# ══════════════════════════════════════════════════════════════════
#  Beast server targets (same source, different flag sets)
# ══════════════════════════════════════════════════════════════════

set(BEAST_SERVER_SRC "${CMAKE_CURRENT_SOURCE_DIR}/echo/test_beast_server.cpp")

# -- echo_beast_server (release) -----------------------------------
add_executable(echo_beast_server ${BEAST_SERVER_SRC})
target_link_libraries(echo_beast_server PRIVATE echo_gen)
target_compile_options(echo_beast_server PRIVATE ${FLAGS_RELEASE})
target_link_options(echo_beast_server PRIVATE ${LINK_FLAGS_RELEASE})

# -- echo_beast_server_prof (debug + profiling, optional) ----------
find_library(PROFILER_LIB profiler)
if(PROFILER_LIB)
	add_executable(echo_beast_server_prof ${BEAST_SERVER_SRC})
	target_link_libraries(echo_beast_server_prof PRIVATE echo_gen)
	target_compile_definitions(echo_beast_server_prof PRIVATE ENABLE_PROFILER)
	target_compile_options(echo_beast_server_prof PRIVATE ${FLAGS_PROFILING})
	target_link_options(echo_beast_server_prof PRIVATE ${LINK_FLAGS_PROFILING})
	target_link_libraries(echo_beast_server_prof PRIVATE ${PROFILER_LIB})
else()
	message(STATUS "gperftools not found — echo_beast_server_prof target disabled")
endif()

# -- echo_beast_server_bench (max performance) ---------------------
add_executable(echo_beast_server_bench ${BEAST_SERVER_SRC})
target_link_libraries(echo_beast_server_bench PRIVATE echo_gen)
target_compile_options(echo_beast_server_bench PRIVATE ${FLAGS_BENCHMARK})
target_link_options(echo_beast_server_bench PRIVATE ${LINK_FLAGS_BENCHMARK})

# ══════════════════════════════════════════════════════════════════
#  Beast self-contained integration test (embedded server + client)
# ══════════════════════════════════════════════════════════════════

add_executable(echo_beast_integration
	"${CMAKE_CURRENT_SOURCE_DIR}/echo/test_beast_integration.cpp")
target_link_libraries(echo_beast_integration PRIVATE echo_gen siesta::siesta Catch2::Catch2WithMain)
target_compile_options(echo_beast_integration PRIVATE ${FLAGS_RELEASE})
catch_discover_tests(echo_beast_integration)

# ══════════════════════════════════════════════════════════════════
#  Beast benchmark (embedded server + async client, Release-optimized)
# ══════════════════════════════════════════════════════════════════

add_executable(echo_beast_benchmark
	"${CMAKE_CURRENT_SOURCE_DIR}/echo/benchmark_beast.cpp")
target_link_libraries(echo_beast_benchmark PRIVATE echo_gen siesta::siesta)
target_compile_options(echo_beast_benchmark PRIVATE -O2 -DNDEBUG)

# ══════════════════════════════════════════════════════════════════
#  Library unit tests
# ══════════════════════════════════════════════════════════════════

file(GLOB_RECURSE test_srcs "${CMAKE_CURRENT_SOURCE_DIR}/siesta/*")
add_executable(siesta_test ${test_srcs})
target_include_directories(siesta_test PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(siesta_test
	PRIVATE
	siesta::siesta
	Catch2::Catch2WithMain
)
catch_discover_tests(siesta_test)
