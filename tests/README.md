# Siesta Tests

The echo schema (`echo.json`) is the foundation for all testing — sanity check,
integration, profiling, and benchmarking. Every target derives from a single
`tests/CMakeLists.txt`.

## Directory Layout

```
tests/
├── CMakeLists.txt          # Single build file — all test targets
├── echo.json               # OpenAPI 3.0 spec (all endpoints live here)
├── echo/
│   ├── test_server.cpp     # C++ server implementation
│   ├── test_client.cpp     # C++ Catch2 integration test driver
│   ├── test_client.py      # Python integration test driver
│   ├── run.sh              # Unified orchestrator (build + run + profile)
│   ├── load_test/          # Load test scripts + profile data
│   └── README.md
└── siesta/
    └── path_tree.t.cpp     # Library unit tests (Catch2)
```

## Quick Start

```bash
# Sanity — build + run everything
cd tests/echo && ./run.sh

# Individual targets from the build directory
cmake -S tests -B tests/build -DCMAKE_PREFIX_PATH=build/install -GNinja
ninja -C tests/build echo_server Echo_API        # server + Python bindings
ninja -C tests/build echo_test_client             # C++ test driver
ninja -C tests/build siesta_test                  # library unit tests
```

## Targets

All targets are defined in `CMakeLists.txt` with three flag-set buckets.

| Target | Flags | ALL | Purpose |
|--------|-------|-----|---------|
| `echo_gen` | — | no | Generated code + defs (shared library) |
| `echo_server` | `-O2 -g -DNDEBUG` | yes | Sanity / integration server |
| `echo_server_prof` | `-O0 -g -fno-omit-frame-pointer` | no | CPU profiling (gperftools) |
| `echo_server_bench` | `-O3 -DNDEBUG -flto -march=native` | no | Max-performance benchmark |
| `echo_test_client` | `-O2 -g -DNDEBUG` | no | C++ Catch2 client-side tests |
| `siesta_test` | — | no | Catch2 library unit tests |
| `Echo_API` | nanobind | no | Python client bindings |

Only `echo_server` is built by default (`ninja`). Everything else is
build-on-demand — specify the target name with `ninja`.

## Flag Buckets

| Bucket | Flags | Linker | Use Case |
|--------|-------|--------|----------|
| Release | `-O2 -g -DNDEBUG` | — | Default: integration tests, fast iteration |
| Profiling | `-O0 -g -fno-omit-frame-pointer` | — | gperftools, perf record, valgrind, heaptrack |
| Benchmark | `-O3 -DNDEBUG -march=native -mtune=native -flto=auto -fno-semantic-interposition -finline-functions --param inline-unit-growth=200` | `-flto=auto -Wl,-O2 -Wl,--gc-sections` | Absolute max throughput |

## Test Orchestrator

`tests/echo/run.sh` is the unified entry point:

```
./run.sh              # sanity: C++ + Python tests
./run.sh --bench      # benchmark build + 100k load test
./run.sh --profile    # profile build + load test + CPU report
./run.sh --cpp        # C++ tests only
./run.sh --py         # Python tests only
./run.sh --quick      # run tests without rebuilding
./run.sh --server     # foreground server (manual testing)
```

## Adding Tests

1. Add endpoints/schemas to `echo.json`
2. Implement handlers in `echo/test_server.cpp`
3. Add C++ test cases in `echo/test_client.cpp` (Catch2)
4. Add Python test functions in `echo/test_client.py`
5. Build: `./run.sh`
