#!/bin/bash
set -euo pipefail

# ==================================================================
#  Siesta Echo — Beast Backend Benchmark & Profiling
# ==================================================================
# Usage:
#   ./benchmark_beast.sh --bench       # C++ benchmark with embedded server
#   ./benchmark_beast.sh --profile     # CPU profile with -O0 server
#   ./benchmark_beast.sh --profile-o2  # CPU profile with -O2 server
#   ./benchmark_beast.sh --server      # start server in foreground
#   ./benchmark_beast.sh --load        # Python load test (assumes server running)
#
# Environment:
#   BUILD_DIR         — path to CMake build directory (default: ../../build)
#   HOST, PORT        — server listen address (default 127.0.0.1:9910)
#   REQUESTS          — request count (default: mode-dependent)
#   CONCURRENCY       — connections (default: 1)

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD="${BUILD_DIR:-"$PROJECT_ROOT/build"}"
SERVE="${HOST:-127.0.0.1}"
PORT="${PORT:-9910}"

RED='\033[0;31m'; CYAN='\033[0;36m'; NC='\033[0m'
log()   { echo -e "[$(date +%H:%M:%S)] $*"; }
fail()  { echo -e "${RED}FAIL${NC} $*"; }
info()  { echo -e "${CYAN}INFO${NC} $*"; }

SRV_PID=""
cleanup() {
	if [[ -n "$SRV_PID" ]] && kill -0 "$SRV_PID" 2>/dev/null; then
		log "cleanup: stopping server (pid $SRV_PID)"
		kill -INT "$SRV_PID" 2>/dev/null || true
		for _ in $(seq 1 20); do
			kill -0 "$SRV_PID" 2>/dev/null || break
			sleep 0.2
		done
		kill -9 "$SRV_PID" 2>/dev/null || true
	fi
	SRV_PID=""
}
trap cleanup EXIT

# ── Help ──────────────────────────────────────────────────────

usage() {
	cat <<EOF
Usage: ./benchmark_beast.sh [MODE]

Modes:
  --bench         C++ benchmark with embedded server (default: 100k req, 1 conn)
  --profile       CPU profile with -O0 server (line-level, 50k req)
  --profile-o2    CPU profile with -O2 server (function-level, 50k req)
  --heap-profile  Heap allocation profile (50k req, gperftools tcmalloc)
  --valgrind      Run short benchmark under valgrind memcheck (1k req)
  --server        start server in foreground (manual testing)
  --load          Python load test only (assumes server running)

Environment:
  BUILD_DIR            path to CMake build directory (default: $BUILD)
  HOST, PORT           server listen address (default $SERVE:$PORT)
  REQUESTS             request count
  CONCURRENCY          connections (default: 1)
EOF
	exit 0
}

# ── Build ──────────────────────────────────────────────────────

require_binary() {
	local bin="$1"
	local target="$2"
	if [[ ! -x "$bin" ]]; then
		fail "$bin not found. Build it first:"
		echo "  ninja -C $BUILD $target"
		exit 1
	fi
}

build_target() {
	local target="$1"
	log "building $target"
	ninja -C "$BUILD" "$target"
}

# ── Server lifecycle ───────────────────────────────────────────

start_server() {
	local server_bin="$1"
	local tls_flag=""
	if [[ "${TLS:-}" == "1" ]]; then tls_flag="--tls"; fi
	log "starting echo server on ${SERVE}:${PORT}"
	"$server_bin" "$SERVE" "$PORT" $tls_flag >/dev/null 2>&1 &
	SRV_PID=$!

	for _ in $(seq 1 10); do
		if python3 -c "import socket; s=socket.socket(); s.settimeout(1); s.connect(('${SERVE}',${PORT})); s.close()" 2>/dev/null; then
			return
		fi
		sleep 0.3
	done
	fail "server did not start"
	SRV_PID=""
	return 1
}

