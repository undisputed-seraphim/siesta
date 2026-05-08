# AGENTS.md - OpenAPI-to-C++ Transpiler

## Overview

This project transpiles OpenAPI v3 schemas into production-grade C++ client/server code and Python extension modules using boost.asio and boost.json. The generator produces:
- **openapi_defs.hpp/cpp**: Type definitions with JSON serialization (boost::json tag_invoke)
- **client.hpp**: Completion-token agnostic HTTP client class (extends `siesta::beast::ClientBase`)
- **server.hpp/cpp**: Abstract server class with virtual dispatch (extends `siesta::beast::ServerBase`)
- **py_module.cpp**: nanobind Python extension module with synchronous client wrappers
- **server_py.cpp**: nanobind trampoline for Python-side server subclassing

See `generator/ARCHITECTURE.md` for full architecture, file index, data flow, and implementation details.

## Key Design Decisions

### 1. allOf → C++ Inheritance ✅

OpenAPI `allOf` maps to C++ multiple inheritance. Serialization merges base class JSON objects before adding derived fields.

### 2. Nested Types → Flat Naming ✅

Nested types use `Parent_Child` naming (not `Parent::Child`) for simpler dependency tracking.

### 3. Variant Deduplication ✅

Duplicate `std::variant<...>` signatures become `using` aliases tracked in a typedef chain. Single-alternative non-nullable variants collapse to typedefs.

### 4. Cyclic Dependencies → Fail Fast ✅

Value-only semantics cannot represent cycles. The generator detects them and aborts with a clear error message.

### 5. Python Bindings via Synchronous Execution ✅

Each Python endpoint method wraps the async C++ client call with `boost::asio::use_future`, runs `io_context`, and converts the JSON response to Python objects.

### 6. Shared Endpoint IR ✅

Endpoints are parsed once via `endpoint_ir.hpp/cpp` → `parseEndpoints()` and passed to all backends via `CodegenArgs::endpoints`. Backends consume `std::vector<Endpoint>` — they no longer re-parse the OpenAPI spec independently.

## Current State

### Completed ✅
- OpenAPI JSON parsing using simdjson
- Normalized AST construction (primitives, structs, arrays, maps, enums, variants)
- Dependency graph with cycle detection and Kahn's topological sort
- `allOf` inheritance support with merged serialization
- `defs.hpp/cpp` generation with variant deduplication and typedef chain resolution
- `client.hpp` generation with path/query/header parameter handling
- `server.hpp/cpp` generation with static-path + parameterised-path dispatch
- `py_module.cpp` generation with nanobind Python bindings
- `server_py.cpp` generation with nanobind trampoline server subclassing
- Echo integration test harness with C++ (Catch2) and Python test drivers

### Known Limitations
1. **Cyclic dependencies**: Not supported — generator fails with clear error
2. **Polymorphic dispatch**: `oneOf` generates `std::variant` but no runtime discriminator-based dispatch
3. **Schema validation**: Minimal OpenAPI spec validation
4. **Parameter resolution**: Complex `$ref` chains in parameters may not resolve perfectly

## Testing

### Quick Test
```bash
# Build generator
cd build && ninja siesta-generator

# Setup test build
cmake -S tests -B tests/build -DCMAKE_PREFIX_PATH=build/install -GNinja

# Generate + compile echo sanity targets
ninja -C tests/build echo_server Echo_API echo_test_client

# Run C++ + Python integration tests
cd tests/echo && ./run.sh
```

### Debugging
```bash
# Check for warnings / missing types
./build/generator/siesta-generator --input tests/echo.json --output /tmp/test/ 2>&1 | grep -i 'warning\|cycle\|missing'

# Filter log output by phase (PARSE, DEP, EMIT, SORT)
./build/generator/siesta-generator --input tests/echo.json --output /tmp/test/ 2>&1 | grep '\[EMIT\]'
```

<!-- codebase-memory-mcp:start -->
# Codebase Knowledge Graph (codebase-memory-mcp)

This project uses codebase-memory-mcp to maintain a knowledge graph of the codebase.
ALWAYS prefer MCP graph tools over grep/glob/file-search for code discovery.

## Priority Order
1. `search_graph` — find functions, classes, routes, variables by pattern
2. `trace_path` — trace who calls a function or what it calls
3. `get_code_snippet` — read specific function/class source code
4. `query_graph` — run Cypher queries for complex patterns
5. `get_architecture` — high-level project summary

## When to fall back to grep/glob
- Searching for string literals, error messages, config values
- Searching non-code files (Dockerfiles, shell scripts, configs)
- When MCP tools return insufficient results

## Examples
- Find a handler: `search_graph(name_pattern=".*OrderHandler.*")`
- Who calls it: `trace_path(function_name="OrderHandler", direction="inbound")`
- Read source: `get_code_snippet(qualified_name="pkg/orders.OrderHandler")`
<!-- codebase-memory-mcp:end -->

