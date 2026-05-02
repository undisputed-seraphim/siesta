# Generator AGENTS.md

## Architecture

The generator is a C++23 compiler-like tool that transforms OpenAPI v3 JSON specifications into production-grade C++ client code and Python extension modules. It follows a three-phase pipeline:

```
OpenAPI v3 JSON ──▶ Phase 1: AST ──▶ Phase 2: Dep Graph ──▶ Phase 3: Codegen
```

### File Index

| File | Role |
|------|------|
| `main.cpp` | CLI entry point, argument parsing |
| `openapi3_codegen.cpp/.hpp` | Pipeline orchestrator (calls all phases sequentially) |
| `openapi.hpp/.cpp` | simdjson wrapper, base OpenAPI objects, `ListAdaptor`/`MapAdaptor` |
| `openapi3.hpp/.cpp` | OpenAPI v3-specific types (schemas, paths, operations, components) |
| `schema_ast.hpp/.cpp` | Normalized AST type definitions (`StructType`, `VariantType`, etc.) |
| `schema_parser.hpp` | `SchemaParser::parseSchema()` — converts raw JsonSchema to AST nodes |
| `dependency_graph.hpp/.cpp` | `DependencyGraph` + Kahn's algorithm topological sort |
| `codegen_defs.hpp/.cpp` | `DefsGenerator` — emits `openapi_defs.hpp/cpp` (type definitions + ser/des) |
| `codegen_client.hpp/.cpp` | `ClientGenerator` — emits `client.hpp` (async C++ client class) |
| `codegen_python.hpp/.cpp` | `PythonGenerator` — emits `py_module.cpp` (nanobind Python bindings) |
| `util.hpp/.cpp` | Shared utilities (sanitize, escape, logging macros) |

### Phase 1: Frontend — Schema Normalization

**Entry**: `openapi3_codegen.cpp::buildAST()`

1. `openapi::OpenAPI::Load()` reads the JSON file via simdjson
2. `static_cast<const openapi::v3::OpenAPIv3&>` casts to v3-specific view
3. For each `components/schemas` entry, `SchemaParser::parseSchema()` is called
4. Path/operation metadata is collected into `PathItem` objects
5. Result: `schema::NormalizedAST` containing all types and paths

**`SchemaParser`** (header-only in `schema_parser.hpp`):
- Dispatches on `JsonSchema::Type_()` (string/integer/number/boolean/object/array/unknown)
- `object` with `oneOf`/`anyOf` → `buildVariant()` (polymorphic types)
- `object` with `properties`/`allOf` → `parseImplicitObject()` or inline struct parsing
- `allOf` with `$ref` → base class tracking (`StructType::allOf_bases`)
- `array` items → recursive parse, creates `_entry` suffixed names
- `string`/`integer` with `enum` values → `PrimitiveType::enum_values` (becomes `enum class`)
- Nested types get mangled names: `Parent_Child`, `Parent_alt_0`, `Parent_base_1`
- Inline types set `TypeRef::is_inline = true` with full C++ type name

**Key AST types** (`schema_ast.hpp`):
```cpp
using SchemaType = std::variant<StructType, VariantType, ArrayType, MapType, EnumType, PrimitiveType>;

struct StructType { std::string name; std::vector<Member> fields; std::vector<TypeRef> allOf_bases; };
struct VariantType { std::string name; std::vector<TypeRef> alternatives; bool is_nullable; };
struct ArrayType  { TypeRef element_type; };
struct MapType    { TypeRef value_type; };
struct EnumType   { std::string name; std::vector<EnumValue> values; bool is_string; };
struct PrimitiveType { PrimitiveKind kind; std::optional<IntegerFormat> int_format; ... };
struct Member    { std::string name; TypeRef type; bool required; };
struct PathItem  { std::string path; std::unordered_map<std::string, std::string> operations; };
```

### Phase 2: Middle-end — Dependency Analysis

**Entry**: `analysis::DependencyGraph::buildFromAST()` + `analysis::sortTypes()`

1. Iterates all AST types, visiting each `SchemaType` variant
2. For `StructType`: adds `DepKind::Value` edges for field types, `DepKind::Base` for `allOf`
3. For `VariantType`: adds `DepKind::Variant` edges for alternatives
4. For `ArrayType`/`MapType`: adds `DepKind::ArrayElem`/`DepKind::MapValue` edges
5. Skips synthetic C++ types (`std::vector`, `int64_t`, etc.) via `isSyntheticCppType()`
6. Detects cycles via DFS with recursion stack
7. Topological sort via Kahn's algorithm (deterministic ordering)
8. Filters result to only AST types (removes synthetic nodes from sort)

**`TopologicalOrder`** result struct:
```cpp
struct TopologicalOrder {
    std::vector<std::string> ordered_types;
    bool has_cycles;
    std::vector<std::vector<std::string>> cycles;
};
```

