# Echo Integration Test Harness

Reference test suite for the Siesta echo sample. Uses a single CMake build file
at `tests/CMakeLists.txt` with flag-set buckets to produce every variant from
one source tree.

## Quick Start

```bash
./run.sh                    # sanity: build + C++ tests + Python tests
./run.sh --quick            # run tests without rebuilding
./run.sh --server           # start server in foreground (manual testing)
./run.sh --bench            # bench build + load test (100k req)
./run.sh --profile          # profile build + load test + CPU report
./run.sh --cpp              # C++ tests only (build + run)
./run.sh --py               # Python tests only (build + run)
./run.sh --load             # load test only (assumes built)

# Override host/port
HOST=0.0.0.0 PORT=8080 ./run.sh

# Override siesta install location
SIESTA_PREFIX=/path/to/siesta/install ./run.sh
```

## Build Targets

All targets are defined in `../CMakeLists.txt`. The shared generated-code
library (`echo_gen`) is built once and linked by all targets.

| Target | Source | Flags | Purpose |
|--------|--------|-------|---------|
| `echo_gen` | (generated) | — | Shared generated code + JSON defs |
| `echo_server` | `test_server.cpp` | `-O2 -g -DNDEBUG` | Sanity / integration test server |
| `echo_server_prof` | `test_server.cpp` | `-O0 -g -fno-omit-frame-pointer` + `-lprofiler` | CPU profiling with gperftools |
| `echo_server_bench` | `test_server.cpp` | `-O3 -DNDEBUG -flto -march=native` | Max-performance benchmarking |
| `echo_test_client` | `test_client.cpp` | `-O2 -g -DNDEBUG` | C++ Catch2 integration test driver |
| `Echo_API` | (generated) | nanobind module | Python client bindings |

Select what you need:
```bash
cmake -S tests -B tests/build -DCMAKE_PREFIX_PATH=... -GNinja
ninja -C tests/build echo_server Echo_API echo_test_client   # sanity
ninja -C tests/build echo_server_bench                        # benchmark
ninja -C tests/build echo_server_prof                         # profiling
```

## Components

| File | Purpose |
|------|---------|
| `test_server.cpp` | Standalone C++ binary — `EchoServer` subclass of generated `openapi::Server`. URL-decodes query, returns JSON echo responses. |
| `test_client.py` | Python integration tests using the generated `Echo_API` nanobind module (3 test cases). |
| `test_client.cpp` | C++ Catch2 integration test driver — connects to running server via generated `openapi::Client`, validates `EchoResponse` (4 test cases). |
| `run.sh` | Unified orchestrator — cmake + ninja build, spawns server, runs C++ and Python tests, load test, profiling. |
| `load_test/load_test.py` | Concurrent raw-HTTP load test with latency percentiles and throughput reporting. |

## How It Works

```
run.sh (sanity)
  ├── cmake -S ../ -B ../build  (tests/CMakeLists.txt)
  ├── ninja echo_server Echo_API echo_test_client
  ├── spawn: ../build/echo_server 127.0.0.1:9910
  ├── ../build/echo_test_client    (Catch2, C++ client tests)
  ├── python3 test_client.py       (nanobind Python tests)
  └── kill server

run.sh --bench
  ├── ninja echo_server_bench
  ├── spawn: ../build/echo_server_bench 127.0.0.1:9910
  ├── python3 load_test/load_test.py --requests 100000 --concurrency 200
  └── kill server

run.sh --profile
  ├── ninja echo_server_prof
  ├── spawn with CPUPROFILE: ../build/echo_server_prof 127.0.0.1:9910
  ├── python3 load_test/load_test.py --requests 50000 --concurrency 100
  ├── kill -INT (triggers ProfilerFlush → ProfilerStop)
  └── google-pprof → load_test/profiles/{cpu_text.txt, cpu_graph.dot, cpu_top.txt}
```

## Adding New Endpoints

1. Add paths and schemas to `../echo.json`
2. Implement the new handler in `test_server.cpp`
3. Add C++ test cases to `test_client.cpp`
4. Add Python test functions to `test_client.py` and append to `TESTS` list
5. Rebuild: `./run.sh`
