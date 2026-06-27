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
in one process, measuring round-trip latency without network overhead. Benchmark and
profiling runs are defined as CMake custom targets in `cmake/`.

```bash
# Embedded benchmark (100k requests, 1 connection)
./bench.sh beast rest --bench
REQUESTS=50000 CONCURRENCY=8 ./bench.sh beast rest --bench

# Future: RPC benchmarks
# ./bench.sh beast rpc_proto --bench
# ./bench.sh beast rpc_openrpc --bench
```

All benchmark targets via CMake directly:

```bash
cmake --build build --target bench_beast_rest
cmake --build build --target bench     # all available benchmarks
```

### Environment

| Variable | Default | Applies to |
|---|---|---|
| `REQUESTS` | 100000 | `bench_*`, `valgrind_*` |
| `CONCURRENCY` | 1 | `bench_*` |
| `PROFILE_REQUESTS` | 50000 | `profile_*` |
| `HEAP_INTERVAL` | 524288 | `profile_heap_*` |

---

## Profiling

Profiling uses gperftools (libprofiler). Profiler-enabled server targets are built
automatically when the library is detected. Output goes to `build/profiles/`.

```bash
# CPU profile (-O0, line-level granularity)
./bench.sh beast rest --profile
PROFILE_REQUESTS=20000 ./bench.sh beast rest --profile

# Heap profile (tcmalloc)
./bench.sh beast rest --profile-heap
```

Results in `build/profiles/`:

| File | Content |
|---|---|
| `cpu.prof` | Raw profile data (gperftools format) |
| `heap.*.heap` | Heap dump snapshots |

Run `google-pprof --text <binary> build/profiles/cpu.prof` to generate reports.

All profiling targets via CMake:

```bash
cmake --build build --target profile_beast_rest
cmake --build build --target profile_heap_beast_rest
cmake --build build --target profile
```

---

## Sanitizing

### AddressSanitizer

```bash
cmake -B build -GNinja -DSIESTA_ENABLE_ASAN=ON
ninja -C build
ctest --test-dir build/tests --output-on-failure
```

`cmake --build build --target asan` prints a brief reminder.

### Valgrind

```bash
./bench.sh beast rest --valgrind          # 1k requests under memcheck
VALGRIND_REQUESTS=5000 ./bench.sh beast rest --valgrind
```

Direct CMake: `cmake --build build --target valgrind_beast_rest`

---

## Directory Layout

```
tests/
├── CMakeLists.txt
├── bench.sh                        Thin shell wrapper for bench/profile/valgrind
├── echo.json                       OpenAPI 3.0 spec (REST fixture)
├── beast/                          Integration tests — one file per format
│   ├── rest_echo.cpp               REST/OpenAPI → beast
│   ├── rpc_proto.cpp               RPC/proto → beast
│   └── rpc_openrpc.cpp             RPC/OpenRPC → beast
├── rpc/                            RPC fixtures (shared across backends)
│   ├── petstore.proto              Proto3 test fixture
│   └── petstore_openrpc.json       OpenRPC test fixture
├── echo/                           Benchmarks + standalone server
│   ├── benchmark_beast.cpp         Embedded benchmark binary
│   ├── echo_stubs.hpp              Shared test server stubs
│   └── test_beast_server.cpp       Standalone server
├── siesta/                         Library unit tests
└── consumer/                       Downstream CMake packaging smoke test
```
