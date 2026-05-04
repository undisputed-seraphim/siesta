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

| File | Role |
|------|------|
| `src/main.cpp` | CLI entry point, argument parsing |
| `src/openapi3_codegen.cpp` / `.hpp` | Pipeline orchestrator — runs all phases sequentially |
| `src/openapi.hpp` / `.cpp` | simdjson wrapper, base OpenAPI accessors, `ListAdaptor` / `MapAdaptor` |
| `src/openapi3.hpp` / `.cpp` | OpenAPI v3-specific parsed types (schemas, paths, operations, components) |
| `src/schema_ast.hpp` / `.cpp` | Normalized AST definitions (`StructType`, `VariantType`, `ArrayType`, etc.) + validation |
| `src/schema_parser.hpp` / `.cpp` | `SchemaParser` — converts raw JsonSchema → AST nodes (split: declarations in header, implementations in cpp) |
| `src/dependency_graph.hpp` / `.cpp` | `DependencyGraph` + Kahn's topological sort + cycle detection |
| `src/codegen_base.hpp` | `CodegenArgs` struct + `ICodeGenerator` abstract interface |
| `src/endpoint_ir.hpp` / `.cpp` | Shared endpoint IR: `Endpoint` struct, `ClientParam`, `AuthType`, plus `parseEndpoints()` — parsed once, consumed by all backends |
| `src/codegen_defs.hpp` / `.cpp` | `DefsGenerator : ICodeGenerator` — emits type definitions + ser/des |
| `src/codegen_client.hpp` / `.cpp` | `ClientGenerator : ICodeGenerator` — emits async client class |
| `src/codegen_server.hpp` / `.cpp` | `ServerGenerator : ICodeGenerator` — emits abstract server class |
| `src/codegen_python.hpp` / `.cpp` | `PythonGenerator : ICodeGenerator` — emits nanobind client module |
| `src/codegen_server_python.hpp` / `.cpp` | `ServerPythonGenerator : ICodeGenerator` — emits nanobind server trampoline |
| `src/util.hpp` / `.cpp` | Shared utilities: `sanitize` (with `static const unordered_set` reserved-keyword table), `escapeCppString`, `primitiveToCpp`, logging macros |
| `CMakeLists.txt` | Builds `siesta-generator` — links simdjson, Boost (json, system, program_options) |

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

Consumes the pre-parsed `Endpoint` IR. Generates `class Client : public ::siesta::beast::ClientBase` with one templated completion-token method per endpoint. Method body emission is decomposed into focused functions: `emitPathParams`, `emitRequiredQueryParams`, `emitOptionalQueryParams`, `emitRequestBody`, `emitHeaderParams`.

```cpp
auto get__api_v3_ping(
    std::optional<int64_t> param_limit,
    std::string param_symbol,
    ::boost::asio::completion_token_for<void(outcome_type)> auto&& token
);
```

Parameter handling:
- **Path params**: `std::string::find` + `replace` of `{}` placeholders in a constructed `target_path`
- **Query params**: string concatenation; non-primitive values serialized via `query_value()` (uses `boost::json::value_from`)
- **Header params**: `req.set(name, value)`
- **HTTP verb**: `delete` → `verb::delete_` (C++ keyword workaround)
- **Parameter sanitization**: C++ keyword names get `param_` prefix; brackets and special chars become `_`

### 3c. ServerGenerator → `server.hpp` + `server.cpp`

Consumes the pre-parsed `Endpoint` IR. Produces an abstract `openapi::Server` class with one pure-virtual method per endpoint. The `.cpp` file contains the dispatch table:

- **Static paths**: `std::unordered_map<std::pair<std::string_view, http::verb>, fnptr_t>` for O(1) lookup
- **Parameterised paths**: `match_path()` segment-by-segment algorithm over a linear array of patterns
- **404 fallback**: returns `http::status::not_found` when no route matches

