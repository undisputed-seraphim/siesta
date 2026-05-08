# Generator Architecture

## Overview

The siesta generator is a C++23 compiler-like tool that transforms OpenAPI v3 JSON schemas into production-grade C++ client code and Python extension modules. It uses simdjson for zero-copy parsing, boost::json for runtime JSON, boost::asio for async I/O, and nanobind for Python bindings.

```
OpenAPI v3 JSON ──▶ Phase 1: AST ──▶ Phase 2: Dep Graph ──▶ Phase 3: Codegen ──▶ .hpp / .cpp / .py
```

| Output File | Content |
|-------------|---------|
| `openapi_defs.hpp` | Type definitions (structs, variants, enums, using-aliases), forward declarations, `tag_invoke` signatures |
| `openapi_defs.cpp` | `tag_invoke` bodies for boost::json serialization/deserialization |
| `client.hpp` | Async HTTP client class extending `siesta::beast::ClientBase` with one endpoint method per OpenAPI operation |
| `server.hpp` / `server.cpp` | Abstract server class with virtual methods + dispatch table (static-path O(1) lookup, parameterised-path segment matching) |
| `py_module.cpp` | Nanobind Python extension module wrapping the C++ client synchronously via `boost::asio::use_future` |
| `server_py.cpp` | Nanobind trampoline class for Python-side server subclassing |

All five backends implement `ICodeGenerator` (`codegen_base.hpp`). Endpoints are parsed once by `endpoint_ir.hpp` → `parseEndpoints()` and passed to backends via `CodegenArgs::endpoints`. This eliminates ~500 lines of duplicated endpoint parsing.

---

## File Index

### Generator (`generator/src/`)

| File | Role |
|------|------|
| `main.cpp` | CLI entry point, argument parsing |
| `openapi3_codegen.cpp` / `.hpp` | Pipeline orchestrator — runs all phases sequentially |
| `openapi.hpp` / `.cpp` | simdjson wrapper, base OpenAPI accessors, `ListAdaptor` / `MapAdaptor` |
| `openapi3.hpp` / `.cpp` | OpenAPI v3-specific parsed types (schemas, paths, operations, components) |
| `schema_ast.hpp` / `.cpp` | Normalized AST definitions (`StructType`, `VariantType`, `ArrayType`, etc.) + validation |
| `schema_parser.hpp` / `.cpp` | `SchemaParser` — converts raw JsonSchema → AST nodes. Top-level `parseSchema` dispatches to `parseObjectSchema`, `parseArraySchema`, `parsePrimitiveSchema`, `parseUnknownSchema` |
| `dependency_graph.hpp` / `.cpp` | `DependencyGraph` + Kahn's topological sort + cycle detection |
| `codegen_base.hpp` | `CodegenArgs` struct + `ICodeGenerator` abstract interface |
| `endpoint_ir.hpp` / `.cpp` | Shared endpoint IR: `Endpoint` struct (includes `cpp_verb`), `ClientParam`, `AuthType`, plus `parseEndpoints()` — parsed once, consumed by all backends |
| `codegen_defs.hpp` / `.cpp` | `DefsGenerator : ICodeGenerator` — emits type definitions + ser/des |
| `codegen_client.hpp` / `.cpp` | `ClientGenerator : ICodeGenerator` — emits async client class |
| `codegen_server.hpp` / `.cpp` | `ServerGenerator : ICodeGenerator` — emits abstract server class |
| `codegen_python.hpp` / `.cpp` | `PythonGenerator : ICodeGenerator` — emits nanobind client module |
| `codegen_server_python.hpp` / `.cpp` | `ServerPythonGenerator : ICodeGenerator` — emits nanobind server trampoline |
| `util.hpp` / `.cpp` | Shared utilities: `sanitize` (O(1) keyword lookup via `unordered_set`), `escapeCppString`, `primitiveToCpp`, logging macros |

### Runtime (`include/siesta/beast/`)

