#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Load-test + profile orchestrator for the siesta echo server.
#
# Usage:
#   ./run.sh                    # full: build, profile, load-test, report
#   ./run.sh --no-profile       # skip CPU profiling (just load test)
#   ./run.sh --heap             # add heap profiling via tcmalloc
#   ./run.sh --quick            # shorter test for quick iteration
#
# Environment:
#   HOST, PORT          — server listen address (default 127.0.0.1:9910)
#   SIESTA_PREFIX       — path to siesta install (default: ../../build/install)
#   REQUESTS            — number of requests (default: 10000)
#   CONCURRENCY         — concurrent workers (default: 50)

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build"
SERVE="${HOST:-127.0.0.1}"
PORT="${PORT:-9910}"
SIESTA_PREFIX="${SIESTA_PREFIX:-"$ROOT/../../build/install"}"
REQUESTS="${REQUESTS:-10000}"
CONCURRENCY="${CONCURRENCY:-50}"
PROFILE=1
HEAP=0
QUICK=0

RED='\033[0;31m'; GREEN='\033[0;32m'; CYAN='\033[0;36m'; NC='\033[0m'
log()  { echo -e "[$(date +%H:%M:%S)] $*"; }
pass() { echo -e "${GREEN}PASS${NC} $*"; }
info() { echo -e "${CYAN}INFO${NC} $*"; }

while [[ $# -gt 0 ]]; do
    case "$1" in
        --no-profile) PROFILE=0; shift ;;
        --heap)       HEAP=1; shift ;;
        --quick)      QUICK=1; shift ;;
        *) echo "Unknown flag: $1"; exit 2 ;;
    esac
done

if [[ "$QUICK" -eq 1 ]]; then
    REQUESTS=2000
    CONCURRENCY=20
elif [[ "$PROFILE" -eq 1 ]] || [[ "$HEAP" -eq 1 ]]; then
    REQUESTS=50000
    CONCURRENCY=100
fi

# ── Build ────────────────────────────────────────────────────

ensure_build() {
    if [[ "$PROFILE" -eq 1 ]] || [[ "$HEAP" -eq 1 ]]; then
        local build_type=RelWithDebInfo
        local server_target=echo_server_prof
    else
        local build_type=Release
        local server_target=echo_server
    fi
    if [[ ! -d "$BUILD" ]] || [[ ! -f "$BUILD/build.ninja" ]]; then
        log "building echo project (CMAKE_BUILD_TYPE=$build_type)"
        mkdir -p "$BUILD"
        cmake -S "$ROOT" -B "$BUILD" \
            -DCMAKE_PREFIX_PATH="$SIESTA_PREFIX" \
            -DCMAKE_BUILD_TYPE="$build_type" \
            -GNinja >/dev/null
    elif [[ "$(grep CMAKE_BUILD_TYPE "$BUILD/CMakeCache.txt" 2>/dev/null | cut -d= -f2)" != "$build_type" ]]; then
        log "reconfiguring for $build_type"
        rm -f "$BUILD/CMakeCache.txt"
        cmake -S "$ROOT" -B "$BUILD" \
            -DCMAKE_PREFIX_PATH="$SIESTA_PREFIX" \
            -DCMAKE_BUILD_TYPE="$build_type" \
            -GNinja >/dev/null
    fi
    ninja -C "$BUILD" "$server_target"
}

# ── Profiler setup ───────────────────────────────────────────

PROF_DIR="$ROOT/load_test/profiles"
rm -rf "$PROF_DIR"
mkdir -p "$PROF_DIR"

kill_server() {
    local pid="$1"
    if [[ -n "${pid:-}" ]] && kill -0 "$pid" 2>/dev/null; then
        log "stopping server (pid $pid)"
        kill -INT "$pid" 2>/dev/null || true
        for i in $(seq 1 20); do
            if ! kill -0 "$pid" 2>/dev/null; then
                return 0
            fi
            sleep 0.2
        done
        kill -9 "$pid" 2>/dev/null || true
    fi
}

