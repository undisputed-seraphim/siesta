# Echo Integration Test Harness

Self-contained integration tests for the Beast backend, plus benchmark
and profiling tooling for the echo sample server.

## Running Tests

Tests are built as part of the main CMake tree and discovered by CTest:

```bash
cmake -B build -GNinja
ninja -C build
ctest --test-dir build/tests --output-on-failure
```

Filter by backend:
```bash
ctest --test-dir build/tests -R beast
```

## Benchmarking & Profiling

```bash
./benchmark_beast.sh --bench      # 100k requests, 200 concurrency
./benchmark_beast.sh --profile    # 50k requests + CPU profile report
./benchmark_beast.sh --server     # start server in foreground
./benchmark_beast.sh --load       # load test only (assumes server running)

# Override defaults
HOST=0.0.0.0 PORT=8080 REQUESTS=200000 ./benchmark_beast.sh --bench
```

## Build Targets

| Target | Source | Purpose |
|--------|--------|---------|
| `echo_beast_integration` | `test_beast_integration.cpp` | Self-contained Catch2 tests (19 test cases, 65 assertions) |
| `echo_beast_server` | `test_beast_server.cpp` | Standalone server for benchmarking |
| `echo_beast_server_prof` | `test_beast_server.cpp` | Server with gperftools profiling |
| `echo_beast_server_bench` | `test_beast_server.cpp` | Server with max-performance flags |
| `Echo_API` | (generated) | Python nanobind client module |

## Components

| File | Purpose |
|------|---------|
| `test_beast_integration.cpp` | Self-contained Catch2 test — embeds a `StubServer` + client in one process. Covers POST body round-trip, path params, verbs, variants, allOf, enums, 404. |
| `test_beast_server.cpp` | Standalone `EchoServer` subclass of generated `Echo_API::Server`. Used by benchmark/profiling scripts. |
| `benchmark_beast.sh` | Benchmark & profiling orchestrator — builds server variants, runs load tests, generates CPU profile reports. |
| `load_test/load_test.py` | Concurrent raw-HTTP load test with latency percentiles and throughput reporting. |

## Adding New Endpoints

1. Add paths and schemas to `../echo.json`
2. Add stubs + test cases to `test_beast_integration.cpp` (tag with `[integration][beast]`)
3. Add benchmark stubs to `test_beast_server.cpp`
4. `ninja -C ../../build && ctest --test-dir ../../build/tests --output-on-failure`