| File | Role |
|------|------|
| `client.hpp/.cpp` | `ClientBase` — async HTTP/1.1 client with strand-serialized I/O, `async_submit_request` 3-state FSM, `_host_value` auto-populated from `start()` |
| `server.hpp/.cpp` | `ServerBase` + `Session` — async TCP acceptor, per-connection request/response pipeline, configurable read/write timeouts |
| `python_util.hpp` | Shared nanobind helpers: `json_to_python()` + `extract_response_json()` — included by all generated `py_module.cpp` |
| `error.hpp` | Outcome/error_code adaptors |

---

## Phase 1: Frontend — Schema Normalization

**Entry**: `openapi3_codegen.cpp::buildAST()` → `parseSchemas()` + `parsePaths()`

1. `openapi::OpenAPI::Load()` reads the JSON file into simdjson's on-demand DOM
2. `static_cast<const openapi::v3::OpenAPIv3&>` casts to a v3-specific typed view
3. `parseSchemas()` iterates `components/schemas`, calling `SchemaParser::parseSchema()` for each entry
4. `parsePaths()` collects path/operation metadata into `PathItem` objects
5. Result: `schema::NormalizedAST` containing all types and paths

### SchemaParser (`schema_parser.hpp` + `.cpp`)

Parses `components/schemas` entries by dispatching on `JsonSchema::Type_()`. The former 255-line `parseSchema` switch has been decomposed into focused sub-parsers: `parseObjectSchema`, `parseArraySchema`, `parsePrimitiveSchema`, `parseUnknownSchema`, plus `parseImplicitObject` and `buildVariant`. The top-level `parseSchema` is now a ~20-line dispatch switch.

| Input Pattern | AST Output | Notes |
|---------------|------------|-------|
| `object` with `properties` / `allOf` | `StructType` | Direct or explicit-object path |
| `object` with `oneOf` / `anyOf` | `VariantType` | Polymorphic object — overrides struct treatment |
| `unknown` type with `properties` / `allOf` | `StructType` | Implicit object (no explicit `"type": "object"`) |
| `unknown` type with `oneOf` / `anyOf` | `VariantType` | Discriminator metadata NOT currently captured |
| `allOf` with `$ref` | `StructType::allOf_bases` | C++ multiple inheritance base |
| `allOf` with inline schema | Mangled `{name}_base_{n}` struct + base ref | Inline base extracted as standalone struct |
| `array` items | `ArrayType` | Recursive parse; unnamed arrays get `ArrayEntry_{N}` |
| `string` / `integer` with `enum` values | `PrimitiveType` with `enum_values` | Emitted as `enum class` later |
| Nested object (inline struct in property) | `Parent_Child` struct | Flat name, not `Parent::Child` |
| Nested variant alternative | `{name}_alt_{n}` struct | Flattened if alternative is itself a variant |
| `$ref` anywhere | `TypeRef{name, is_inline=false}` | Name is sanitized from the last path component |

---

## Phase 2: Middle-end — Dependency Analysis

**Entry**: `analysis::DependencyGraph::buildFromAST()` + `analysis::sortTypes()`

1. Iterates all AST types, visiting each `SchemaType` variant
2. For `StructType`: adds `DepKind::Value` edges for field types, `DepKind::Base` for `allOf` bases
3. For `VariantType`: adds `DepKind::Variant` edges for alternatives
4. For `ArrayType` / `MapType`: adds `DepKind::ArrayElem` / `DepKind::MapValue` edges
5. Skips synthetic C++ types (`std::string`, `int64_t`, etc.) via `isSyntheticCppType()`
6. Cycle detection via DFS with recursion-stack tracking
7. Topological sort via Kahn's algorithm (deterministic ordering from `std::queue`)
8. Sorted result filtered to only real AST types (synthetic nodes removed)

### `TopologicalOrder`

```cpp
struct TopologicalOrder {
    std::vector<std::string> ordered_types;
    bool has_cycles;
    std::vector<std::vector<std::string>> cycles;
    bool isValid() const { return !has_cycles && !ordered_types.empty(); }
};
```

Cyclic schemas are a hard error — value-semantic types cannot express cycles. The generator aborts with a clear error listing all cycle paths.

---

## Phase 3: Backend — Code Generation

