# Generator Architecture

## Overview

The siesta generator is a multi-frontend C++23 transpiler that reads API schema definitions
and emits production-grade HTTP client/server code and Python extension modules.

Three input formats are supported, auto-detected by file extension and content:

```
.proto  ──▶ ProtoFrontend (Spirit X3)
.json   ──▶ OpenAPIFrontend (simdjson)  or  OpenRPCFrontend (simdjson)
```

All frontends converge on a shared intermediate representation, then pass through a common
back-end pipeline. The compiler is structured in three layers:

```
Frontend           Middle-end (IR)              Backend
─────────          ───────────────              ───────
parsing +          NormalizedAST                ICodeGenerator
format-specific    EndpointIR                   ├─ DefsGenerator
→ NormalizedAST    DependencyGraph              ├─ BeastClientGen
→ Endpoint IR      DefsGenerator                ├─ BeastServerGen
                   RPCEndpointBridge            ├─ BeastPythonGen
                                                └─ BeastServerPythonGen
```

Key libraries: simdjson (JSON parsing), Spirit X3 (proto parsing), boost::json (runtime
ser/des), boost::asio + beast (async I/O), nanobind (Python bindings).

---

## Directory Layout

```
generator/src/
├── Driver/                    CLI entry + phase orchestration
├── Frontend/
│   ├── IFrontend.hpp/.cpp     Virtual interface + auto-detect factory
│   ├── AST.hpp/.cpp           NormalizedAST (shared type IR)
│   ├── SchemaParser.hpp/.cpp  JSON schema → AST node parser
│   ├── openapi.hpp/.cpp       simdjson DOM wrappers, ListAdaptor, MapAdaptor
│   ├── openapi3.hpp/.cpp      OpenAPI v3 typed views
│   ├── OpenAPI/               OpenAPI frontend
│   │   └── OpenAPIFrontend.hpp/.cpp
│   ├── OpenRPC/               OpenRPC frontend
│   │   ├── openrpc.hpp        simdjson OpenRPC views
│   │   └── OpenRPCFrontend.hpp/.cpp
│   └── Proto/                 Proto3 frontend
│       ├── AST.hpp            Proto-specific AST types
│       ├── Parser.hpp/.cpp    Spirit X3 grammar
│       ├── Skipper.hpp        Comment-aware skipper
│       ├── ProtoBridge.hpp    ProtoFile → NormalizedAST + RPCMethod
│       └── ProtoFrontend.hpp/.cpp
├── IR/
│   ├── CodegenArgs.hpp        CodegenArgs struct + ICodeGenerator interface
│   ├── EndpointIR.hpp/.cpp    Endpoint struct, parseEndpoints(), detectAuth()
│   ├── RPCIR.hpp              RPCMethod, StreamingMode, RPCParam, RPCError
│   ├── RPCEndpointBridge.hpp  RPCMethod → Endpoint converter
│   ├── DependencyGraph.hpp/.cpp  Build + topological sort (Kahn) + cycle detection
│   └── DefsGenerator.hpp/.cpp Type definitions + JSON ser/des codegen
├── Backend/
│   └── Beast/                 boost::beast backends
│       ├── BeastClientGen.hpp/.cpp
│       ├── BeastServerGen.hpp/.cpp
│       ├── BeastPythonGen.hpp/.cpp
│       └── BeastServerPythonGen.hpp/.cpp
└── Support/
    ├── Utils.hpp/.cpp          sanitize, escapeCppString, primitiveToCpp, logging
    └── Filenames.hpp           Output path constants (constexpr string_view)
```

### Output files (all formats)

| File | Content |
|------|---------|
| `openapi_defs.hpp` | Type definitions, forward declarations, `tag_invoke` signatures |
| `openapi_defs.cpp` | `tag_invoke` bodies for boost::json ser/des |
| `client.hpp` | Async HTTP client class extending `siesta::beast::ClientBase` |
| `server.hpp` / `server.cpp` | Abstract server class with virtual methods + dispatch |
| `py_module.cpp` | Nanobind Python client module |
| `server_py.cpp` | Nanobind Python server trampoline |

---

## Layer 1: Frontend — Input Parsing

### IFrontend (virtual interface)

All frontends implement a common interface. The factory auto-detects the format:

```cpp
class IFrontend {
public:
    virtual ~IFrontend() = default;
    virtual bool parse(const std::filesystem::path& input) = 0;

    virtual const schema::NormalizedAST& ast() const = 0;
    virtual const std::vector<Endpoint>& endpoints() const = 0;
    virtual std::string module_name() const = 0;

    static std::unique_ptr<IFrontend> create(
        const std::filesystem::path& input,
        std::string_view format_hint = "");
};
```

