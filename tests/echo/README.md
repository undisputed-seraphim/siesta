# Echo Integration Test Harness

Reference test suite for the Siesta echo sample. Run before merging any
generator changes or refactors.

## Quick Start

```bash
# From tests/echo/:
./run.sh

# Run only (assumes server already running on port 9910)
./run.sh --quick

# Start the test server in the foreground (manual testing)
./run.sh --server

# Override host/port
HOST=0.0.0.0 PORT=8080 ./run.sh

# Override siesta install location
SIESTA_PREFIX=/path/to/siesta/install ./run.sh
```

## Components

| File | Purpose |
|------|---------|
| `test_server.cpp` | Standalone C++ binary — concrete `EchoServer` implementation. Links against generated `server.cpp` and `libsiesta.a`. |
| `test_client.py` | Python integration tests using the generated `Echo_API` nanobind module. |
| `run.sh` | Orchestrator — cmake + ninja build, spawns server, runs client tests, tears down. |

## How It Works

```
run.sh
  ├── cmake (finds siesta via CMAKE_PREFIX_PATH)
  ├── ninja siesta-generator (runs on ../echo.json → build/siesta_gen/)
  ├── ninja echo_server Echo_API
  ├── spawn: build/echo_server 127.0.0.1:9910
  └── python3 test_client.py
```

All generated files live under `build/` — nothing generated in the source tree.

The test server is a pure C++ binary. It implements the generated
`openapi::Server` abstract class inline and returns proper JSON echo
responses with URL-decoded query values.

## Adding New Tests

Add endpoints to `../echo.json`, regenerate, then update `test_server.cpp`
to implement any new handler methods. Add test functions to `test_client.py`
and append them to the `TESTS` list.

## Known Limitations

- The Python server module (`Echo_API_server.so`) cannot yet deliver HTTP
  responses due to a Python GIL ↔ boost::asio interaction. The C++ test
  server serves as the reference.
- `HOST` and `PORT` env vars must agree between server and client.
