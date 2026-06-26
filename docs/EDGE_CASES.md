# Edge Cases & Limitations

## AST Mapping

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

### Proto3-Specific

| Case | Mechanism |
|------|-----------|
| Nested messages | `Parent_Child` flat naming (same convention as OpenAPI inline structs) |
| `repeated T` | Anonymous `ArrayType` wrapper (`{Parent}_{Field}_array`) |
| `map<K, V>` | Anonymous `MapType` wrapper (`{Parent}_{Field}_map`) |
| `oneof` | `VariantType` (non-nullable) with alternatives as TypeRefs |
| `stream` keyword | Maps to `StreamingMode::{Server,Client,Bidirectional}` in `RPCMethod` |
| Proto package | Used as C++ namespace fallback |
| Dotted type references (`package.Type`) | Last component extracted as type name |
| Options (opaque) | Parsed but not interpreted — skipped |
| Reserved fields/numbers | Parsed but not interpreted — skipped |

---

## Known Limitations

### OpenAPI

1. **Cyclic dependencies**: Not supported — value semantics prohibit cycles. The generator
   detects them and aborts with a clear error listing all cycle paths.
2. **Polymorphic dispatch**: `oneOf` / `anyOf` generates `std::variant` but does not emit
   runtime discriminator-based dispatch. The schema `discriminator.propertyName` field is
   parsed but not acted upon.
3. **Schema validation**: Minimal OpenAPI spec validation. Invalid schemas may produce
   confusing errors rather than early rejection.
4. **Complex `$ref` chains**: Multi-hop `$ref` chains in parameters (e.g., `$ref` → `$ref` → inline)
   may not fully resolve.
5. **Request body content types**: Only the first content-type entry is used for generated
   request body code.
6. **Server URLs / authentication**: Not generated — the client class accepts host/port at
   construction but does not parse OpenAPI `servers` or `securitySchemes`.
7. **Response type generation**: All endpoints return
   `siesta::beast::ClientBase::outcome_type` (a `boost::system::result` of the HTTP
   response). Structured response types from the schema are not generated or validated.
8. **Query parameter arrays of non-string types**: Multi-valued query params for non-primitive
   arrays use `query_value()` which serializes each element as JSON — this may not match
   all server expectations.
9. **simdjson single-pass ranges**: simdjson's `dom::object` / `dom::array` iterators are
   single-pass — re-entering `begin()` on an already-consumed range triggers a debug
   assertion. The fix is pre-fetching all component data and endpoint data into C++
   containers before iterating paths.

### Proto3

1. **No protobuf wire format**: All serialization uses JSON via boost::json. Protobuf binary
   encoding is intentionally excluded — delegate to Google's libprotobuf if needed.
2. **No proto2 support**: Only proto3 syntax is handled. Proto2 constructs (groups,
   extensions, `required` with different semantics) are not parsed.
3. **MessageValue (text format literals)**: Option values using the proto text format
   (e.g., `option (custom) = { name: "foo" };`) are not parsed — only string/number/bool
   option values are handled.
4. **Import resolution**: The parser captures `import` statements but does not resolve
   or follow imported files. Single-file processing only.
5. **Float literal precision**: Float literals are parsed as Spirit X3's default double
   precision; exact proto3 float syntax (hex floats, `inf`, `nan`) is not fully supported.
6. **Negative enum values**: Enum values with a leading minus sign are parsed as
   `-x3::lit('-') >> int_lit` which discards the sign; the value is stored as absolute.

### RPC (all formats)

1. **No gRPC compatibility**: The RPC transport uses HTTP/1.1 JSON, not HTTP/2 protobuf.
   gRPC ecosystem tools (grpcurl, Envoy gRPC filters) do not interoperate.
2. **No multiplexed streams**: One connection carries one active stream. Multiplexing
   requires HTTP/2 or QUIC transport.
3. **Error model**: Structured RPC errors (status codes + messages + details) are captured
   in the IR but not yet emitted in generated code.
4. **Python bindings for RPC**: Not yet generated — Python bindings target REST endpoints
   only.

### Auto-detection

1. **JSON format disambiguation**: OpenRPC files require one failed OpenAPI parse attempt
   before detection succeeds. This adds one extra simdjson load (cached by the OS page cache).
2. **No `--format` override flag**: The generator auto-detects format by file extension
   and content. There is no manual override for ambiguous inputs.
