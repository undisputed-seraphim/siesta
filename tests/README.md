# Siesta Tests

The echo schema (`echo.json`) is the foundation for all testing — sanity check,
integration, profiling, and benchmarking. Every target derives from a single
`tests/CMakeLists.txt`.

## Directory Layout

```
tests/
├── CMakeLists.txt              # Single build file — all test targets
├── echo.json                   # OpenAPI 3.0 spec (all endpoints live here)
├── echo/
│   ├── test_beast_server.cpp   # Beast C++ server implementation
│   ├── test_beast_client.cpp   # Beast C++ Catch2 integration test driver
│   ├── test_beast_integration.cpp # Beast self-contained integration tests
│   ├── test_beast_client.py    # Beast Python integration test driver
│   ├── run_beast.sh            # Beast test orchestrator (build + run + profile)
│   ├── load_test/              # Load test scripts + profile data
│   └── README.md
└── siesta/
    └── path_tree.t.cpp         # Library unit tests (Catch2)
```

## Quick Start

```bash
# Sanity — build + run everything
cd tests/echo && ./run_beast.sh

# Individual targets from the build directory
cmake -S tests -B tests/build -DCMAKE_PREFIX_PATH=build/install -GNinja
ninja -C tests/build echo_beast_server Echo_API    # server + Python bindings
ninja -C tests/build echo_beast_client             # C++ test driver
ninja -C tests/build echo_beast_integration        # self-contained integration tests
ninja -C tests/build siesta_test                   # library unit tests
```

## Targets

All targets are defined in `CMakeLists.txt` with three flag-set buckets.

| Target | Flags | ALL | Purpose |
|--------|-------|-----|---------|
| `echo_gen` | — | no | Generated code + defs (shared library) |
| `echo_beast_server` | `-O2 -g -DNDEBUG` | yes | Beast sanity / integration server |
| `echo_beast_server_prof` | `-O0 -g -fno-omit-frame-pointer` | no | CPU profiling (gperftools) |
| `echo_beast_server_bench` | `-O3 -DNDEBUG -flto -march=native` | no | Max-performance benchmark |
| `echo_beast_client` | `-O2 -g -DNDEBUG` | no | Beast C++ Catch2 client-side tests |
| `echo_beast_integration` | `-O2 -g -DNDEBUG` | no | Beast self-contained integration tests |
| `siesta_test` | — | no | Catch2 library unit tests |
| `Echo_API` | nanobind | no | Python client bindings |

Only `echo_beast_server` is built by default (`ninja`). Everything else is
build-on-demand — specify the target name with `ninja`.

## Flag Buckets

| Bucket | Flags | Linker | Use Case |
|--------|-------|--------|----------|
| Release | `-O2 -g -DNDEBUG` | — | Default: integration tests, fast iteration |
| Profiling | `-O0 -g -fno-omit-frame-pointer` | — | gperftools, perf record, valgrind, heaptrack |
| Benchmark | `-O3 -DNDEBUG -march=native -mtune=native -flto=auto -fno-semantic-interposition -finline-functions --param inline-unit-growth=200` | `-flto=auto -Wl,-O2 -Wl,--gc-sections` | Absolute max throughput |

## Test Orchestrator

`tests/echo/run_beast.sh` is the unified entry point for Beast backend tests:

```
./run_beast.sh              # sanity: C++ + Python tests
./run_beast.sh --bench      # benchmark build + 100k load test
./run_beast.sh --profile    # profile build + load test + CPU report
./run_beast.sh --cpp        # C++ tests only
./run_beast.sh --py         # Python tests only
./run_beast.sh --quick      # run tests without rebuilding
./run_beast.sh --server     # foreground server (manual testing)
```

## Adding Tests

1. Add endpoints/schemas to `echo.json`
2. Implement handlers in `echo/test_beast_server.cpp`
3. Add C++ test cases in `echo/test_beast_integration.cpp` (Catch2, `[beast]` tag)
4. Add Python test functions in `echo/test_beast_client.py`
5. Build: `./run_beast.sh`
