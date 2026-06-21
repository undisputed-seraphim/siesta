# Echo Integration Test Harness

Reference test suite for the Siesta echo sample. Uses a single CMake build file
at `tests/CMakeLists.txt` with flag-set buckets to produce every variant from
one source tree.

## Quick Start

```bash
./run_beast.sh                    # sanity: build + C++ tests + Python tests
./run_beast.sh --quick            # run tests without rebuilding
./run_beast.sh --server           # start server in foreground (manual testing)
./run_beast.sh --bench            # bench build + load test (100k req)
./run_beast.sh --profile          # profile build + load test + CPU report
./run_beast.sh --cpp              # C++ tests only (build + run)
./run_beast.sh --py               # Python tests only (build + run)
./run_beast.sh --load             # load test only (assumes built)

# Override host/port
HOST=0.0.0.0 PORT=8080 ./run_beast.sh

# Override siesta install location
SIESTA_PREFIX=/path/to/siesta/install ./run_beast.sh
```

## Build Targets

All targets are defined in `../CMakeLists.txt`. The shared generated-code
library (`echo_gen`) is built once and linked by all targets.

| Target | Source | Flags | Purpose |
|--------|--------|-------|---------|
| `echo_gen` | (generated) | — | Shared generated code + JSON defs |
| `echo_beast_server` | `test_beast_server.cpp` | `-O2 -g -DNDEBUG` | Sanity / integration test server |
| `echo_beast_server_prof` | `test_beast_server.cpp` | `-O0 -g -fno-omit-frame-pointer` + `-lprofiler` | CPU profiling with gperftools |
| `echo_beast_server_bench` | `test_beast_server.cpp` | `-O3 -DNDEBUG -flto -march=native` | Max-performance benchmarking |
| `echo_beast_client` | `test_beast_client.cpp` | `-O2 -g -DNDEBUG` | C++ Catch2 integration test driver |
| `echo_beast_integration` | `test_beast_integration.cpp` | `-O2 -g -DNDEBUG` | Self-contained integration tests |
| `Echo_API` | (generated) | nanobind module | Python client bindings |

Select what you need:
```bash
cmake -S tests -B tests/build -DCMAKE_PREFIX_PATH=... -GNinja
ninja -C tests/build echo_beast_server Echo_API echo_beast_client   # sanity
ninja -C tests/build echo_beast_server_bench                        # benchmark
ninja -C tests/build echo_beast_server_prof                         # profiling
```

## Components

| File | Purpose |
|------|---------|
| `test_beast_server.cpp` | Standalone C++ binary — `EchoServer` subclass of generated `Echo_API::Server`. URL-decodes query, returns JSON echo responses. |
| `test_beast_integration.cpp` | Self-contained Catch2 test — embeds a `StubServer` + client in one process. Covers POST body round-trip, path params, verbs, variants, allOf, enums, 404 (19 test cases). |
| `test_beast_client.cpp` | C++ Catch2 integration test driver — connects to running server via generated `Echo_API::Client`, validates `EchoResponse` (4 test cases). |
| `test_beast_client.py` | Python integration tests using the generated `Echo_API` nanobind module (3 test cases). |
| `run_beast.sh` | Unified orchestrator — cmake + ninja build, spawns server, runs C++ and Python tests, load test, profiling. |
| `load_test/load_test.py` | Concurrent raw-HTTP load test with latency percentiles and throughput reporting. |

## How It Works

```
run_beast.sh (sanity)
  ├── cmake -S ../ -B ../build  (tests/CMakeLists.txt)
  ├── ninja echo_beast_server Echo_API echo_beast_client echo_beast_integration
  ├── ../build/echo_beast_integration (self-contained, no external server)
  ├── spawn: ../build/echo_beast_server 127.0.0.1:9910
  ├── ../build/echo_beast_client       (Catch2, C++ client tests)
  ├── python3 test_beast_client.py     (nanobind Python tests)
  └── kill server

run_beast.sh --bench
  ├── ninja echo_beast_server_bench
  ├── spawn: ../build/echo_beast_server_bench 127.0.0.1:9910
  ├── python3 load_test/load_test.py --requests 100000 --concurrency 200
  └── kill server

run_beast.sh --profile
  ├── ninja echo_beast_server_prof
  ├── spawn with CPUPROFILE: ../build/echo_beast_server_prof 127.0.0.1:9910
  ├── python3 load_test/load_test.py --requests 50000 --concurrency 100
  ├── kill -INT (triggers ProfilerFlush → ProfilerStop)
  └── google-pprof → load_test/profiles/{cpu_text.txt, cpu_graph.dot, cpu_top.txt}
```

## Adding New Endpoints

1. Add paths and schemas to `../echo.json`
2. Implement the new handler in `test_beast_server.cpp`
3. Add C++ test cases to `test_beast_integration.cpp` (tag with `[beast]`)
4. Add Python test functions to `test_beast_client.py` and append to `TESTS` list
5. Rebuild: `./run_beast.sh`
