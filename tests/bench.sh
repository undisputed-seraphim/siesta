#!/bin/bash
# Thin wrapper around CMake benchmark/profile/valgrind custom targets.
#
# Usage:
#   ./bench.sh beast rest --bench         # benchmark REST via beast
#   ./bench.sh beast rpc_proto --profile  # profile RPC/proto via beast
#   ./bench.sh beast rest --valgrind      # valgrind REST via beast
#   ./bench.sh --asan                     # print ASan instructions
#
# Environment:
#   BUILD_DIR        path to CMake build directory (default: ../../build)
#   REQUESTS         request count for benchmarks
#   CONCURRENCY      connections for benchmarks
#   PROFILE_REQUESTS request count for profiling
#   VALGRIND_REQUESTS request count for valgrind
#   HEAP_INTERVAL    tcmalloc sample interval (bytes)

set -euo pipefail
BACKEND="${1:-beast}"
FORMAT="${2:-rest}"
MODE="${3:---bench}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD="${BUILD_DIR:-"$SCRIPT_DIR/../build"}"

target="${MODE#--}_${BACKEND}_${FORMAT}"

case "$MODE" in
	--bench|--profile|--valgrind|--profile-heap)
		cmake --build "$BUILD" --target "$target"
		;;
	--asan)
		cmake --build "$BUILD" --target asan
		;;
	*)
		echo "Usage: bench.sh <backend> <format> <mode>"
		echo "  backend: beast"
		echo "  format:  rest | rpc_proto | rpc_openrpc"
		echo "  mode:    --bench | --profile | --profile-heap | --valgrind | --asan"
		echo ""
		echo "Examples:"
		echo "  ./bench.sh beast rest --bench"
		echo "  REQUESTS=50000 ./bench.sh beast rest --bench"
		echo "  PROFILE_REQUESTS=20000 ./bench.sh beast rest --profile"
		exit 1
		;;
esac