### Phase 3: Backend — Code Generation

Three independent generators run sequentially, each receiving `TopologicalOrder` + `NormalizedAST`:

#### 3a. DefsGenerator → `openapi_defs.hpp` + `openapi_defs.cpp`

**Header** (`openapi_defs.hpp`):
- Forward declarations for all types in topological order
- Struct definitions with `allOf` inheritance: `struct Derived : Base { ... };`
- Variant typedefs: `using TypeName = std::variant<A, B>;`
- Enum class definitions: `enum class Name : int { VAL1, VAL2 };`
- Primitive typedefs: `using StringId = std::string;`
- Array/Map aliases: `using Items = std::vector<Item>;`
- Variant deduplication: duplicate signatures become `using A = B;` with typedef chain tracking
- Single-alternative variant collapse: `using X = std::string;`

**Source** (`openapi_defs.cpp`):
- `tag_invoke` for `boost::json::value_from` (serialization)
- `tag_invoke` for `boost::json::value_to` (deserialization)
- Struct serialization merges base class objects then adds derived fields
- Variant deserialization tries each alternative in order with try/catch
- Enum serialization maps enum values to/from JSON strings

#### 3b. ClientGenerator → `client.hpp`

- Parses all paths/operations from the OpenAPI spec
- Builds `ClientEndpoint` objects with path templates, parameters, body info
- Generates `class Client : public ::siesta::beast::ClientBase`
- Each endpoint becomes a templated callback method:
  ```cpp
  auto get__api_v3_ping(std::optional<int64_t> param_limit,
                        ::boost::asio::completion_token_for<void(outcome_type)> auto&& token);
  ```
- Path parameters: `std::regex_replace` for string types, `std::to_string` for numeric
- Query parameters: string concatenation with `query_value()` helper for non-primitives
- Header parameters: `req.set(name, value)`
- Verb `delete` → `verb::delete_` (C++ keyword workaround)
- Parameter names sanitized: C++ keywords get `param_` prefix, brackets replaced with `_`

#### 3c. PythonGenerator → `py_module.cpp`

- Reuses same `parseEndpoints()` logic as `ClientGenerator` (duplicated, not shared)
- Generates nanobind module with:
  - `json_to_python()` helper: `boost::json::value` → Python dict/list/primitive
  - `extract_response_json()` helper: HTTP response body → JSON → Python
  - `ClientWrapper` struct: owns `openapi::Client` + `boost::asio::io_context`
  - `NB_MODULE(siesta_bindings, m)` with `nb::class_<ClientWrapper>`
  - Each endpoint as `.def("method_name", [](ClientWrapper& self, ...) { ... })`
- Synchronous execution model:
  1. Call async client method with `boost::asio::use_future`
  2. Run `ctx.run()` to process the request
  3. Get result from `future.get()`
  4. Convert response to Python object
- Parameter names also sanitized (same list as ClientGenerator)

### Data Flow Summary

```
main.cpp
  └─ openapi3_codegen.cpp::generateFromOpenAPI()
       ├─ openapi::OpenAPI::Load()              [simdjson]
       ├─ buildAST() ──▶ SchemaParser::parseSchema() × N
       │     └─ schema::NormalizedAST
       ├─ ast.validate()
       ├─ DependencyGraph::buildFromAST()
       ├─ sortTypes() ──▶ TopologicalOrder
       ├─ DefsGenerator::generateDefsHpp/Cpp()  ──▶ openapi_defs.hpp/cpp
       ├─ ClientGenerator::generateClientHpp()  ──▶ client.hpp
       └─ PythonGenerator::generatePythonModule() ──▶ py_module.cpp
```

## Key Design Decisions

1. **allOf = C++ inheritance**: `struct Derived : Base` with merged serialization
2. **Nested types = flat naming**: `Parent_Child` instead of `Parent::Child`
3. **Variant deduplication**: Duplicate `std::variant<...>` signatures become `using` aliases
4. **Single-alternative collapse**: Variants with one non-nullable alternative become typedefs
5. **Synthetic type filtering**: `std::string`, `int64_t` etc. are not real AST nodes
6. **Enum from primitives**: String primitives with `enum` values become `enum class`
7. **Parameter sanitization**: C++ keywords → `param_` prefix, brackets → `_`
8. **GCC predefined macros**: `linux`, `unix`, `x86_64` etc. are reserved identifiers
9. **String concatenation for paths**: `std::regex_replace` instead of `std::format` for compatibility
10. **`query_value()` helper**: Uses `boost::json::value_from` to serialize non-primitive query params

## Logging

All generator logging goes to stderr with phase tags for easy filtering:

```bash
# Filter by phase
./siesta-generator --input spec.json --output out/ 2>&1 | grep '\[EMIT\]'
./siesta-generator --input spec.json --output out/ 2>&1 | grep '\[DEP\]'

# Phases: PARSE, DEP, EMIT, SORT
```

