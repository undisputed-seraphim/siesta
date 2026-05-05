#!/bin/bash
set -euo pipefail

# ==================================================================
#  Siesta Echo — Test Orchestrator
# ==================================================================
# Usage:
#   ./run.sh                    # sanity: build + C++ tests + Python tests
#   ./run.sh --quick            # run tests without rebuilding
#   ./run.sh --server           # start server in foreground (manual testing)
#   ./run.sh --bench            # bench build + load test
#   ./run.sh --profile          # profile build + load test + CPU report
#   ./run.sh --load             # load test only (no build / no profile)
#   ./run.sh --cpp              # C++ tests only
#   ./run.sh --py               # Python tests only
#
# Environment:
#   HOST, PORT        — server listen address (default 127.0.0.1:9910)
#   SIESTA_PREFIX     — path to siesta install (default: ../../build/install)
#   REQUESTS          — load-test request count (default: mode-dependent)
#   CONCURRENCY       — load-test workers    (default: mode-dependent)

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build"
SERVE="${HOST:-127.0.0.1}"
PORT="${PORT:-9910}"
SIESTA_PREFIX="${SIESTA_PREFIX:-"$ROOT/../build/install"}"

RED='\033[0;31m'; GREEN='\033[0;32m'; CYAN='\033[0;36m'; NC='\033[0m'
log()   { echo -e "[$(date +%H:%M:%S)] $*"; }
pass()  { echo -e "${GREEN}PASS${NC} $*"; }
fail()  { echo -e "${RED}FAIL${NC} $*"; }
info()  { echo -e "${CYAN}INFO${NC} $*"; }

# ── Help ──────────────────────────────────────────────────────

usage() {
	cat <<EOF
Usage: ./run.sh [MODE]

Modes:
  (default)       sanity: build + C++ tests + Python tests
  --quick         run tests without rebuilding
  --server        start server in foreground (manual testing)
  --bench         bench build + load test (100k req, 200 concurrency)
  --profile       profile build + load test + CPU report (50k req, 100 concurrent)
  --load          load test only (no build, no profile)
  --cpp           C++ tests only (build + run)
  --py            Python tests only (build + run)

Environment:
  HOST, PORT           server listen address (default $SERVE:$PORT)
  SIESTA_PREFIX        path to siesta install
  REQUESTS             load-test request count
  CONCURRENCY          load-test concurrency
EOF
	exit 0
}

# ── Build ──────────────────────────────────────────────────────

