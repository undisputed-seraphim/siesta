#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Test harness for the echo sample project.
#
# Usage:
#   ./test/run.sh           # build + run all tests
#   ./test/run.sh --quick   # run only (assumes already built)
#   ./test/run.sh --server  # start server in foreground (for manual testing)
#
# Environment:
#   HOST, PORT  — override the listen address (default 127.0.0.1:9910)

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build"
SERVE="${HOST:-127.0.0.1}"
PORT="${PORT:-9910}"

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

log()  { echo -e "[$(date +%H:%M:%S)] $*"; }
pass() { echo -e "${GREEN}PASS${NC} $*"; }
fail() { echo -e "${RED}FAIL${NC} $*"; }

ensure_build() {
    local src_time=0 bin_time=0
    for f in "$ROOT"/server.cpp "$ROOT"/server_py.cpp "$ROOT"/py_module.cpp "$ROOT"/test/test_server.cpp; do
        [[ -f "$f" ]] && src_time=$(stat -c %Y "$f" 2>/dev/null || echo 0)
        [[ $src_time -gt $bin_time ]] && bin_time=0
    done
    local ts_bin="$BUILD/test_server"
    if [[ -f "$ts_bin" ]]; then
        bin_time=$(stat -c %Y "$ts_bin" 2>/dev/null || echo 0)
    fi
    if [[ $bin_time -lt $src_time ]]; then
        log "building echo project (${BUILD})"
        mkdir -p "$BUILD"
        cmake -S "$ROOT" -B "$BUILD" -DSIESTA_ROOT="$ROOT/.." -GNinja >/dev/null
        ninja -C "$BUILD" test_server Echo_API
    else
        log "build is up to date"
    fi
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
    "$BUILD/test_server" "$SERVE" "$PORT" &
    local srv_pid=$!
    trap "kill_server $srv_pid" EXIT

    sleep 0.5
    if ! kill -0 "$srv_pid" 2>/dev/null; then
        fail "test server failed to start"
        return 1
    fi

    log "running Python client tests"
    local out rc=0
    out=$(HOST="$SERVE" PORT="$PORT" python3 "$ROOT/test/test_client.py" 2>&1) || rc=$?

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
        # Assumes test_server is already running on SERVE:PORT
        python3 "$ROOT/test/test_client.py"
        ;;
    --server)
        ensure_build
        log "starting test server on ${SERVE}:${PORT}"
        exec "$BUILD/test_server" "$SERVE" "$PORT"
        ;;
    *)
        run_tests
        ;;
esac