Endpoints are parsed once into a shared `std::vector<Endpoint>` IR (`endpoint_ir.hpp/.cpp`), then passed to all backends via `CodegenArgs`. Backends consume the IR — they no longer re-parse the OpenAPI spec independently.

```cpp
struct CodegenArgs {
    const schema::NormalizedAST& ast;
    const analysis::TopologicalOrder& order;
    const openapi::v3::OpenAPIv3* spec = nullptr;
    std::string module_name = "siesta_bindings";
    const std::vector<Endpoint>* endpoints = nullptr;  // pre-parsed endpoint IR
};

class ICodeGenerator {
public:
    virtual ~ICodeGenerator() = default;
    virtual void operator()(const CodegenArgs& args, const std::filesystem::path& output_dir) = 0;
};
```

### 3a. DefsGenerator → `openapi_defs.hpp` + `openapi_defs.cpp`

**Header**: forward declarations → structs/aliases → `tag_invoke` declarations, all in topological order.

**Source**: `tag_invoke` bodies for every type.

Key behaviors:
- `allOf` becomes C++ inheritance: `struct Derived : Base { ... }`
- `oneOf`/`anyOf` become `using Name = std::variant<A, B, C>;`
- `additionalProperties` (object-as-map) becomes `using Name = std::map<std::string, T>;`
- Top-level arrays become `using Name = std::vector<T>;`
- Enum primitives become `enum class Name : int { ... };`
- Struct serialization merges base JSON objects before adding derived fields
- Variant deserialization tries each alternative in order via try/catch

### 3b. ClientGenerator → `client.hpp`

Consumes the pre-parsed `Endpoint` IR. Generates `class Client : public ::siesta::beast::ClientBase` with one templated completion-token method per endpoint. Method body emission is decomposed into focused functions: `emitPathParams`, `emitQueryParams`, `emitRequestBody`, `emitHeaderParams`.

```cpp
auto get__api_v3_ping(
    std::optional<int64_t> param_limit,
    std::string param_symbol,
    ::boost::asio::completion_token_for<void(outcome_type)> auto&& token
);
```

Parameter handling:
- **Path params**: `std::string::find` + `replace` of `{}` placeholders in a `std::string target_path`
- **Query params**: all params (required + optional) built into a single `std::string query` buffer via a `_sep` lambda (`[&]{ if (!query.empty()) query += '&'; }`). Required params emit unconditionally; optional params emit inside `if (param.has_value())`. The `?` prefix and append to `target_path` happens once at the end. No separate buffer for optional params, no `target_has_query` flag.
- **`query_value`**: type-specialized overloads for `int32_t`–`bool` use `std::to_string()` / literal booleans; `const std::string&` delegates to `url_encode`; a generic `const auto&` template (round-trip `value_from`→`value_to<string>`) handles complex types like `boost::json::value`.
- **Header params**: `req.set(name, value)`
- **HTTP verb**: uses `ep.cpp_verb` from the endpoint IR (pre-computed during `parseEndpoints()` — `"delete"` → `"delete_"`)
- **Auth**: `HttpBearer` token is pre-computed as `_auth_header("Bearer "+token)` in the constructor and reused per-endpoint as a stored `std::string` member rather than allocated per call. `ApiKey` uses the raw key member directly.
- **Parameter sanitization**: C++ keyword names get `param_` prefix; brackets and special chars become `_`

### 3c. ServerGenerator → `server.hpp` + `server.cpp`

Consumes the pre-parsed `Endpoint` IR. Produces an abstract `openapi::Server` class with one pure-virtual method per endpoint. The `.cpp` file contains the dispatch table:

- **Static paths**: `std::unordered_map<std::pair<std::string_view, http::verb>, fnptr_t>` for O(1) lookup
- **Parameterised paths**: `match_path()` segment-by-segment algorithm over a linear array of patterns
- **404 fallback**: returns `http::status::not_found` when no route matches

### 3d. PythonGenerator → `py_module.cpp`