ensure_build() {
	local mode="$1"
	local cmake_args=(-S "$ROOT" -B "$BUILD"
		-DCMAKE_PREFIX_PATH="$SIESTA_PREFIX"
		-GNinja)

	if [[ ! -d "$BUILD" ]] || [[ ! -f "$BUILD/build.ninja" ]]; then
		log "configuring cmake"
		mkdir -p "$BUILD"
		cmake "${cmake_args[@]}" >/dev/null
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

# ── Test runners ───────────────────────────────────────────────

_export_env() {
	export ECHO_HOST="$SERVE"
	export ECHO_PORT="$PORT"
	export HOST="$SERVE"
	export PORT="$PORT"
}

run_cpp_tests() {
	_export_env
	log "running C++ integration tests"
	local out rc=0
	out=$("$BUILD/echo_test_client" 2>&1) || rc=$?
	echo "$out"
	if echo "$out" | grep -q "All tests passed"; then
		pass "C++ integration tests passed"
	else
		fail "C++ integration tests failed"
		rc=1
	fi
	return "$rc"
}

run_python_tests() {
	_export_env
	log "running Python client tests"
	local out rc=0
	out=$(python3 "$ROOT/echo/test_client.py" 2>&1) || rc=$?
	echo "$out"
	if echo "$out" | grep -q "=== Result:.*0 failed"; then
		pass "Python integration tests passed"
	else
		fail "Python tests failed"
		rc=1
	fi
	return "$rc"
}

run_load_test() {
	local py="$ROOT/echo/load_test/load_test.py"
	local extra_args=()
	[[ "${KEEPALIVE:-0}" -gt 0 ]] && extra_args+=(--keepalive "$KEEPALIVE")
	info "load test ($REQUESTS req, $CONCURRENCY concurrent)"
	python3 "$py" \
		--host "$SERVE" --port "$PORT" \
		--requests "$REQUESTS" --concurrency "$CONCURRENCY" \
		--warmup 50 \
		"${extra_args[@]}"
}

# ── Profile reports ────────────────────────────────────────────

generate_profile_report() {
	local binary="$BUILD/echo_server_prof"
	local prof_dir="$ROOT/echo/load_test/profiles"
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

mode_sanity() {
	ensure_build "sanity"
	build_target echo_server
	build_target Echo_API
	build_target echo_test_client

	local srv_pid
	if ! srv_pid=$(start_server "$BUILD/echo_server") || [[ -z "$srv_pid" ]]; then
		fail "could not start server"
		exit 1
	fi
	trap "kill_server $srv_pid" EXIT

	local failed=0
	run_cpp_tests || failed=1
	run_python_tests || failed=1

	kill_server "$srv_pid"
	trap - EXIT

	if [[ "$failed" -ne 0 ]]; then
		echo ""
		fail "some sanity tests failed"
		exit 1
	fi
}

mode_quick() {
	_export_env
	local failed=0
	"$BUILD/echo_test_client" 2>&1 || failed=1
	python3 "$ROOT/echo/test_client.py" 2>&1 || failed=1
	if [[ "$failed" -ne 0 ]]; then
		fail "some quick tests failed"
		exit 1
	fi
}

mode_server() {
	ensure_build "server"
	build_target echo_server
	log "starting echo server on ${SERVE}:${PORT}"
	exec "$BUILD/echo_server" "$SERVE" "$PORT"
}

mode_bench() {
	: "${REQUESTS:=100000}"
	: "${CONCURRENCY:=200}"
	ensure_build "bench"
	build_target echo_server_bench

	local srv_pid
	if ! srv_pid=$(start_server "$BUILD/echo_server_bench") || [[ -z "$srv_pid" ]]; then
		fail "could not start server"
		exit 1
	fi
	trap "kill_server $srv_pid" EXIT

	run_load_test || exit 1

	kill_server "$srv_pid"
	trap - EXIT
}

mode_profile() {
	: "${REQUESTS:=50000}"
	: "${CONCURRENCY:=100}"
	ensure_build "profile"
	build_target echo_server_prof

	local prof_dir="$ROOT/echo/load_test/profiles"
	rm -rf "$prof_dir"
	mkdir -p "$prof_dir"

	local srv_pid
	if ! srv_pid=$(start_server "$BUILD/echo_server_prof" \
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
	"$ROOT/echo/load_test/load_test.py" \
		--host "$SERVE" --port "$PORT" \
		--requests "${REQUESTS:-10000}" --concurrency "${CONCURRENCY:-50}" \
		--warmup 50
}

mode_cpp() {
	ensure_build "cpp"
	build_target echo_server
	build_target echo_test_client

	local srv_pid
	if ! srv_pid=$(start_server "$BUILD/echo_server") || [[ -z "$srv_pid" ]]; then
		fail "could not start server"
		exit 1
	fi
	trap "kill_server $srv_pid" EXIT

	run_cpp_tests || { kill_server "$srv_pid"; exit 1; }

	kill_server "$srv_pid"
	trap - EXIT
}

mode_py() {
	ensure_build "py"
	build_target echo_server
	build_target Echo_API

	local srv_pid
	if ! srv_pid=$(start_server "$BUILD/echo_server") || [[ -z "$srv_pid" ]]; then
		fail "could not start server"
		exit 1
	fi
	trap "kill_server $srv_pid" EXIT

	run_python_tests || { kill_server "$srv_pid"; exit 1; }

	kill_server "$srv_pid"
	trap - EXIT
}

# ── Main ───────────────────────────────────────────────────────

case "${1:-}" in
	--help|-h)   usage ;;
	--quick)     mode_quick ;;
	--server)    mode_server ;;
	--bench)     mode_bench ;;
	--profile)   mode_profile ;;
	--load)      mode_load ;;
	--cpp)       mode_cpp ;;
	--py)        mode_py ;;
	"")          mode_sanity ;;
	*)           echo "Unknown flag: $1"; usage ;;
esac
