# OpenAPI → C++ Type Mapping

How siesta maps OpenAPI v3 schema types to C++ types. All generated types
live in a single namespace derived from the spec's `info.title` (sanitized)
or the `--namespace` CLI flag.

All types get `boost::json::tag_invoke` overloads for round-trip JSON
serialization. Serialization includes all fields unconditionally.
Deserialization is lenient — missing fields are silently skipped and
left default-constructed.

## Primitives

| OpenAPI type | format | C++ type |
|-------------|--------|----------|
| `string` | *(none)* | `std::string` |
| `string` | `date-time`, `date`, `byte`, `base64`, `binary` | `std::string` |
| `integer` | *(none)* | `int32_t` |
| `integer` | `int32` | `int32_t` |
| `integer` | `int64` | `int64_t` |
| `integer` | `uint32` | `uint32_t` |
| `integer` | `uint64` | `uint64_t` |
| `number` | *(none)* | `float` |
| `number` | `float` | `float` |
| `number` | `double` | `double` |
| `boolean` | — | `bool` |

No special date/time or binary types are generated. All string formats
produce `std::string`.

## Objects → structs

An OpenAPI `object` with `properties` becomes a C++ `struct` with one
member per property.

```json
"Error": {
  "properties": {
    "code": { "type": "integer", "format": "int32" },
    "message": { "type": "string" },
    "fields": { "type": "string" }
  }
}
```

```cpp
struct Error {
    int32_t code;
    std::string message;
    std::string fields;
};
```

**No `std::optional` for optional fields.** All fields use value semantics
regardless of whether they appear in the schema's `required` array.
During deserialization, missing fields are silently skipped and left
default-constructed (zero for integers, empty for strings, etc.).

Field names that collide with C++ reserved words get a `_` suffix
(`class` → `class_`, `delete` → `delete_`). Names matching GCC/Clang
predefined macros (`unix`, `linux`) also get a `_` suffix.

## Nested inline objects → flat naming

An inline object property (not a `$ref`) is extracted as a standalone
struct with `Parent_Child` naming. Nested types do NOT use `Parent::Child`
— all types are top-level in the namespace.

```json
"Order": {
  "type": "object",
  "properties": {
    "metadata": {
      "type": "object",
      "properties": {
        "created_at": { "type": "string" }
      }
    }
  }
}
```

```cpp
struct Order_metadata {
    std::string created_at;
};

struct Order {
    Order_metadata metadata;
};
```

## Enums

A string or integer type with an `enum` array becomes `enum class`.

```json
"ItemStatus": {
  "type": "string",
  "enum": ["active", "inactive", "archived"]
}
```

```cpp
enum class ItemStatus : int {
    active,
    inactive,
    archived
};
```

Enum identifiers are sanitized: dots become `_`, leading digits get a `_`
prefix, C++ reserved words get a `_` suffix.

JSON serialization maps between the string value and the enum constant.
A `query_value()` overload is also generated for use in URL query strings:

```cpp
inline std::string query_value(ItemStatus val) {
    switch (val) {
        case ItemStatus::active: return "active";
        case ItemStatus::inactive: return "inactive";
        case ItemStatus::archived: return "archived";
        default: return "";
    }
}
```

## Arrays

An `array` type becomes `std::vector<ElementType>`.

As a struct field:

```json
"tags": { "type": "array", "items": { "type": "string" } }
```

```cpp
std::vector<std::string> tags;
```

As a top-level schema:

```json
"ItemList": {
  "type": "array",
  "items": { "$ref": "#/components/schemas/Item" }
}
```

```cpp
using ItemList = std::vector<Item>;
```

## Maps (`additionalProperties`)

An object with `additionalProperties` becomes `std::map<std::string, ValueType>`.

```json
"Metadata": {
  "type": "object",
  "additionalProperties": { "type": "string" }
}
```

```cpp
using Metadata = std::map<std::string, std::string>;
```

## `allOf` → C++ inheritance

`allOf` with `$ref` entries maps to C++ base class inheritance. Inline
`allOf` members are extracted as standalone `{Name}_base_{N}` structs.

```json
"DetailedItem": {
  "allOf": [
    { "$ref": "#/components/schemas/Item" },
    {
      "type": "object",
      "properties": {
        "detail": { "type": "string" },
        "rating": { "type": "number", "format": "double" }
      }
    }
  ]
}
```

```cpp
struct DetailedItem_base_1 {
    std::string detail;
    double rating;
};

struct DetailedItem : Item, DetailedItem_base_1 {
};
```

Multiple `$ref` entries produce multiple inheritance. Serialization merges
all base class JSON objects into the derived object. Deserialization
populates each base slice from the same JSON object independently.

## `oneOf` / `anyOf` → `std::variant`

