# siesta

Generates C++ client and server stubs from an OpenAPI v3 JSON spec. Takes a
schema file, outputs ready-to-compile `.hpp`/`.cpp`/`.py` files with no runtime
code generation — everything happens at build time.

## Backends

| Backend | Status |
|---------|--------|
| boost::beast | shipping |
| (more planned) | — |

The backend abstraction lives in `include/siesta/beast/` and the generator
selects the appropriate backend at codegen time. Adding a new backend means
writing a new set of `ClientBase`/`ServerBase` implementations and a
corresponding generator class.

## Architecture

```
OpenAPI v3 JSON  ──▶  simdjson parse  ──▶  Normalized AST  ──▶  topological sort
                                                                      │
                        ┌─────────────────────────────────────────────┘
                        ▼
                  code generation
                        │
          ┌─────────────┼──────────────┬────────────────┐
          ▼             ▼              ▼                ▼
     defs.hpp      client.hpp    py_module.cpp    server.hpp
     defs.cpp                                 server.cpp
                                                server_py.cpp
```

The generator runs in three phases: schema normalization into an AST,
dependency analysis with cycle detection and topological sort, then code
generation through a set of polymorphic backends (`ICodeGenerator`). Full
details in `generator/ARCHITECTURE.md`.

## Generated output

### Type definitions (`openapi_defs.hpp` / `.cpp`)

Structs, variants (`std::variant`), enums, and map/array aliases for every type
in the spec. All of them support round-trip JSON serialization via
`boost::json::tag_invoke`.

### Client (`client.hpp`)

A class extending `siesta::beast::ClientBase` with one templated async method
per operation. Path, query, and header parameters become function arguments.
The return type is a completion-token-agnostic async operation — call with
`boost::asio::use_future` for synchronous use, or chain with any asio
completion token.

### Server (`server.hpp` / `server.cpp`)

An abstract class extending `siesta::beast::ServerBase` with one pure-virtual
method per endpoint. The `.cpp` file contains the dispatch table — incoming
requests are routed to the correct virtual method by path and verb. Parameterised
paths (`/items/{id}`) use a segment-by-segment match algorithm.

### Python bindings (`py_module.cpp`)

A nanobind extension module that wraps the C++ client synchronously. Each
endpoint method runs the asio event loop (`ctx.run()`) behind the scenes and
returns a Python dict parsed from the JSON response body.

### Python server (`server_py.cpp`)

A nanobind trampoline class that lets you subclass the server in Python.
Override the virtual methods to handle requests, then call `listen()` to
start the event loop. Status: functional but with a known GIL-interaction
issue — use the C++ server binary for integration testing until resolved.

## Quick start

```bash
# Build the generator
cd build && ninja siesta-generator

# Generate from a spec
./build/generator/siesta-generator --input spec.json --output out/

# Build the generated project
cd out && cmake -B build -DSIESTA_ROOT=.. -GNinja && ninja -C build

# Run the echo integration tests (reference)
cd tests/echo && ./run.sh
```

The `echo/` directory is the reference test project. It has a single `GET
/echo?message=` endpoint and includes a C++ test server binary, a Python
client test suite, and a shell orchestrator. Use it to verify that generator
changes haven't broken anything.
