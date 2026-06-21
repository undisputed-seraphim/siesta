# Siesta Tests

Integration tests for generated code and the siesta runtime library.
Tests are built as part of the main CMake tree and run via CTest.

## Directory Layout

```
tests/
├── CMakeLists.txt                  # Test targets (integrated into root build)
├── echo.json                       # OpenAPI 3.0 spec (backend-independent)
├── echo/
│   ├── test_beast_server.cpp       # Beast server implementation (benchmark only)
│   ├── test_beast_integration.cpp  # Beast self-contained integration tests
│   ├── benchmark_beast.sh          # Benchmark & profiling orchestrator
│   ├── load_test/                  # Load test scripts + profile data
│   └── README.md
└── siesta/
    └── path_tree.t.cpp             # Library unit tests (Catch2)
```

## Quick Start

```bash
cmake -B build -GNinja
ninja -C build
ctest --test-dir build/tests --output-on-failure
```

## Targets

| Target | ALL | Purpose |
|--------|-----|---------|
| `echo_beast_integration` | yes | Self-contained Beast integration tests (19 test cases) |
| `echo_beast_server` | no | Beast server binary (benchmark/profiling only) |
| `echo_beast_server_prof` | no | Beast server with gperftools profiling |
| `echo_beast_server_bench` | no | Beast server with max-performance flags |
| `Echo_API` | no | Python nanobind client module |
| `siesta_test` | no | Library unit tests |

## Benchmarking & Profiling

```bash
cd tests/echo
./benchmark_beast.sh --bench      # 100k requests, 200 concurrency
./benchmark_beast.sh --profile    # 50k requests + CPU profile report
./benchmark_beast.sh --server     # start server in foreground
```

## Adding Tests

1. Add endpoints/schemas to `echo.json`
2. Add stubs + test cases to `echo/test_beast_integration.cpp` (tag with `[beast]`)
3. `ninja && ctest --test-dir build/tests --output-on-failure`
