// SPDX-License-Identifier: Apache-2.0
#pragma once
/// simdjson-wrapped DOM model for OpenRPC 1.4.x.
/// Follows the same Object<T> / MapAdaptor<T> / SIESTA_*_FIELD pattern
/// as generator/src/Frontend/openapi3.hpp.
///
/// Key differences from OpenAPI 3:
///   - No securitySchemes in Components (JSON-RPC doesn't have them)
///   - ContentDescriptors replace Parameters (schema REQUIRED, name REQUIRED)
///   - Result is a single ContentDescriptor (not a responses map)
///   - Errors have integer code + message + unstructured data (not JSON Schema)
///   - Methods have paramStructure: "by-name" | "by-position" | "either"

#include "Frontend/openapi.hpp"
#include "Frontend/openapi3.hpp"  // JsonSchema, String, Number, etc.
#include <string_view>

namespace openrpc {

namespace __detail = openapi::__detail;
namespace common  = openapi::common;
namespace json_schema = openapi::json_schema;

using openapi::json_schema::JsonSchema;
using openapi::json_schema::String;
using openapi::json_schema::Number;
using openapi::json_schema::Integer;
using openapi::json_schema::Boolean;
using openapi::json_schema::Object;
using openapi::json_schema::Array;
using openapi::common::ExternalDocumentation;
using openapi::common::Info;
using openapi::common::Contact;
using openapi::common::License;
using openapi::common::Tag;
using openapi::common::Tags;

// ── Server ──────────────────────────────────────────────────────

class Server final : public __detail::Object<Server> {
public:
	using __detail::Object<Server>::Object;

	SIESTA_OPENAPI_VALUE_FIELD(name, std::string_view)
	SIESTA_OPENAPI_VALUE_FIELD(url, std::string_view)          // REQUIRED
	SIESTA_OPENAPI_VALUE_FIELD(description, std::string_view)
	SIESTA_OPENAPI_VALUE_FIELD(summary, std::string_view)
};

// ── Server Variable ─────────────────────────────────────────────

class ServerVariable final : public __detail::Object<ServerVariable> {
public:
	using __detail::Object<ServerVariable>::Object;

	SIESTA_OPENAPI_VALUE_FIELD(description, std::string_view)
	// enum: [string] — list of allowed values

	std::string_view default_value() const { return _GetValueIfExist<std::string_view>("default"); }
};
using ServerVariables = __detail::MapAdaptor<ServerVariable>;

// ── Link Object Server (distinct from Server Object) ────────────

class LinkServer final : public __detail::Object<LinkServer> {
public:
	using __detail::Object<LinkServer>::Object;

	SIESTA_OPENAPI_VALUE_FIELD(name, std::string_view)
	SIESTA_OPENAPI_VALUE_FIELD(url, std::string_view)          // REQUIRED
	SIESTA_OPENAPI_VALUE_FIELD(description, std::string_view)
	SIESTA_OPENAPI_VALUE_FIELD(summary, std::string_view)
	SIESTA_OPENAPI_OBJECT_FIELD(variables, ServerVariables)
};

// ── Content Descriptor (params AND result — schema REQUIRED) ────

class ContentDescriptor final : public __detail::Object<ContentDescriptor> {
public:
	using __detail::Object<ContentDescriptor>::Object;

	SIESTA_OPENAPI_VALUE_FIELD(name, std::string_view)         // REQUIRED
	SIESTA_OPENAPI_VALUE_FIELD(summary, std::string_view)
	SIESTA_OPENAPI_VALUE_FIELD(description, std::string_view)
	SIESTA_OPENAPI_VALUE_FIELD(required, bool)                 // default false
	SIESTA_OPENAPI_VALUE_FIELD(deprecated, bool)               // default false
	SIESTA_OPENAPI_OBJECT_FIELD(schema, JsonSchema)            // REQUIRED — can be bool in JSON Schema
};
using ContentDescriptors = __detail::MapAdaptor<ContentDescriptor>;

// ── Error (integer code, string message, unstructured data) ─────

class ErrorDef final : public __detail::Object<ErrorDef> {
public:
	using __detail::Object<ErrorDef>::Object;

	SIESTA_OPENAPI_VALUE_FIELD(code, int64_t)                  // REQUIRED — integer, not string
	SIESTA_OPENAPI_VALUE_FIELD(message, std::string_view)      // REQUIRED
	// data: any — unstructured, not a JsonSchema
};
using ErrorDefs = __detail::MapAdaptor<ErrorDef>;

// ── Link ────────────────────────────────────────────────────────

class Link final : public __detail::Object<Link> {
public:
	using __detail::Object<Link>::Object;

	SIESTA_OPENAPI_VALUE_FIELD(name, std::string_view)
	SIESTA_OPENAPI_VALUE_FIELD(summary, std::string_view)
	SIESTA_OPENAPI_VALUE_FIELD(description, std::string_view)
	SIESTA_OPENAPI_VALUE_FIELD(method, std::string_view)       // unique Method name
	SIESTA_OPENAPI_OBJECT_FIELD(server, LinkServer)
	// params: map of param name → runtime expression (unknown type)
};
using Links = __detail::MapAdaptor<Link>;

// ── Example Object ──────────────────────────────────────────────

class Example final : public __detail::Object<Example> {
public:
	using __detail::Object<Example>::Object;