Consumes the pre-parsed `Endpoint` IR. Generates a nanobind module containing:
- `#include <siesta/beast/python_util.hpp>` for `json_to_python()` and `extract_response_json()` — these live in the shared siesta runtime header rather than being duplicated in every generated module.
- `ClientWrapper` struct: owns `openapi::Client` + `boost::asio::io_context`
- `NB_MODULE(siesta_bindings, m)` with `nb::class_<ClientWrapper>` wrapping every endpoint

Synchronous execution model:
1. Call async client method with `boost::asio::use_future` as last argument
2. Run `ctx.run()` to drain the io_context
3. `future.get()` retrieves the outcome
4. Convert response body to Python via `extract_response_json()`

### 3e. ServerPythonGenerator → `server_py.cpp`

Consumes the pre-parsed `Endpoint` IR. Generates a nanobind trampoline class (`PyServer`) enabling Python-side subclassing of the C++ server. Each virtual method dispatches to a Python override via `nb::detail::ticket`. The module exposes `listen()` and `shutdown()` on the `Server` class.

---

## Data Flow

```
main.cpp
  └─ openapi3_codegen.cpp::generateFromOpenAPI()
       ├─ openapi::OpenAPI::Load()                          [simdjson]
       ├─ buildAST()
       │    ├─ parseSchemas()  ──▶ SchemaParser × N
       │    └─ parsePaths()
       │         └─ schema::NormalizedAST
       ├─ ast.validate()
       ├─ DependencyGraph::buildFromAST()
       ├─ sortTypes()  ──▶ TopologicalOrder
       ├─ parseEndpoints()  ──▶ std::vector<Endpoint>      [shared endpoint IR]
       └─ Phase 4: for each backend
            CodegenArgs args{ast, order, &spec, name, &eps};
            DefsGenerator{}(args, out_dir);                 // openapi_defs.hpp/.cpp
            ClientGenerator{}(args, out_dir);               // client.hpp
            ServerGenerator{}(args, out_dir);               // server.hpp/.cpp
            PythonGenerator{}(args, out_dir);               // py_module.cpp
            ServerPythonGenerator{}(server_args, out_dir);  // server_py.cpp
```

---

## Siesta Runtime Library

The generated code depends on `siesta::beast::{ClientBase,ServerBase,Session}` in `include/siesta/beast/`. These provide the async I/O layer.

### ClientBase

- **Ownership**: `ClientBase` holds an `io_context&` reference (does not own). The caller provides lifetime. `enable_shared_from_this` is used as a lifetime guard in all async callbacks — clients must be heap-allocated in a `shared_ptr`.
- **I/O model**: A single `strand` wraps both resolver and TCP stream. All I/O is serialized through the strand even if multiple threads run the io_context.
- **`start(address, port)`**: initiates DNS resolution → TCP connect. Stores the connected host as `_host_value` (used later as the `Host` header).
- **`async_submit_request(req, token)`**: the sole public async entry point. Uses `asio::async_compose` with a 3-state FSM (send → recv → done) that is local to the compose lambda (no shared mutable state on the class). Sets `Host` header from `_host_value` before sending. Completes with `outcome_type` (either the HTTP response or an error).
- **Config**: `connect_timeout`, `write_timeout`, `read_timeout` (default 1000 ms each).

### ServerBase

- **Ownership**: stores `io_context* _ctx` (pointer, not reference — stored in constructor, used in `start()`). No `shared_from_this` requirement at this level.
- **`start(address, port)`**: opens, binds, and listens on the acceptor. Takes no `io_context&` parameter — uses the stored `*_ctx`. Starts the `async_accept` loop with strand-serialized completion handlers.
- **`handle_request(const request, Session::Ptr)`**: pure virtual. Derived classes implement request dispatch.
- **Config**: `read_timeout` (default 1 hour) and `write_timeout` (default 30 seconds).

### Session

