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
| `py_module.cpp` | Nanobind Python extension module wrapping the C++ client synchronously via `boost::asio::use_future` |

---

## File Index

| File | Role |
|------|------|
| `src/main.cpp` | CLI entry point, argument parsing |
| `src/openapi3_codegen.cpp` / `.hpp` | Pipeline orchestrator — runs all phases sequentially |
| `src/openapi.hpp` / `.cpp` | simdjson wrapper, base OpenAPI accessors, `ListAdaptor` / `MapAdaptor` |
| `src/openapi3.hpp` / `.cpp` | OpenAPI v3-specific parsed types (schemas, paths, operations, components) |
| `src/schema_ast.hpp` / `.cpp` | Normalized AST definitions (`StructType`, `VariantType`, `ArrayType`, etc.) + validation |
| `src/schema_parser.hpp` | `SchemaParser` — converts raw JsonSchema → AST nodes (header-only) |
| `src/dependency_graph.hpp` / `.cpp` | `DependencyGraph` + Kahn's topological sort + cycle detection |
| `src/codegen_base.hpp` | `CodegenArgs` struct + `ICodeGenerator` abstract interface |
| `src/codegen_defs.hpp` / `.cpp` | `DefsGenerator : ICodeGenerator` — emits type definitions + ser/des |
| `src/codegen_client.hpp` / `.cpp` | `ClientGenerator : ICodeGenerator` — emits async client class |
| `src/codegen_python.hpp` / `.cpp` | `PythonGenerator : ICodeGenerator` — emits nanobind module |
| `src/endpoint_util.hpp` | Shared endpoint helpers: `refComponentName`, `resolveRefName`, `generateFunctionName`, etc. |
| `src/util.hpp` / `.cpp` | Shared utilities: `sanitize`, `escapeCppString`, `primitiveToCpp`, logging macros |
| `CMakeLists.txt` | Builds `siesta-generator` — links simdjson, Boost (json, system, program_options) |

---

## Phase 1: Frontend — Schema Normalization

**Entry**: `openapi3_codegen.cpp::buildAST()` → `parseSchemas()` + `parsePaths()`

1. `openapi::OpenAPI::Load()` reads the JSON file into simdjson's on-demand DOM
2. `static_cast<const openapi::v3::OpenAPIv3&>` casts to a v3-specific typed view
3. `parseSchemas()` iterates `components/schemas`, calling `SchemaParser::parseSchema()` for each entry
4. `parsePaths()` collects path/operation metadata into `PathItem` objects
5. Result: `schema::NormalizedAST` containing all types and paths

### SchemaParser (header-only: `schema_parser.hpp`)

Dispatches on `JsonSchema::Type_()` (string / integer / number / boolean / object / array / unknown):

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

All three generators implement `ICodeGenerator` (`codegen_base.hpp`):

```cpp
struct CodegenArgs {
    const schema::NormalizedAST& ast;
    const analysis::TopologicalOrder& order;
    const openapi::v3::OpenAPIv3* spec = nullptr;  // only Client/Python backends need this
};

class ICodeGenerator {
public:
    virtual ~ICodeGenerator() = default;
    virtual void operator()(const CodegenArgs& args, const std::filesystem::path& output_dir) = 0;
};
```

The pipeline calls each generator with a single `(*gen)(args, output_path)` invocation. DefsGenerator is treated as a single logical operation despite producing two files — the asymmetry is its internal concern.

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

Parses all paths/operations from the spec, pre-fetching component parameters and request bodies to avoid simdjson on-demand re-iteration bugs. Generates `class Client : public ::siesta::beast::ClientBase` with one templated completion-token method per endpoint:

```cpp
auto get__api_v3_ping(
    std::optional<int64_t> param_limit,
    std::string param_symbol,
    ::boost::asio::completion_token_for<void(outcome_type)> auto&& token
);
```

Parameter handling:
- **Path params**: `std::regex_replace` / `std::to_string` in a constructed `target_path` string
- **Query params**: string concatenation; non-primitive values serialized via `query_value()` (uses `boost::json::value_from`)
- **Header params**: `req.set(name, value)`
- **HTTP verb**: `delete` → `verb::delete_` (C++ keyword workaround)
- **Parameter sanitization**: C++ keyword names get `param_` prefix; brackets and special chars become `_`

### 3c. PythonGenerator → `py_module.cpp`

Parses the same endpoints as ClientGenerator (parallel implementation for different output struct: `PyEndpoint`).

Generates a nanobind module containing:
- `json_to_python()`: recursive `boost::json::value` → Python dict / list / primitive
- `extract_response_json()`: HTTP body → JSON parse → Python
- `ClientWrapper` struct: owns `openapi::Client` + `boost::asio::io_context`
- `NB_MODULE(siesta_bindings, m)` with `nb::class_<ClientWrapper>` wrapping every endpoint

Synchronous execution model:
1. Call async client method with `boost::asio::use_future` as last argument
2. Run `ctx.run()` to drain the io_context
3. `future.get()` retrieves the outcome
4. Convert response body to Python via `extract_response_json()`

Each endpoint becomes a `.def("name", [](ClientWrapper& self, params...) -> nb::object { ... })` with nanobind `nb::arg()` documentation.

---

## Data Flow

```
main.cpp
  └─ openapi3_codegen.cpp::generateFromOpenAPI()
       ├─ openapi::OpenAPI::Load()                       [simdjson]
       ├─ buildAST()
       │    ├─ parseSchemas()  ──▶ SchemaParser × N
       │    └─ parsePaths()
       │         └─ schema::NormalizedAST
       ├─ ast.validate()
       ├─ DependencyGraph::buildFromAST()
       ├─ sortTypes()  ──▶ TopologicalOrder
       └─ Phase 4: for each backend
            CodegenArgs args{ast, order, &spec};
            DefsGenerator{}(args, out_dir);
            ClientGenerator{}(args, out_dir);
            PythonGenerator{"siesta_bindings"}(args, out_dir);
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
Utility functions live in `namespace codegen` (moved from the global namespace during refactoring). File-local callers in non-`codegen` TUs use `using codegen::fn;` at file scope. The `endpoint_util.hpp` header provides shared endpoint helpers in `namespace codegen` as inline functions — no external linkage overhead for small utilities.

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
9. **simdjson on-demand re-iteration**: simdjson's on-demand API asserts when iterating the same object region from a different parent. The fix is pre-fetching `components/parameters` and `components/requestBodies` into C++ containers before any path iteration loop. This is done in both `ClientGenerator` and `PythonGenerator`.

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
14. **`ICodeGenerator` interface**: All three backends inherit from a common abstract base — single `operator()` call per generator, constructors receive only per-backend config

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