start_server() {
    local env_vars=()

    if [[ "$PROFILE" -eq 1 ]]; then
        env_vars+=(CPUPROFILE="$PROF_DIR/cpu.prof"
                   CPUPROFILE_FREQUENCY=500)
        SERVER_BIN="$BUILD/echo_server_prof"
    else
        SERVER_BIN="$BUILD/echo_server"
    fi

    if [[ "$HEAP" -eq 1 ]]; then
        env_vars+=(HEAPPROFILE="$PROF_DIR/heap.prof"
                   HEAP_PROFILE_ALLOCATION_INTERVAL=1048576)
    fi

    log "starting echo server on ${SERVE}:${PORT}" >&2

    if [[ ${#env_vars[@]} -gt 0 ]]; then
        env "${env_vars[@]}" "$SERVER_BIN" "$SERVE" "$PORT" >/dev/null 2>&1 &
    else
        "$SERVER_BIN" "$SERVE" "$PORT" >/dev/null 2>&1 &
    fi
    local pid=$!
    echo "$pid"

    for i in $(seq 1 10); do
        if python3 -c "import socket; s=socket.socket(); s.settimeout(1); s.connect(('${SERVE}',${PORT})); s.close()" 2>/dev/null; then
            return 0
        fi
        sleep 0.3
    done
    echo ""  # signal failure on stdout
    return 1
}

# ── Load test ─────────────────────────────────────────────────

run_load() {
    local py="$ROOT/load_test/load_test.py"
    info "running load test ($REQUESTS req, $CONCURRENCY concurrent)"
    python3 "$py" \
        --host "$SERVE" --port "$PORT" \
        --requests "$REQUESTS" --concurrency "$CONCURRENCY" \
        --warmup 50
}

# ── Profile reports ───────────────────────────────────────────

generate_reports() {
    local binary="$BUILD/echo_server_prof"

    if [[ "$PROFILE" -eq 1 ]] && [[ -f "$PROF_DIR/cpu.prof" ]]; then
        log "generating CPU profile reports"

        # Text report (top functions)
        google-pprof --text --lines "$binary" "$PROF_DIR/cpu.prof" \
            > "$PROF_DIR/cpu_text.txt" 2>/dev/null

        # Call graph (dot format for graphviz)
        google-pprof --dot "$binary" "$PROF_DIR/cpu.prof" \
            > "$PROF_DIR/cpu_graph.dot" 2>/dev/null

        # Cumulative (focus on hot paths)
        google-pprof --text "$binary" "$PROF_DIR/cpu.prof" \
            2>/dev/null | head -30 > "$PROF_DIR/cpu_top.txt"

        echo ""
        info "CPU profile — top 15 functions:"
        head -16 "$PROF_DIR/cpu_top.txt"
        echo ""
        info "Full reports: $PROF_DIR/cpu_text.txt"
        info "Dot graph:     $PROF_DIR/cpu_graph.dot"
        info "(render with: dot -Tsvg $PROF_DIR/cpu_graph.dot > cpu.svg)"
    fi

    if [[ "$HEAP" -eq 1 ]]; then
        log "generating heap profile report"
        local latest
        latest=$(ls -t "$PROF_DIR"/heap.prof.*.heap 2>/dev/null | head -1)
        if [[ -n "$latest" ]]; then
            google-pprof --text --lines "$binary" "$latest" \
                > "$PROF_DIR/heap_text.txt" 2>/dev/null
            info "Heap profile: $PROF_DIR/heap_text.txt"
        fi
    fi
}

# ── Main ──────────────────────────────────────────────────────

main() {
    echo ""
    echo "═══════════════════════════════════════════"
    echo "  Siesta Echo Server — Load Test + Profile"
    echo "═══════════════════════════════════════════"
    echo ""

    ensure_build

    local srv_pid
    if ! srv_pid=$(start_server) || [[ -z "$srv_pid" ]]; then
        echo "FATAL: could not start server" >&2
        exit 1
    fi
    trap "kill_server $srv_pid" EXIT

    if ! run_load; then
        echo "FATAL: load test failed" >&2
        exit 1
    fi

    kill_server "$srv_pid"
    trap - EXIT

    generate_reports

    echo ""
    log "done — load test complete"
}

main
