#!/bin/bash
set -euo pipefail

# ==================================================================
#  Siesta Echo — Beast Backend Benchmark & Profiling
# ==================================================================
# Usage:
#   ./benchmark_beast.sh --bench     # load test with max-performance build
#   ./benchmark_beast.sh --profile   # load test + CPU profile report
#   ./benchmark_beast.sh --server    # start server in foreground (manual testing)
#   ./benchmark_beast.sh --load      # load test only (assumes server running)
#
# Environment:
#   BUILD_DIR         — path to CMake build directory (default: ../../build)
#   HOST, PORT        — server listen address (default 127.0.0.1:9910)
#   REQUESTS          — load-test request count (default: mode-dependent)
#   CONCURRENCY       — load-test workers    (default: mode-dependent)

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD="${BUILD_DIR:-"$PROJECT_ROOT/build"}"
SERVE="${HOST:-127.0.0.1}"
PORT="${PORT:-9910}"

RED='\033[0;31m'; GREEN='\033[0;32m'; CYAN='\033[0;36m'; NC='\033[0m'
log()   { echo -e "[$(date +%H:%M:%S)] $*"; }
fail()  { echo -e "${RED}FAIL${NC} $*"; }
info()  { echo -e "${CYAN}INFO${NC} $*"; }

# ── Help ──────────────────────────────────────────────────────

usage() {
	cat <<EOF
Usage: ./benchmark_beast.sh [MODE]

Modes:
  --bench         load test with max-performance build (100k req, 200 concurrency)
  --profile       load test + CPU profile report (50k req, 100 concurrent)
  --server        start server in foreground (manual testing)
  --load          load test only (assumes server running)

Environment:
  BUILD_DIR            path to CMake build directory (default: $BUILD)
  HOST, PORT           server listen address (default $SERVE:$PORT)
  REQUESTS             load-test request count
  CONCURRENCY          load-test concurrency
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
	local server_bin="$1"; shift
	local env_vars=("$@")

	log "starting echo server on ${SERVE}:${PORT}"
	if [[ ${#env_vars[@]} -gt 0 ]]; then
		env "${env_vars[@]}" "$server_bin" "$SERVE" "$PORT" >/dev/null 2>&1 &
	else
		"$server_bin" "$SERVE" "$PORT" >/dev/null 2>&1 &
	fi
	local pid=$!
	echo "$pid"

	for i in $(seq 1 10); do
		if python3 -c "import socket; s=socket.socket(); s.settimeout(1); s.connect(('${SERVE}',${PORT})); s.close()" 2>/dev/null; then
			return
		fi
		sleep 0.3
	done
	echo ""
	return 1
}

kill_server() {
	local pid="${1:-}"
	if [[ -z "$pid" ]]; then return; fi
	if ! kill -0 "$pid" 2>/dev/null; then return; fi

	log "stopping server (pid $pid)"
	kill -INT "$pid" 2>/dev/null || true
	for i in $(seq 1 20); do
		if ! kill -0 "$pid" 2>/dev/null; then
			return 0
		fi
		sleep 0.2
	done
	kill -9 "$pid" 2>/dev/null || true
}

# ── Benchmark helpers ──────────────────────────────────────────

run_load_test() {
	local py="$SCRIPT_DIR/load_test/load_test.py"
	info "load test ($REQUESTS req, $CONCURRENCY concurrent)"
	python3 "$py" \
		--host "$SERVE" --port "$PORT" \
		--requests "$REQUESTS" --concurrency "$CONCURRENCY" \
		--warmup 50
}

generate_profile_report() {
	local binary="$BUILD/tests/echo_beast_server_prof"
	local prof_dir="$SCRIPT_DIR/load_test/profiles"
	local prof_file="$prof_dir/cpu.prof"

	if [[ ! -f "$prof_file" ]]; then
		info "no profile data found"
		return
	fi

	rm -rf "$prof_dir"
	mkdir -p "$prof_dir"

	log "generating CPU profile reports"
	google-pprof --text --lines "$binary" "$prof_file" \
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
	log "starting echo server on ${SERVE}:${PORT}"
	exec "$bin" "$SERVE" "$PORT"
}

mode_bench() {
	local bin="$BUILD/tests/echo_beast_benchmark"
	build_target echo_beast_benchmark
	require_binary "$bin" echo_beast_benchmark

	"$bin" \
		--requests "${REQUESTS:-100000}" \
		--concurrency "${CONCURRENCY:-1}" \
		--warmup 100 \
		--port "$PORT"
}

mode_profile() {
	: "${REQUESTS:=50000}"
	: "${CONCURRENCY:=100}"

	local bin="$BUILD/tests/echo_beast_server_prof"
	require_binary "$bin" echo_beast_server_prof

	local prof_dir="$SCRIPT_DIR/load_test/profiles"
	rm -rf "$prof_dir"
	mkdir -p "$prof_dir"

	local srv_pid
	if ! srv_pid=$(start_server "$bin" \
		CPUPROFILE="$prof_dir/cpu.prof" \
		CPUPROFILE_FREQUENCY=500) || [[ -z "$srv_pid" ]]; then
		fail "could not start server"
		exit 1
	fi
	trap "kill_server $srv_pid" EXIT

	run_load_test || exit 1

	kill_server "$srv_pid"
	trap - EXIT

	generate_profile_report
}

mode_load() {
	"$SCRIPT_DIR/load_test/load_test.py" \
		--host "$SERVE" --port "$PORT" \
		--requests "${REQUESTS:-10000}" --concurrency "${CONCURRENCY:-50}" \
		--warmup 50
}

# ── Main ───────────────────────────────────────────────────────

case "${1:-}" in
	--help|-h)   usage ;;
	--server)    mode_server ;;
	--bench)     mode_bench ;;
	--profile)   mode_profile ;;
	--load)      mode_load ;;
	"")          usage ;;
	*)           echo "Unknown flag: $1"; usage ;;
esac