- **Lifecycle**: per-connection, always heap-allocated (`make_shared`). Inherits `enable_shared_from_this<Session>`.
- **Request pipeline**: `do_read()` → `on_read()` calls `parent.handle_request(move(request), shared_from_this())` → handler fills `get_response()` → calls `write()` → `on_write()` loops back to `do_read()`. `shared_from_this()` keeps the session alive while the handler holds the shared pointer.
- **I/O**: runs on the socket's native executor (no explicit strand). `run()` uses `asio::post` for guaranteed deferred dispatch. All async callbacks use lambdas with `[self = shared_from_this()]` capture.
- **Timeouts**: read timeout and write timeout are applied before `async_read`/`async_write` respectively. Configured via `ServerBase::Config`.
- **Close**: `do_close()` performs `shutdown(send)` on the socket. The destructor calls `do_close()` via RAII.

---

## Design Decisions

### 1. allOf → C++ Inheritance
`allOf` with `$ref` bases becomes C++ multiple inheritance: `struct Derived : Base { ... }`. Serialization merges base JSON objects into the derived object. `allOf` with inline schemas extracts them as standalone structs (`{name}_base_{N}`).

### 2. Nested Types → Flat Naming
Inline structs within parent types use `Parent_Child` naming (not `Parent::Child`). This simplifies dependency tracking — C++ `::` would require qualification that makes forward-declaration ordering fragile.

### 3. Variant Deduplication
Duplicate `std::variant<A, B>` signatures become `using` aliases. The first variant with a given signature wins; subsequent identical signatures become `using TypeB = TypeA;`. A `typedef_chain_` map tracks these aliases for recursive resolution. Signatures are canonicalised by resolving typedef chains on alternatives first, giving a stable string like `variant<int64_t,std::string>`.

### 4. Single-Alternative Variant Collapse
A variant with exactly one alternative and no null marker collapses to a typedef: `using X = std::string;`. This handles OpenAPI schemas that declare a single-allowed-type via `oneOf`. The collapse is tracked in `typedef_chain_` so downstream types that reference the variant resolve to the concrete type.

### 5. Synthetic Type Filtering
`isSyntheticCppType()` gates the dependency graph — any type starting with `std::` or matching a C++ primitive keyword (`int`, `double`, `bool`, etc.) is excluded from dependency tracking and topological sort. Without this, the dep graph would balloon with synthetic edges between `std::vector` and `std::string`, etc.

### 6. Enum from Primitives
String/integer primitives with `enum` values in the OpenAPI spec are emitted as `enum class Name : int { ... }` rather than simple `using` typedefs. This provides type safety at the C++ level. Enum value identifiers pass through `sanitize_enum_identifier()` which handles dots, leading digits, and C++ reserved words.

### 7. Parameter Sanitization
Parameter names that collide with C++ keywords (`token`, `result`, `error`, `next`, `type`, `metadata`, `include`, `order`, `event_types`) get a `param_` prefix. Brackets, parentheses, dots, and commas are replaced with `_`.

### 8. GCC/Clang Predefined Macros
Names matching GCC/Clang predefined macros (`unix`, `linux`, `x86_64`, `__unix__`, etc.) get a trailing `_` appended by `sanitize()`. Without this, they silently expand to `1` at compile time, producing cryptic errors.

### 9. Path Construction via find+replace
Path templates use `std::string::find` + `replace` instead of `std::format`. This avoids requiring `<format>` (and `<regex>`) in generated client headers, keeping the generated code compatible with older standard library implementations.

### 10. ICodeGenerator Interface
All five backends share a single abstract interface. Constructors receive only per-backend configuration (`PythonGenerator` takes a module name; the other four take nothing). All data needed for generation flows in through `operator()(const CodegenArgs&, const fs::path&)`. This separates configuration from execution and lets the pipeline call every generator through the same polymorphic pattern.

### 11. Namespace Organization
Utility functions live in `namespace codegen` (moved from the global namespace during refactoring). File-local callers in non-`codegen` TUs use `using codegen::fn;` at file scope. The `endpoint_ir.hpp` header declares the shared endpoint IR and `parseEndpoints()` in `namespace codegen` — the implementation lives in `endpoint_ir.cpp` for clean compilation-unit separation.

---

## Edge Cases & Limitations

### Currently Handled