start_profiled_server() {
	local server_bin="$1"
	local prof_file="$2"
	local tls_flag=""
	if [[ "${TLS:-}" == "1" ]]; then tls_flag="--tls"; fi
	log "starting profiled server on ${SERVE}:${PORT}"
	CPUPROFILE="$prof_file" CPUPROFILE_FREQUENCY=500 \
		"$server_bin" "$SERVE" "$PORT" $tls_flag >/dev/null 2>&1 &
	SRV_PID=$!

	for _ in $(seq 1 10); do
		if python3 -c "import socket; s=socket.socket(); s.settimeout(1); s.connect(('${SERVE}',${PORT})); s.close()" 2>/dev/null; then
			return
		fi
		sleep 0.3
	done
	fail "profiled server did not start"
	SRV_PID=""
	return 1
}

stop_server() {
	if [[ -z "$SRV_PID" ]]; then return; fi
	if ! kill -0 "$SRV_PID" 2>/dev/null; then SRV_PID=""; return; fi

	log "stopping server (pid $SRV_PID)"
	kill -INT "$SRV_PID" 2>/dev/null || true
	for _ in $(seq 1 20); do
		kill -0 "$SRV_PID" 2>/dev/null || break
		sleep 0.2
	done
	kill -9 "$SRV_PID" 2>/dev/null || true
	SRV_PID=""
}

# ── Benchmark / Profile helpers ────────────────────────────────

run_benchmark_traffic() {
	local bench_bin="$BUILD/tests/echo_beast_benchmark"
	require_binary "$bench_bin" echo_beast_benchmark
	local tls_flag=""
	if [[ "${TLS:-}" == "1" ]]; then tls_flag="--tls"; fi
	"$bench_bin" \
		--host "$SERVE" --port "$PORT" \
		--requests "${REQUESTS}" --concurrency "${CONCURRENCY:-1}" \
		--warmup 100 $tls_flag
}

generate_profile_report() {
	local binary="$1"
	local prof_dir="$SCRIPT_DIR/load_test/profiles"
	local prof_file="$prof_dir/cpu.prof"

	if [[ ! -f "$prof_file" ]]; then
		info "no profile data found"
		return
	fi

	log "generating CPU profile reports"
	google-pprof --text "$binary" "$prof_file" \
		> "$prof_dir/cpu_text.txt" 2>/dev/null
	google-pprof --dot "$binary" "$prof_file" \
		> "$prof_dir/cpu_graph.dot" 2>/dev/null
	google-pprof --text "$binary" "$prof_file" \
		2>/dev/null | head -30 > "$prof_dir/cpu_top.txt"
	echo ""
	info "CPU profile — top 15 functions:"
	head -16 "$prof_dir/cpu_top.txt"
	echo ""
	info "Full reports: $prof_dir/cpu_text.txt"
	info "Dot graph:     $prof_dir/cpu_graph.dot"
}

# ── Modes ──────────────────────────────────────────────────────

mode_server() {
	local bin="$BUILD/tests/echo_beast_server"
	build_target echo_beast_server
	require_binary "$bin" echo_beast_server
	local tls_flag=""
	if [[ "${TLS:-}" == "1" ]]; then tls_flag="--tls"; fi
	log "starting echo server on ${SERVE}:${PORT} (foreground)"
	exec "$bin" "$SERVE" "$PORT" $tls_flag
}

mode_bench() {
	local bin="$BUILD/tests/echo_beast_benchmark"
	build_target echo_beast_benchmark
	require_binary "$bin" echo_beast_benchmark
	local tls_flag=""
	if [[ "${TLS:-}" == "1" ]]; then tls_flag="--tls"; fi
	"$bin" \
		--requests "${REQUESTS:-100000}" \
		--concurrency "${CONCURRENCY:-1}" \
		--warmup 100 \
		--port "$PORT" \
		$tls_flag
}

run_profile() {
	local bin="$1"
	local target="$2"
	: "${REQUESTS:=50000}"

	require_binary "$bin" "$target"

	local prof_dir="$SCRIPT_DIR/load_test/profiles"
	rm -rf "$prof_dir"
	mkdir -p "$prof_dir"

	start_profiled_server "$bin" "$prof_dir/cpu.prof"
	run_benchmark_traffic
	stop_server
	generate_profile_report "$bin"
}