	SIESTA_OPENAPI_VALUE_FIELD(name, std::string_view)         // REQUIRED
	SIESTA_OPENAPI_VALUE_FIELD(summary, std::string_view)
	SIESTA_OPENAPI_VALUE_FIELD(description, std::string_view)
	// value: any — REQUIRED (mutually exclusive with externalValue)
};
using Examples = __detail::MapAdaptor<Example>;

// ── Example Pairing ─────────────────────────────────────────────

class ExamplePairing final : public __detail::Object<ExamplePairing> {
public:
	using __detail::Object<ExamplePairing>::Object;

	SIESTA_OPENAPI_VALUE_FIELD(name, std::string_view)         // REQUIRED
	SIESTA_OPENAPI_VALUE_FIELD(description, std::string_view)
	// params: [Example | Reference] — REQUIRED
	// result: Example | Reference — optional (absent = notification example)
};
using ExamplePairings = __detail::MapAdaptor<ExamplePairing>;

// ── Method ──────────────────────────────────────────────────────
/// params is REQUIRED per spec (can be empty array).
/// result is optional — when absent, the method is a notification.

class Method final : public __detail::Object<Method> {
public:
	using __detail::Object<Method>::Object;

	SIESTA_OPENAPI_VALUE_FIELD(name, std::string_view)         // REQUIRED, unique within methods
	SIESTA_OPENAPI_VALUE_FIELD(summary, std::string_view)
	SIESTA_OPENAPI_VALUE_FIELD(description, std::string_view)
	SIESTA_OPENAPI_VALUE_FIELD(deprecated, bool)               // default false
	SIESTA_OPENAPI_VALUE_FIELD(paramStructure, std::string_view) // "by-name"|"by-position"|"either"

	using Params           = __detail::ListAdaptor<ContentDescriptor>;
	using Errors           = __detail::ListAdaptor<ErrorDef>;
	using ExampleList      = __detail::ListAdaptor<ExamplePairing>;
	using LinkList         = __detail::ListAdaptor<Link>;
	using ServerList       = __detail::ListAdaptor<Server>;

	Params params() const      { return _GetObjectIfExist<Params>("params"); }
	ContentDescriptor result() const { return _GetObjectIfExist<ContentDescriptor>("result"); }
	Errors errors() const      { return _GetObjectIfExist<Errors>("errors"); }
	LinkList links() const     { return _GetObjectIfExist<LinkList>("links"); }
	ExampleList examples() const { return _GetObjectIfExist<ExampleList>("examples"); }
	ExternalDocumentation externalDocs() const { return _GetObjectIfExist<ExternalDocumentation>("externalDocs"); }
	ServerList servers() const { return _GetObjectIfExist<ServerList>("servers"); }
	Tags tags() const          { return _GetObjectIfExist<Tags>("tags"); }
};
using Methods = __detail::ListAdaptor<Method>;

// ── Components ──────────────────────────────────────────────────
/// OpenRPC does NOT have securitySchemes.
/// Uses patterned Maps for reusable objects.

class Components final : public __detail::Object<Components> {
public:
	using __detail::Object<Components>::Object;

	SIESTA_OPENAPI_OBJECT_FIELD(schemas,            __detail::MapAdaptor<JsonSchema>)
	SIESTA_OPENAPI_OBJECT_FIELD(errors,             ErrorDefs)
	SIESTA_OPENAPI_OBJECT_FIELD(contentDescriptors, ContentDescriptors)
	SIESTA_OPENAPI_OBJECT_FIELD(links,              Links)
	SIESTA_OPENAPI_OBJECT_FIELD(examples,           Examples)
	SIESTA_OPENAPI_OBJECT_FIELD(examplePairings,    ExamplePairings)
	SIESTA_OPENAPI_OBJECT_FIELD(tags,               Tags)
};

// ── Top-level document ──────────────────────────────────────────

class OpenRPC : public __detail::Object<OpenRPC> {
protected:
	simdjson::dom::parser _parser;

public:
	OpenRPC() noexcept = default;
	OpenRPC(const OpenRPC&) = delete;
	OpenRPC(OpenRPC&&) noexcept = default;

	bool Load(const std::string& path) {
		try {
			_json = _parser.load(path).get_object();
			return true;
		} catch (...) {
			return false;
		}
	}

	using Servers = __detail::ListAdaptor<Server>;

	std::string_view openrpc() const { return _GetValueIfExist<std::string_view>("openrpc"); }
	Info info() const      { return _GetObjectIfExist<Info>("info"); }
	Servers servers() const { return _GetObjectIfExist<Servers>("servers"); }
	Methods methods() const { return _GetObjectIfExist<Methods>("methods"); }
	Components components() const { return _GetObjectIfExist<Components>("components"); }
	ExternalDocumentation externalDocs() const {
		return _GetObjectIfExist<ExternalDocumentation>("externalDocs");
	}
};

} // namespace openrpc
