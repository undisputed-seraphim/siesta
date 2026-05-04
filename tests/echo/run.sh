#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Test harness for the echo integration test.
#
# Usage:
#   ./run.sh              # build + run all tests
#   ./run.sh --quick      # run only (assumes already built)
#   ./run.sh --server     # start server in foreground (for manual testing)
#
# Environment:
#   HOST, PORT       — override the listen address (default 127.0.0.1:9910)
#   SIESTA_PREFIX    — path to siesta install (default: ../../build)

set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD="$ROOT/build"
SERVE="${HOST:-127.0.0.1}"
PORT="${PORT:-9910}"
SIESTA_PREFIX="${SIESTA_PREFIX:-"$ROOT/../../build"}"

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

log()  { echo -e "[$(date +%H:%M:%S)] $*"; }
pass() { echo -e "${GREEN}PASS${NC} $*"; }
fail() { echo -e "${RED}FAIL${NC} $*"; }

ensure_build() {
	if [[ ! -d "$BUILD" ]] || [[ ! -f "$BUILD/build.ninja" ]]; then
		log "building echo project"
		mkdir -p "$BUILD"
		cmake -S "$ROOT" -B "$BUILD" \
			-DCMAKE_PREFIX_PATH="$SIESTA_PREFIX" \
			-GNinja >/dev/null
	fi
	ninja -C "$BUILD" echo_server Echo_API
}

kill_server() {
	local pid="$1"
	if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
		log "stopping test server (pid $pid)"
		kill "$pid" 2>/dev/null || true
		wait "$pid" 2>/dev/null || true
	fi
}

run_tests() {
	ensure_build

	log "starting test server on ${SERVE}:${PORT}"
	"$BUILD/echo_server" "$SERVE" "$PORT" &
	local srv_pid=$!
	trap "kill_server $srv_pid" EXIT

	sleep 0.5
	if ! kill -0 "$srv_pid" 2>/dev/null; then
		fail "test server failed to start"
		return 1
	fi

	log "running Python client tests"
	local out rc=0
	out=$(HOST="$SERVE" PORT="$PORT" python3 "$ROOT/test_client.py" 2>&1) || rc=$?

	echo "$out"

	if echo "$out" | grep -q "=== Result:.*0 failed"; then
		pass "all integration tests passed"
	else
		fail "some tests failed"
		rc=1
	fi

	kill_server "$srv_pid"
	trap - EXIT
	return "$rc"
}

case "${1:-}" in
	--quick)
		python3 "$ROOT/test_client.py"
		;;
	--server)
		ensure_build
		log "starting test server on ${SERVE}:${PORT}"
		exec "$BUILD/echo_server" "$SERVE" "$PORT"
		;;
	*)
		run_tests
		;;
esac
