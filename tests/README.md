# Tests

## Integrated Tests

All tests run via CTest from the top-level build directory. The generator is invoked
at build time to produce C++ stubs from schema files, then each test binary links the
generated code and exercises it against a live HTTP server embedded in the test process.

```bash
# Build everything and run all tests
cmake -B build -GNinja
ninja -C build
ctest --test-dir build/tests --output-on-failure

# Filter by name
ctest --test-dir build/tests -R beast
ctest --test-dir build/tests -R rpc
ctest --test-dir build/tests -R siesta_test
```

### Test targets

| Target | Format | Backend | Purpose |
|---|---|---|---|
| `beast_rest_echo` | OpenAPI (`echo.json`) | beast | 30+ Catch2 tests: JSON round-trip, path params, verbs, variants, allOf, enums, 404, WebSocket |
| `beast_rpc_proto` | proto3 (`rpc/petstore.proto`) | beast | CreatePet + GetPet round-trip |
| `beast_rpc_openrpc` | OpenRPC (`rpc/petstore_openrpc.json`) | beast | CreatePet + GetPet round-trip |
| `siesta_test` | — | library | Library unit tests (path tree, containers) |
| `Echo_API` | — | Python | nanobind client module tests |
| `Echo_API_server` | — | Python | nanobind server trampoline tests |
| `consumer_smoke` | — | cmake | Downstream CMake packaging smoke test |

### Adding tests

1. Add endpoints/schemas to `echo.json` (REST), `rpc/petstore.proto` (RPC), or `rpc/petstore_openrpc.json` (OpenRPC)
2. Add test cases to the corresponding `tests/beast/<format>_<source>.cpp` file
3. `ninja -C build && ctest --test-dir build/tests --output-on-failure`

### Adding a new backend

For each new backend (e.g. `nghttp2`):

1. Create `tests/nghttp2/` with the same three test files: `rest_echo.cpp`, `rpc_proto.cpp`, `rpc_openrpc.cpp`
2. In `CMakeLists.txt`, add `nghttp2_*` targets with `BACKEND nghttp2` in `siesta_generate()`
3. The test files are identical to `tests/beast/` except they `#include` nghttp2 base classes

---

## Benchmarking

The C++ benchmark binary (`echo_beast_benchmark`) embeds both an HTTP server and client
in one process, measuring round-trip latency without network overhead.

```bash
# Embedded benchmark (100k requests, 1 connection, server in-process)
cd tests/echo && ./benchmark_beast.sh --bench

# With custom parameters
REQUESTS=50000 CONCURRENCY=8 ./benchmark_beast.sh --bench

# Benchmark modes
./benchmark_beast.sh --bench --mode post-item      # POST with JSON parse/serialize
./benchmark_beast.sh --bench --mode post-detailed  # POST with allOf inheritance
./benchmark_beast.sh --bench --mode post-outcome   # POST with variant dispatch
```

The benchmark script also supports external server mode:

```bash
# Start server in one terminal
./benchmark_beast.sh --server

# Run load test against it from another
REQUESTS=100000 CONCURRENCY=200 ./benchmark_beast.sh --load
```

### Benchmark results

Output is printed to stdout with latency percentiles (p50, p90, p99, p99.9) and throughput
(requests/sec). Example:

```
Beast Benchmark [echo] (pipeline depth: 0)
════════════════════════════════════════
  Requests:  100000
  Concurrency: 1
  Total time: 1.075 s
  Throughput: 93023 req/s
  Latency (us):
    p50:  1    p90:  2    p99:  5    p99.9: 12
════════════════════════════════════════
```

### Benchmark build targets

| Target | Compiler flags | Purpose |
|--------|---------------|---------|
| `echo_beast_benchmark` | Release (`-O3 -DNDEBUG`) | Standard benchmark |
| `echo_beast_server_bench` | Max-performance (`-O3 -march=native -flto`) | Max throughput server for external benchmarking |

---

## Sanitizing

AddressSanitizer (ASan) is enabled at the CMake level and propagates to all build targets:

```bash
# Configure with ASan
cmake -B build -GNinja -DSIESTA_ENABLE_ASAN=ON

# Build and test — ASan runs inside every test binary
ninja -C build
ctest --test-dir build/tests --output-on-failure
```

ASan flags are also propagated to the `consumer_smoke` test (downstream CMake project),
ensuring end-to-end sanitizer coverage.

Valgrind can be used for heap analysis independent of the build flags:

```bash
cd tests/echo
./benchmark_beast.sh --valgrind    # 1k requests under memcheck
```

---

## Profiling

Profiling uses gperftools (libprofiler). Profiler-enabled server targets are built
automatically when the library is detected.

### CPU profiling

```bash
cd tests/echo

# -O0 build — line-level granularity
./benchmark_beast.sh --profile

# -O2 build — function-level granularity  
./benchmark_beast.sh --profile-o2
```

Results are stored under `tests/echo/load_test/profiles/`:

| File | Content |
|------|---------|
| `cpu.prof` | Raw profile data (gperftools format) |
| `cpu_text.txt` | Full text report (all functions) |
| `cpu_top.txt` | Top 30 functions by sample count |
| `cpu_graph.dot` | Call graph (Graphviz format) |

The script prints the top 15 functions to stdout after the run.

### Heap profiling

```bash
cd tests/echo
./benchmark_beast.sh --heap-profile   # 50k requests, tcmalloc heap sampling
```

Results under `tests/echo/load_test/profiles/`:

| File | Content |
|------|---------|
| `heap.*.heap` | Heap dump snapshots |
| `heap_text.txt` | Full allocation report |
| `heap_top.txt` | Top 30 allocation sites |

### Profiling build targets

| Target | Compiler flags | Purpose |
|--------|---------------|---------|
| `echo_beast_server_prof` | `-O0 -g -fno-omit-frame-pointer` | Line-level CPU profiling |
| `echo_beast_server_prof_o2` | `-O2 -g -fno-omit-frame-pointer` | Optimized CPU profiling |

---

## Directory Layout

```
tests/
├── CMakeLists.txt
├── echo.json                       OpenAPI 3.0 spec (REST fixture)
├── beast/                          Integration tests — one file per format
│   ├── rest_echo.cpp               REST/OpenAPI → beast
│   ├── rpc_proto.cpp               RPC/proto → beast
│   └── rpc_openrpc.cpp             RPC/OpenRPC → beast
├── rpc/                            RPC fixtures (shared across backends)
│   ├── petstore.proto              Proto3 test fixture
│   └── petstore_openrpc.json       OpenRPC test fixture
├── echo/                           Beast benchmark + standalone server
│   ├── test_beast_server.cpp       Standalone server (benchmark/profiling)
│   ├── benchmark_beast.cpp         Embedded benchmark binary
│   ├── echo_stubs.hpp              Shared test server stubs
│   ├── benchmark_beast.sh          Benchmark & profiling orchestrator
│   └── load_test/                  Load test scripts + profile data
├── siesta/                         Library unit tests
└── consumer/                       Downstream CMake packaging smoke test
```