### 3d. PythonGenerator → `py_module.cpp`

Consumes the pre-parsed `Endpoint` IR (no longer duplicates ClientGenerator's endpoint parsing). Generates a nanobind module containing:
- `json_to_python()`: recursive `boost::json::value` → Python dict / list / primitive
- `extract_response_json()`: HTTP body → JSON parse → Python
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
All three backends share a single abstract interface. Constructors receive only per-backend configuration (`PythonGenerator` takes a module name; the other two take nothing). All data needed for generation flows in through `operator()(const CodegenArgs&, const fs::path&)`. This separates configuration from execution and lets the pipeline call every generator through the same polymorphic pattern.

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

## Refactoring History

Recent structural improvements (committed in atomic steps with verification):

1. **Bugfix**: simdjson on-demand re-iteration crash — pre-fetched components before path loops
2. **Bugfix**: `ListAdaptor::front()` dangling reference — returns `T` by value
3. **Remove `using namespace std::literals` from headers** — kept only in `util.cpp`
4. **`std::regex_replace` → `find`+`replace`** in generated code — removes `<regex>` dependency from generated headers
5. **Dead code removal**: `transform_url_to_function_signature`, `QUERY_VALUE_HELPER`, `ltrim`, `rtrim`, `trim`, `compare_ignore_case`, `decompose_http_query`
6. **DRY endpoint parsing** → shared `endpoint_util.hpp` (73 new shared lines, 117 duplicated deleted)
7. **Move utility functions into `namespace codegen`** — avoids global namespace pollution
8. **Move `dependency_graph.hpp` heavy methods to `.cpp`** — 225 lines out of the header
9. **Fix full `unordered_map` copy per endpoint** — replaced with `op_overrides` + reference lookup
10. **`constexpr` annotations**: `isSyntheticCppType()`, `component_path()`; `noexcept` on `getType()` accessors
11. **`validate()`/`validateType()` moved** from `schema_ast.hpp` to `schema_ast.cpp`
12. **Cleanups**: `std::endl` → `'\n'`, `escapeCppString` pre-allocation with `reserve()`, remove unused `#include <siesta/asio/queue.hpp>`
13. **`buildAST()` split**: `parseSchemas()` + `parsePaths()` — two focused functions from one 85-line monolith
14. **`ICodeGenerator` interface**: All backends inherit from a common abstract base — single `operator()` call per generator, constructors receive only per-backend config
15. **Shared endpoint IR** (`endpoint_ir.hpp/cpp`): Unified `Endpoint` struct, `parseEndpoints()`, `resolveParameter()`, `schemaToCppType()` extracted from duplicated backend code. Endpoints parsed once, consumed by all five backends. Removed `endpoint_util.hpp`. Net -414 lines across backends.
16. **Decompose `schema_parser`**: 513-line header-only file split into declarations (`.hpp`) + implementations (`.cpp`). `parseSchema` (was 255-line switch) decomposed into `parseObjectSchema`, `parseArraySchema`, `parsePrimitiveSchema`, `parseUnknownSchema` — top-level dispatch now ~20 lines.
17. **Decompose `emitMethodBody`**: 143-line client method body split into 5 focused functions: `emitPathParams`, `emitRequiredQueryParams`, `emitOptionalQueryParams`, `emitRequestBody`, `emitHeaderParams`. Orchestrator now ~40 lines.
18. **Table-drive `sanitize`**: Reserved C++ keywords moved from `constexpr std::array` + `std::any_of` (O(n) linear scan over 71 entries) to `static const std::unordered_set<std::string_view>` (O(1) average lookup).
19. **Unified query-string building**: `emitRequiredQueryParams` + `emitOptionalQueryParams` merged into `emitQueryParams`. Single `query` buffer with `_sep` lambda replaces the dual-path (required-direct-to-target / optional-buffer-then-merge) pattern. Eliminates redundant `target_has_query` bool and N surface-level ternaries.
20. **Pre-computed auth header**: `HttpBearer` `"Bearer " + token` is constructed in the Client constructor under `_auth_header`. Per-endpoint methods use the stored string instead of allocating a temporary each call.
21. **Type-specialized `query_value`**: Explicit overloads for `int32_t`–`bool` use `std::to_string()` / literal `"true"`/`"false"` instead of round-trip `value_from`→`value_to<string>`. A generic `const auto&` template remains as a fallback for complex types like `boost::json::value`.
22. **Centralized `delete_` conversion**: `Endpoint::cpp_verb` field, populated once during `parseEndpoints()`, replaces repeated `ep.method == "delete" ? "delete_"` checks in client, server (static paths), and server (param paths) emitters.
23. **Shared `siesta/beast/python_util.hpp`**: `json_to_python` and `extract_response_json` (~90 lines) moved from per-schema inline emission into a single siesta runtime header, included by all generated `py_module.cpp` files.
24. **Generated code quality**: Removed unnecessary `static_cast<std::string_view>(path)`, `std::move` on NRVO returns, `std::string_view(req.target())` redundant constructor; replaced `using namespace std::literals` with targeted `using std::literals::string_view_literals::operator""sv`.

## Siesta Runtime Refactors

25. **Removed legacy `async_result`/`async_request`**: Dead code (40 lines) superseded by `async_submit_request`; removed `#include <boost/asio/async_result.hpp>`.

26. **Host header**: `ClientBase` now stores `_host_value` (address:port) from `start()` and sets `http::field::host` in `async_submit_request` — Beast requires explicit Host for HTTP/1.1 clients.

27. **`_state` moved into compose lambda**: Eliminates shared mutable state on `ClientBase` between composed-operation invocations. No more `enum { send, recv, done }` member.

28. **ServerBase API cleanup**: `io_context* _ctx` stored as member; `start(address, port)` no longer requires an `io_context&` parameter. Eliminated fragile `[&ctx]` dangling-reference captures in `start()` and `on_accept()`.

29. **ServerBase Config**: `read_timeout` (default 1h) and `write_timeout` (default 30s) added to the previously-empty `Config` struct. `Session::do_read()` and `Session::write()` now use config values instead of hardcoded constants.

30. **`asio::dispatch` → `asio::post`**: `Session::run()` now uses `post` to guarantee deferred execution, avoiding potential deep recursion.

31. **`std::bind_front` → lambdas**: All 4 async callback sites (client resolve/connect, session do_read/write) replaced with explicit lambdas — better inlining, no binder overhead.

32. **MapHash improved**: `_1 ^ (_2 << _1)` → `_1 ^ (_2 + 0x9e3779b9 + (_1 << 6) + (_1 >> 2))` (boost::hash_combine), reducing collision bias for similar strings.

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

# Generate from a spec
./build/generator/siesta-generator --input tests/binance.json --output binance/

# Compile generated code
cd binance/build && cmake .. -DSIESTA_ROOT=/path/to/siesta && ninja -j$(nproc)

# Test Python import
python3 -c "import sys; sys.path.insert(0,'binance/build'); import siesta_bindings; print('OK')"

# Run C++ unit tests
./build/tests/siesta_test
```

### Quick Sanity Check

```bash
./build/generator/siesta-generator --input tests/binance.json --output /tmp/test/ 2>&1 | grep -E '(AST summary|Path endpoints|types present|WARNING|cycle)'
```

### Debugging Compilation Errors

1. Search for undefined type names in `openapi_defs.hpp` — types referenced but not forward-declared
2. Check `namespace api { ... }` — all generated types live here; client code uses `api::TypeName`
3. Check C++ keyword conflicts — `delete`, `class`, `template`, `operator` in identifiers
4. Check variant ordering — variant alternatives must be defined before the variant that references them
5. Verify `NB_MODULE` name matches CMake target name for Python import