`oneOf` or `anyOf` with `$ref` alternatives becomes a `std::variant` alias.

```json
"Outcome": {
  "oneOf": [
    { "$ref": "#/components/schemas/Error" },
    { "$ref": "#/components/schemas/EchoResponse" }
  ]
}
```

```cpp
using Outcome = std::variant<Error, EchoResponse>;
```

Serialization uses `std::visit` to serialize the active alternative.
Deserialization tries each alternative in declaration order via try/catch.

**Limitation: no discriminator-based dispatch.** The `discriminator`
field in the OpenAPI spec is parsed but not acted upon. Deserialization
always tries alternatives in order and returns the first one that doesn't
throw. Since struct deserializers are lenient (they don't throw for missing
fields), the first struct alternative will always match when multiple
struct alternatives are present.

### Inline alternatives

Inline object alternatives are extracted as `{Name}_alt_{N}` structs:

```json
"Result": {
  "oneOf": [
    { "type": "object", "properties": { "value": { "type": "string" } } },
    { "$ref": "#/components/schemas/Error" }
  ]
}
```

```cpp
struct Result_alt_0 {
    std::string value;
};

using Result = std::variant<Result_alt_0, Error>;
```

### Nested variants

If a variant alternative is itself a variant, the inner alternatives
are flattened into the outer variant.

### Nullable variants

A nullable `oneOf`/`anyOf` adds `std::nullptr_t` as the final alternative:

```cpp
using MaybeItem = std::variant<Item, std::nullptr_t>;
```

Nullable variants are NOT collapsed to typedefs even if they have a single
alternative.

### Single-alternative collapse

A non-nullable variant with exactly one alternative collapses to a typedef:

```json
"Wrapper": {
  "oneOf": [
    { "$ref": "#/components/schemas/Item" }
  ]
}
```

```cpp
using Wrapper = Item;
```

### Duplicate variant signatures

If two variant schemas produce identical `std::variant<...>` signatures,
the second becomes a `using` alias to the first:

```cpp
using TypeA = std::variant<int64_t, std::string>;
using TypeB = TypeA;  // same signature, aliased
```

### Empty variants

A variant with no alternatives and no nullable marker becomes
`std::monostate`:

```cpp
using Empty = std::monostate;
```

## `$ref` resolution

A `$ref` resolves to the referenced schema's type name (last path
component, sanitized):

```json
{ "$ref": "#/components/schemas/EchoResponse" }
```

Becomes the C++ type `EchoResponse` — used directly in struct fields,
variant alternatives, and function signatures.

## Namespace

All generated types, client, server, and Python bindings live in a single
C++ namespace. The namespace name is:

1. The `--namespace` CLI flag if provided
2. Otherwise, the sanitized `info.title` from the spec

```json
{ "info": { "title": "Echo API" } }
```

```cpp
namespace Echo_API {
    // all types, client, server here
}
```

## JSON serialization

Every generated struct, variant, and enum gets a pair of `boost::json::tag_invoke`
overloads for `value_from` (C++ → JSON) and `value_to` (JSON → C++).

```cpp
void tag_invoke(boost::json::value_from_tag, boost::json::value& jv, const Item& v);
Item tag_invoke(boost::json::value_to_tag<Item>, const boost::json::value& jv);
```

Usage:

```cpp
// Serialize
Item item{1, "widget", "desc", {"a", "b"}, ItemStatus::active};
boost::json::value jv = boost::json::value_from(item);

// Deserialize
auto item2 = boost::json::value_to<Item>(jv);
```

### Serialization behavior

- **Structs**: all fields serialized unconditionally (no omission of defaults)
- **Inherited structs**: base class objects merged into derived JSON object
- **Enums**: serialized as JSON strings matching the OpenAPI enum values
- **Variants**: `std::visit` dispatches to the active alternative's serializer

### Deserialization behavior

- **Structs**: each field checked with `if_contains()` — missing fields are
  silently skipped, leaving the member default-constructed
- **Enums**: matched against known string values; unrecognized values return
  the first enum constant as default
- **Variants**: alternatives tried in declaration order; first successful
  `value_to<T>()` wins. No exception on success; throws
  `std::runtime_error` only if ALL alternatives fail

## Limitations

1. **No `std::optional` for optional fields.** All struct fields are
   value-semantic. Missing JSON fields are default-constructed, not absent.

2. **No discriminator dispatch.** `oneOf`/`anyOf` deserialization tries
   alternatives in order. Struct alternatives with lenient deserialization
   (no required-field checking) will always match first.

3. **No special date/time types.** All string formats map to `std::string`.

4. **Cyclic dependencies not supported.** Value semantics cannot represent
   cycles. The generator detects cycles and aborts with an error.

5. **Enum default on unknown values.** Deserializing an unrecognized enum
   string returns the first enum constant, not an error.