**Auto-detection logic:**

```
file extension
  ├── .proto ──────────────────────▶ ProtoFrontend
  └── .json ──▶ try OpenAPIFrontend::parse(input)
                    ├── succeeds ▶ return it
                    └── fails ──▶ try OpenRPCFrontend::parse(input)
                                      ├── succeeds ▶ return it
                                      └── fails    ▶ error
```

### OpenAPIFrontend

Wraps the existing OpenAPI v3 parsing pipeline:

1. `openapi::OpenAPI::Load()` reads JSON into simdjson's on-demand DOM
2. `static_cast<const openapi::v3::OpenAPIv3&>` casts to a typed view
3. Iterates `components/schemas`, calls `SchemaParser::parseSchema()` for each entry
4. Calls `parseEndpoints(spec)` to produce `vector<Endpoint>`
5. Returns: `NormalizedAST` + `vector<Endpoint>`

### SchemaParser

Converts raw `JsonSchema` objects (simdjson DOM views) into `NormalizedAST` nodes:

| Input Pattern | AST Output |
|---------------|------------|
| `object` with `properties` / `allOf` | `StructType` |
| `object` with `oneOf` / `anyOf` | `VariantType` |
| `unknown` type with `properties` / `allOf` | `StructType` (implicit object) |
| `unknown` type with `oneOf` / `anyOf` | `VariantType` |
| `allOf` with `$ref` | `StructType::allOf_bases` (multiple inheritance) |
| `allOf` with inline schema | Extracted as `{name}_base_{N}` standalone struct |
| `array` items | `ArrayType` |
| `additionalProperties` on object | `MapType` |
| `string` / `integer` with `enum` | `PrimitiveType` with `enum_values` |
| `$ref` | `TypeRef{name, is_inline=false}` |

### OpenRPCFrontend

Wraps the OpenRPC JSON parsing pipeline:

1. `openrpc::OpenRPC::Load()` reads JSON into simdjson DOM
2. `materialiseMethods()` extracts methods into `vector<RPCMethod>` (flat C++ structs, no simdjson refs)
3. Iterates `components/schemas`, calls `SchemaParser::parseSchema()` for each entry
4. Calls `rpcToEndpoints()` to convert `RPCMethod` → `Endpoint`
5. Returns: `NormalizedAST` + `vector<Endpoint>`

### ProtoFrontend

Wraps the proto3 parsing pipeline:

1. Reads file into string, calls `parse_proto(source)` (Spirit X3 grammar)
2. `ProtoBridge::convertFile()` converts `ProtoFile` → `NormalizedAST` (messages → structs, enums → enums, oneofs → variants, maps → MapTypes)
3. Calls `rpcToEndpoints()` to convert service RPCs → `Endpoint`
4. Returns: `NormalizedAST` + `vector<Endpoint>`