## Manual Testing Strategies

### 1. Build the Generator

```bash
cd /home/shurelia/siesta/build
cmake .. -GNinja
ninja siesta-generator
```

Verify the binary exists:
```bash
ls -la build/generator/siesta-generator
```

### 2. Generate from a Test Spec

```bash
# Binance (smaller, good for quick iteration)
./build/generator/siesta-generator --input tests/binance.json --output binance/

# OpenAI (larger, tests edge cases)
./build/generator/siesta-generator --input openai.json --output openai/
```

Check output:
```bash
ls -la binance/openapi_defs.hpp binance/openapi_defs.cpp binance/client.hpp binance/py_module.cpp
```

### 3. Verify Type Counts

```bash
# Check AST summary in stderr output
./build/generator/siesta-generator --input tests/binance.json --output /tmp/test/ 2>&1 | grep -E '(AST summary|Path endpoints|types present)'

# Count generated types
grep -c '^struct \|^enum class \|^using ' binance/openapi_defs.hpp
```

### 4. Check for Warnings

```bash
# Any types missing from topological order
./build/generator/siesta-generator --input tests/binance.json --output /tmp/test/ 2>&1 | grep -i 'warning\|cycle\|missing'
```

### 5. Compile Generated Code (C++ Client)

```bash
cd binance/build
rm -rf *
cmake .. -DSIESTA_ROOT=/home/shurelia/siesta
make -j$(nproc)
```

### 6. Compile Python Extension Module

```bash
cd binance/build
make -j$(nproc)
```

### 7. Test Python Import

```python
import sys
sys.path.insert(0, '/home/shurelia/siesta/binance/build')
import _siesta_binance
print('OK:', _siesta_binance.Client)
print('Methods:', [m for m in dir(_siesta_binance.Client) if not m.startswith('_')][:10])
```

### 8. Edge Case Testing

When testing new or unusual OpenAPI schemas:

1. **Check for nested variants**: Look for `buildVariant` creating `VariantType` inside `VariantType` alternatives — these should be flattened
2. **Check enum value sanitization**: Enum values like `linux`, `include`, `order` need `sanitize_enum_identifier()`
3. **Check parameter name sanitization**: Parameters named `token`, `result`, `error`, `metadata`, `include`, `order`, `event_types` get `param_` prefix
4. **Check path parameter types**: String path params use `std::regex_replace` directly, numeric use `std::to_string`
5. **Check `api::` namespace prefix**: Ref names must be prefixed with `api::` in both client and Python generators
6. **Check variant deduplication**: Duplicate variant signatures should become `using` aliases, not redefined `std::variant`

### 9. Debugging Compilation Errors

If generated code fails to compile:

1. **Check for undefined types**: Search for type names in `openapi_defs.hpp` that are referenced but not defined
2. **Check namespace issues**: All generated types are in `namespace api { ... }`, client code uses `api::TypeName`
3. **Check C++ keyword conflicts**: Search for keywords used as identifiers (especially `delete`, `class`, `template`, `operator`)
4. **Check variant ordering**: Variant alternatives must be defined before the variant that references them
5. **Check `std::format` usage**: Replaced with string concatenation for `std::optional`/`std::vector` compatibility

### 10. Testing with New OpenAPI Specs

To test with a new spec:

```bash
# 1. Copy spec to generator input
cp /path/to/spec.json tests/new_spec.json

# 2. Generate
./build/generator/siesta-generator --input tests/new_spec.json --output /tmp/new_spec/

# 3. Inspect output for correctness
cat /tmp/new_spec/openapi_defs.hpp | head -100
grep -c 'struct ' /tmp/new_spec/openapi_defs.hpp

# 4. If Python bindings needed, set up build directory
mkdir -p /tmp/new_spec/build && cd /tmp/new_spec/build
# Copy CMakeLists.txt from binance/build/ and adapt
```

## Common Pitfalls

1. **SchemaParser is header-only**: All implementation is inline in `schema_parser.hpp` — changes require recompile of all including files
2. **DependencyGraph is also header-only**: Same constraint
3. **PythonGenerator duplicates ClientGenerator logic**: Endpoint parsing code is duplicated — changes to parameter handling must be applied to both
4. **`isSyntheticCppType()` is the gate**: Types starting with `std::` or matching primitive names are excluded from dependency tracking and topological sort
5. **Variant signature deduplication is stateful**: `emitted_variant_signatures_` and `typedef_chain_` are member state in `DefsGenerator`
6. **`sanitize()` mutates in place**: `sanitize_enum_identifier()` calls `sanitize()` then adds dot handling
7. **`use_future` must be last argument**: The completion token is always the last parameter in generated client methods
8. **NB_MODULE name must match target name**: The nanobind module name (`siesta_bindings`) must match the CMake target for Python import to work