mode_profile() {
	run_profile "$BUILD/tests/echo_beast_server_prof" echo_beast_server_prof
}

mode_profile_o2() {
	run_profile "$BUILD/tests/echo_beast_server_prof_o2" echo_beast_server_prof_o2
}

mode_load() {
	"$SCRIPT_DIR/load_test/load_test.py" \
		--host "$SERVE" --port "$PORT" \
		--requests "${REQUESTS:-10000}" --concurrency "${CONCURRENCY:-50}" \
		--warmup 50
}

mode_heap_profile() {
	local bin="$BUILD/tests/echo_beast_server"
	build_target echo_beast_server
	require_binary "$bin" echo_beast_server
	: "${REQUESTS:=50000}"

	local prof_dir="$SCRIPT_DIR/load_test/profiles"
	rm -rf "$prof_dir"
	mkdir -p "$prof_dir"

	local tls_flag=""
	if [[ "${TLS:-}" == "1" ]]; then tls_flag="--tls"; fi

	local tcmalloc_lib
	tcmalloc_lib=$(find /usr/lib -name 'libtcmalloc.so.4' 2>/dev/null | head -1)
	if [[ -z "$tcmalloc_lib" ]]; then
		fail "libtcmalloc.so.4 not found — install libgoogle-perftools-dev"
		return 1
	fi

	log "starting heap-profiled server on ${SERVE}:${PORT}"
	LD_PRELOAD="$tcmalloc_lib" \
		HEAPPROFILE="$prof_dir/heap" \
		HEAP_PROFILE_ALLOCATION_INTERVAL=$((512*1024)) \
		"$bin" "$SERVE" "$PORT" $tls_flag 2>"$prof_dir/heap_stderr.log" &
	SRV_PID=$!

	for _ in $(seq 1 10); do
		if python3 -c "import socket; s=socket.socket(); s.settimeout(1); s.connect(('${SERVE}',${PORT})); s.close()" 2>/dev/null; then
			break
		fi
		sleep 0.3
	done

	run_benchmark_traffic
	stop_server

	local latest_heap
	latest_heap=$(ls -t "$prof_dir"/heap.*.heap 2>/dev/null | head -1)
	if [[ -z "$latest_heap" ]]; then
		info "no heap profile data found"
		cat "$prof_dir/heap_stderr.log" 2>/dev/null
		return
	fi

	log "generating heap profile reports"
	google-pprof --text "$bin" "$latest_heap" \
		> "$prof_dir/heap_text.txt" 2>/dev/null
	google-pprof --text "$bin" "$latest_heap" \
		2>/dev/null | head -30 > "$prof_dir/heap_top.txt"
	echo ""
	info "Heap profile — top 15 allocation sites:"
	head -16 "$prof_dir/heap_top.txt"
	echo ""
	info "Full report: $prof_dir/heap_text.txt"
	info "Heap dumps: $(ls "$prof_dir"/heap.*.heap 2>/dev/null | wc -l) snapshots"
}

mode_valgrind() {
	local bin="$BUILD/tests/echo_beast_benchmark"
	build_target echo_beast_benchmark
	require_binary "$bin" echo_beast_benchmark

	local tls_flag=""
	if [[ "${TLS:-}" == "1" ]]; then tls_flag="--tls"; fi

	log "running valgrind memcheck (${REQUESTS:-1000} requests)"
	valgrind \
		--leak-check=full \
		--show-leak-kinds=all \
		--track-origins=yes \
		--error-exitcode=1 \
		"$bin" \
		--requests "${REQUESTS:-1000}" \
		--concurrency 1 \
		--warmup 10 \
		--port "$PORT" \
		$tls_flag
}

# ── Main ───────────────────────────────────────────────────────

case "${1:-}" in
	--help|-h)      usage ;;
	--server)       mode_server ;;
	--bench)        mode_bench ;;
	--profile)      mode_profile ;;
	--profile-o2)   mode_profile_o2 ;;
	--heap-profile) mode_heap_profile ;;
	--valgrind)     mode_valgrind ;;
	--load)         mode_load ;;
	"")             usage ;;
	*)              echo "Unknown flag: $1"; usage ;;
esac
