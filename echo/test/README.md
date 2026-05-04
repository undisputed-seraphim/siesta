# Echo Integration Test Harness

This directory contains the reference test suite for the Siesta echo sample
project. New features, refactors, and generator changes should run this suite
before merging.

## Quick Start

```bash
# Build everything and run tests (from echo/ directory)
./test/run.sh

# Run only (assumes server already running on port 9910)
./test/run.sh --quick

# Start the test server in the foreground (manual testing)
./test/run.sh --server
```

Override host/port:

```bash
HOST=0.0.0.0 PORT=8080 ./test/run.sh
```

## Components

| File | Purpose |
|------|---------|
| `test_server.cpp` | Standalone C++ binary with a concrete `EchoServer` implementation. Links against `server.cpp` and `libsiesta.a`. Minimal — no Python or nanobind dependency. |
| `test_client.py` | Python integration tests using the `siesta_client` nanobind module. Sends real HTTP requests and validates responses. |
| `run.sh` | Orchestrator: builds the project (cmake + ninja), spawns the test server, runs the client tests, tears down. |

## How It Works

```
run.sh
  ├── cmake + ninja → build/ (test_server binary + siesta_client.so)
  ├── spawn: test_server 127.0.0.1:9910
  └── python3 test_client.py
```

The test server is a pure C++ binary with no Python dependency. It implements
the `openapi::Server` abstract class inline and returns proper JSON echo
responses parsed from the query string.

The Python client uses the generated `siesta_client` nanobind module (which
wraps `openapi::Client`) to make real HTTP requests against the test server.

## Server Architecture

```
test_server.cpp
  └── EchoServer : openapi::Server       (concrete handler)
        └── Server : siesta::beast::ServerBase  (abstract base, generated)
              ├── handle_request()        (routing dispatch)
              ├── start() / accept loop   (from libsiesta.a)
              └── Session                 (from libsiesta.a)
```

## Adding New Tests

### Server tests

Add endpoints to `tests/echo_v3.json`, regenerate, then update
`test/test_server.cpp` to implement any new handler methods.

### Client tests

Add test functions to `test/test_client.py`, append them to the `TESTS` list.

## Known Limitations

- The Python server (`siesta_server.so`) cannot yet deliver HTTP responses due
  to a GIL interaction issue between `boost::asio::io_context::run()` and
  Python's GIL state management. The C++ test server works correctly and serves
  as the reference implementation.
- `HOST` and `PORT` env vars are passed to both the test server binary and the
  Python client — they must agree.