| Case | Mechanism |
|------|-----------|
| Empty variant (no alternatives, not nullable) | Emitted as `std::monostate` |
| Single-alternative variant (not nullable) | Collapses to typedef (`using X = string;`) — tracked in `typedef_chain_` |
| Duplicate variant signatures | Second occurrence becomes `using New = Existing;` |
| Variant with `std::nullptr_t` (nullable) | `nullptr_t` added as final alternative — NOT collapsed even if singleton |
| Nested variant alternatives | Flattened into the parent variant (`buildVariant` checks for `VariantType` in alternatives) |
| `allOf` with both `$ref` and inline properties | ref → base class; inline → struct field |
| `allOf` with inline object | Object extracted as `{name}_base_{N}` standalone struct |
| Implicit object (no `"type"`, but has properties/allOf) | Treated as `StructType` via `parseImplicitObject()` |
| `additionalProperties` on an object | Treated as `std::map<std::string, ValueType>` |
| String enum values with dots/special chars | `sanitize_enum_identifier()` replaces dots with `_`, adds `_` prefix for leading digits |
| Reserved C++ identifiers as type/param names | `sanitize()` appends `_`; `sanitizeParamName()` adds `param_` prefix |
| Path parameters with `{}` format specifiers | Placeholder replaced with `{}` for `find`+`replace` at call site |
| `delete` HTTP verb | Emitted as `boost::beast::http::verb::delete_` |
| `$ref` to component parameters | Resolved via pre-fetched `fetched_params` map |
| Operation-level params overriding path-level params | `op_overrides` map + `lookup` lambda prefers operation-level |

### Known Limitations

1. **Cyclic dependencies**: Not supported — value semantics prohibit cycles. The generator detects them and aborts with a clear error.
2. **Polymorphic dispatch**: `oneOf` / `anyOf` generates `std::variant` but does not emit runtime discriminator-based dispatch. The schema `discriminator.propertyName` field is parsed but not acted upon.
3. **Schema validation**: Minimal OpenAPI spec validation. Invalid schemas may produce confusing errors rather than early rejection.
4. **Complex `$ref` chains**: Multi-hop `$ref` chains in parameters (e.g., `$ref` → `$ref` → inline) may not fully resolve.
5. **Request body content types**: Only the first content-type entry is used for generated request body code.
6. **Server URLs / authentication**: Not generated — the client class accepts host/port at construction but does not parse OpenAPI `servers` or `securitySchemes`.
7. **Response type generation**: All endpoints return `siesta::beast::ClientBase::outcome_type` (a `boost::system::result` of the HTTP response). Structured response types from the schema are not generated or validated.
8. **Query parameter arrays of non-string types**: Multi-valued query params for non-primitive arrays use `query_value()` which serializes each element as JSON — this may not match all server expectations.
9. **simdjson single-pass ranges**: simdjson's `dom::object` / `dom::array` iterators are single-pass — re-entering `begin()` on an already-consumed range triggers a debug assertion (`tape.usable()`). The fix is pre-fetching all component data (parameters, request bodies, security schemes) and endpoint data into C++ containers before iterating paths. The `endpoint_ir.cpp` `parseEndpoints()` iterates paths exactly once, materialising all extracted data before returning.

---

## Logging

All logging goes to stderr. Phase tags enable filtering:

```bash
./siesta-generator --input spec.json --output out/ 2>&1 | grep '\[EMIT\]'
./siesta-generator --input spec.json --output out/ 2>&1 | grep '\[DEP\]'
```

Tags: `PARSE`, `DEP`, `SORT`, `EMIT`

---

## Build & Test

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

### Quick Sanity Check

```bash
./build/generator/siesta-generator --input tests/echo.json --output /tmp/test/ 2>&1 | grep -E '(AST summary|Path endpoints|types present|WARNING|cycle)'
```

### Debugging Compilation Errors

1. Search for undefined type names in `openapi_defs.hpp` — types referenced but not forward-declared
2. Check `namespace api { ... }` — all generated types live here; client code uses `api::TypeName`
3. Check C++ keyword conflicts — `delete`, `class`, `template`, `operator` in identifiers
4. Check variant ordering — variant alternatives must be defined before the variant that references them
5. Verify `NB_MODULE` name matches CMake target name for Python import