The Spirit X3 grammar (~330 lines) covers proto3 syntax including: messages, enums, oneofs,
map fields, reserved, options (opaque), services with streaming RPCs, nested messages,
comments (// and /* */), imports, and package declarations.

---

## Layer 2: Middle-end — Shared IR + Analysis

### NormalizedAST (`Frontend/AST.hpp`)

The format-agnostic type system. All frontends populate it, all backends consume it:

| Type | Represents | Examples |
|------|-----------|----------|
| `StructType` | Record/object with named fields | `message`, `object` schema |
| `VariantType` | Discriminated union | `oneof`, `oneOf`/`anyOf` |
| `ArrayType` | Homogeneous list | `repeated`, `array` |
| `MapType` | String-keyed dictionary | `map<K,V>`, `additionalProperties` |
| `EnumType` | Named integer constants | `enum` |
| `PrimitiveType` | Scalar with optional format and enum values | `string`, `int32`, etc. |

`TypeRef` links types to each other: `{name, is_inline}`. Inline types carry the C++ type directly (e.g. `"std::string"`); named types reference other AST entries.

### EndpointIR (`IR/EndpointIR.hpp`)

The format-agnostic HTTP operation descriptor. One `Endpoint` per operation/RPC method:

```cpp
struct Endpoint {
    std::string method;           // HTTP verb string
    std::string path;             // URL path
    std::string path_template;    // path with {} placeholders
    std::string function_name;    // C++ method name
    std::string cpp_verb;         // beast verb constant name
    std::vector<ClientParam> params;  // path/query/header params
    bool has_request_body;
    std::string body_type;        // C++ type of request body
    std::string body_content_type;
    bool is_websocket;            // true for streaming RPCs
    AuthType auth_type;
};
```

REST (OpenAPI) endpoints populate path/query/header params. RPC (proto/OpenRPC) endpoints
set params empty — everything is body-only POST.

### RPCIR (`IR/RPCIR.hpp`)

RPC-specific metadata before conversion to `Endpoint`. Frontends produce this as an
intermediate step, then `RPCEndpointBridge` converts to `Endpoint`:

```cpp
struct RPCMethod {
    std::string name;             // "Service/Method"
    std::vector<RPCParam> params;
    TypeRef result_type;
    bool is_notification;         // no response expected
    StreamingMode streaming;      // None, Server, Client, Bidirectional
};
```

### RPCEndpointBridge (`IR/RPCEndpointBridge.hpp`)

Converts `RPCMethod` → `Endpoint`. A single `~30-line` function:

```
RPCMethod                      Endpoint
─────────                      ────────
name: "Service/Method"   →     path: "/rpc/Service/Method"
                               function_name: "Service_Method"
                               method: "post", cpp_verb: "post"
                               params: {}  (empty)
                               has_request_body: true
                               body_type: first param's cpp_type
                               is_websocket: streaming != None
```

### DependencyGraph (`IR/DependencyGraph.hpp`)

Builds a directed graph over `NormalizedAST` types:
- `StructType` → edges from field types (`DepKind::Value`) and allOf bases (`DepKind::Base`)
- `VariantType` → edges from alternatives (`DepKind::Variant`)
- `ArrayType` → edge from element type
- `MapType` → edge from value type
- Synthetic types (`std::string`, `int64_t`, etc.) excluded via `isSyntheticCppType()`

Cycle detection via DFS with recursion-stack tracking. Topological sort via Kahn's
algorithm with deterministic ordering (`std::queue`). Cyclic schemas are a hard error —
value-semantic types cannot express cycles.

### DefsGenerator (`IR/DefsGenerator.hpp`)

Implements `ICodeGenerator`. Emits type definitions and boost::json `tag_invoke` ser/des
bodies in topological order. Key behaviors:

- `allOf` → C++ inheritance with merged base-class serialization
- `oneOf`/`anyOf` → `using Name = std::variant<A, B, C>;`
- Variant deduplication: identical signatures become `using` aliases
- Single-alternative non-nullable variants collapse to typedefs
- `additionalProperties` → `using Name = std::map<std::string, T>;`
- Top-level arrays → `using Name = std::vector<T>;`
- Enum primitives → `enum class Name : int { ... };`
- Struct serialization: construct `object(sp)` with propagated `storage_ptr`, merge
  allOf bases then emit fields
- Variant deserialization: try each alternative in order via `try/catch`

---

## Layer 3: Backend — Code Generation

### ICodeGenerator (`IR/CodegenArgs.hpp`)

All backends share a single abstract interface:

```cpp
struct CodegenArgs {
    const schema::NormalizedAST& ast;
    const analysis::TopologicalOrder& order;
    std::string module_name;
    std::string ns;
    const std::vector<Endpoint>* endpoints;
};

class ICodeGenerator {
public:
    virtual ~ICodeGenerator() = default;
    virtual void operator()(const CodegenArgs& args,
                            const std::filesystem::path& output_dir) = 0;
};
```

Backends consume only `NormalizedAST` (type shapes) and `vector<Endpoint>` (operations).
They have no knowledge of the input format.

### BeastClientGenerator → `client.hpp`

Emits `class Client : public ::siesta::beast::ClientBase` with one templated
completion-token method per endpoint. Method body emission decomposes into:
`emitPathParams` (find+replace of `{}` placeholders), `emitQueryParams` (query string
building), `emitRequestBody` (JSON serialization with pool-backed `storage_ptr`),
`emitHeaderParams`. RPC endpoints hit the degenerate path — no path params, no query
params, body-only.

### BeastServerGenerator → `server.hpp` + `server.cpp`

Emits abstract `Server` class with one pure-virtual method per endpoint. The dispatch
table uses:
- **Static paths**: O(1) hash lookup (`std::unordered_map<pair<path, verb>, fnptr>`)
- **Parameterised paths**: segment-by-segment `match_path()` algorithm over a linear array
- **WebSocket**: `upgrade_to_websocket()` dispatch for streaming endpoints

RPC endpoints are static-path entries with `POST /rpc/Service/Method`.

### BeastPythonGenerator / BeastServerPythonGenerator

Nanobind modules: `ClientWrapper` for synchronous client usage (via `boost::asio::use_future`) and `PyServer` trampoline for Python-side server subclassing.

---

## Runtime Library (`include/siesta/`)

Generated code links against `siesta::beast`:

| Component | Role |
|-----------|------|
| `ClientBase` | Async HTTP/1.1 client: strand-serialized I/O, 3-state FSM (send→recv→done), connection pooling, WebSocket upgrade, per-request JSON pool |
| `ServerBase` + `Session` | Async TCP acceptor, per-connection request/response pipeline, configurable timeouts, WebSocket upgrade, per-request JSON pool |
| `encoding.hpp` | `url_encode()` + `query_value()` overloads — included by generated headers |
| `python_util.hpp` | `json_to_python()` + `extract_response_json()` — shared by generated Python modules |

### Runtime vs Generated — split convention

All new features must decide where their logic lives. The rule:

**Runtime library** (`include/siesta/`, `src/`): Any code that would be identical
regardless of the input schema — connection lifecycle, retry/hedging loops,
deadline timers, WebSocket frame I/O primitives, CORS, compression, error
envelope helpers, interceptor chains, health-check responder, `is_transient()`.

**Generated code** (`Backend/Beast/` emitter output): Schema-specific code —
type definitions, route-to-handler dispatch tables, method signatures, parameter
extraction, auth header names, per-endpoint retry/deadline config, per-endpoint
streaming mode.

**Rule of thumb**: If you can type the code without looking at a `.proto` or
`.json` file, it belongs in the runtime library.

---

## Data Flow

```
Driver/main.cpp
  │
  └─ IFrontend::create(input)
       │
       ├─ .proto  → ProtoFrontend::parse()
       │               ├─ Spirit X3 → ProtoFile
       │               ├─ ProtoBridge → NormalizedAST + RPCMethod
       │               └─ rpcToEndpoints → vector<Endpoint>
       │
       ├─ .json → OpenAPIFrontend::parse()
       │            ├─ simdjson → OpenAPI v3 DOM
       │            ├─ SchemaParser → NormalizedAST
       │            └─ parseEndpoints → vector<Endpoint>
       │
       └─ .json → OpenRPCFrontend::parse()
                    ├─ simdjson → OpenRPC DOM
                    ├─ materialiseMethods → vector<RPCMethod>
                    ├─ SchemaParser → NormalizedAST
                    └─ rpcToEndpoints → vector<Endpoint>
  │
  ├─ analysis::DependencyGraph::buildFromAST(ast)
  ├─ analysis::sortTypes(ast) → TopologicalOrder
  │
  └─ for each ICodeGenerator:
       CodegenArgs{ast, order, module_name, ns, &endpoints}
       generator(args, output_dir)
```

---

## Design Decisions

### 1. Frontend polymorphism via IFrontend
Frontends implement a virtual interface. The Driver knows only `IFrontend` — format-specific
parsing is isolated in per-format subdirectories (`OpenAPI/`, `OpenRPC/`, `Proto/`). Adding
a new format means adding a new directory with a single class implementing `IFrontend`.

### 2. Endpoint as the universal operation IR
Both REST (path+verb+params) and RPC (body-only POST) map to `Endpoint`. RPC methods set
params empty and use a flat path. The same backends handle both identically — RPC is just
a degenerate REST endpoint.

### 3. Two-tier RPC IR (RPCMethod → Endpoint bridge)
RPC-specific metadata (streaming mode, notification flag, structured errors) lives in
`RPCMethod`, which frontends produce. A ~30-line bridge converts to `Endpoint`, which
backends consume. This keeps RPC concepts out of the transport layer.

### 4. allOf → C++ inheritance
`allOf` with `$ref` bases becomes C++ multiple inheritance with merged serialization.
Inline `allOf` components are extracted as standalone `{name}_base_{N}` structs.

### 5. Nested types → flat naming
Inline structs within parent types use `Parent_Child` naming (not `Parent::Child`) to
avoid forward-declaration ordering fragility in C++.

### 6. Variant deduplication and collapse
Duplicate variant signatures become `using` aliases tracked in a typedef chain.
Single-alternative non-nullable variants collapse to typedefs. Signatures are canonicalised
by resolving typedef chains before comparison.

### 7. Synthetic type filtering
Types matching C++ primitives (`int`, `double`, `std::string`, etc.) are excluded from
the dependency graph via `isSyntheticCppType()`, preventing ballooning with synthetic edges.

### 8. Output filename constants
Output path constants are centralized in `Support/Filenames.hpp` as `constexpr string_view`
rather than hardcoded across generator files.

### 9. Per-concern utility organization
`sanitize()` handles C++ keyword conflicts, GCC predefined macros, and special-character
replacement in a single O(1) lookup function. `query_value()` overloads for URL encoding
live in the runtime header `encoding.hpp`, shared by all generated clients.
