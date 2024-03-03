// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "openapi.hpp"

namespace openapi::v3 {

using common::ExternalDocumentation;
using common::Info;
using common::Tag;
using common::Tags;
using json_schema::Array;
using json_schema::Boolean;
using json_schema::Integer;
using json_schema::JsonSchema;
using json_schema::Number;
using json_schema::Object;
using json_schema::String;

class Example final : public __detail::Object<Example> {
public:
	using __detail::Object<Example>::Object;

	SIESTA_OPENAPI_VALUE_FIELD(summary, std::string_view)
	SIESTA_OPENAPI_VALUE_FIELD(description, std::string_view)
	SIESTA_OPENAPI_VALUE_FIELD(value, std::string_view)
	SIESTA_OPENAPI_VALUE_FIELD(externalValue, std::string_view)
};
using Examples = __detail::MapAdaptor<Example>;

class Header final : public __detail::Object<Header> {
public:
	using __detail::Object<Header>::Object;

	std::string_view description() const;
	bool required() const;
	bool deprecated() const;
	bool allowEmptyValue() const;
};
using Headers = __detail::MapAdaptor<Header>;

class MediaType final : public __detail::Object<MediaType> {
public:
	using __detail::Object<MediaType>::Object;

	class Encoding final : public __detail::Object<Encoding> {
	public:
		using __detail::Object<Encoding>::Object;

		SIESTA_OPENAPI_VALUE_FIELD(contentType, std::string_view)
		SIESTA_OPENAPI_OBJECT_FIELD(headers, Headers)
		SIESTA_OPENAPI_VALUE_FIELD(style, std::string_view)
		SIESTA_OPENAPI_VALUE_FIELD(explode, bool)
		SIESTA_OPENAPI_VALUE_FIELD(allowReserved, bool)
	};
	using Encodings = __detail::ListAdaptor<Encoding>;

	SIESTA_OPENAPI_OBJECT_FIELD(schema, JsonSchema)
	SIESTA_OPENAPI_OBJECT_FIELD(examples, Examples)
	SIESTA_OPENAPI_OBJECT_FIELD(encoding, Encodings)
};
using MediaTypes = __detail::MapAdaptor<MediaType>;

class Parameter final : public common::Ref<common::Parameter> {
public:
	using Base = common::Ref<common::Parameter>;
	using Base::Base;

	enum class Location : uint8_t {
		path,
		query,
		header,
		cookie,
		unknown,
	};
	enum class Style : uint8_t {
		matrix,
		label,
		form,
		simple,
		spaceDelimited,
		pipeDelimited,
		deepObject,
		unknown,
	};

	// Returns strongly typed enum representing the location of the path.
	Location In() const noexcept;
	// Returns strongly typed enum representing the style of the parameter value.
	Style Style_() const noexcept;

	bool deprecated() const;
	bool allowEmptyValue() const;

	SIESTA_OPENAPI_VALUE_FIELD(style, std::string_view)
	SIESTA_OPENAPI_VALUE_FIELD(explode, bool)
	SIESTA_OPENAPI_VALUE_FIELD(allowReserved, bool)
	SIESTA_OPENAPI_OBJECT_FIELD(schema, JsonSchema)
	SIESTA_OPENAPI_OBJECT_FIELD(examples, Examples)
	SIESTA_OPENAPI_OBJECT_FIELD(content, MediaTypes)
};

class SecurityScheme final : public common::SecuritySchema {
public:
	using common::SecuritySchema::SecuritySchema;

	SIESTA_OPENAPI_VALUE_FIELD(scheme, std::string_view)
	SIESTA_OPENAPI_VALUE_FIELD(bearerFormat, std::string_view)
	SIESTA_OPENAPI_VALUE_FIELD(openIdConnectUrl, std::string_view)
};

class RequestBody final : public __detail::Object<RequestBody> {
public:
	using __detail::Object<RequestBody>::Object;
	using Content = __detail::MapAdaptor<MediaType>;

	std::string_view description() const;
	Content content() const;
	bool required() const;
};

class Server final : public __detail::Object<Server> {
public:
	using __detail::Object<Server>::Object;

	std::string_view description() const;
	std::string_view url() const;
};

class Link final : public __detail::Object<Link> {
public:
	using __detail::Object<Link>::Object;

	SIESTA_OPENAPI_VALUE_FIELD(operationRef, std::string_view)
	SIESTA_OPENAPI_VALUE_FIELD(operationId, std::string_view)
	SIESTA_OPENAPI_VALUE_FIELD(description, std::string_view)
	SIESTA_OPENAPI_OBJECT_FIELD(server, Server)
};

class Response : public __detail::Object<Response> {
public:
	using __detail::Object<Response>::Object;
	using Content = __detail::MapAdaptor<MediaType>;
	using Links = __detail::MapAdaptor<Link>;

	std::string_view description() const;
	Headers headers() const;
	Content content() const;
	Links links() const;
};

class Operation final : public common::Operation {
public:
	using Parameters = __detail::ListAdaptor<Parameter>;
	using Responses = __detail::MapAdaptor<Response>;
	using Servers = __detail::ListAdaptor<Server>;
	using common::Operation::Operation;

	Parameters parameters() const;
	RequestBody requestBody() const;
	Responses responses() const;
	Servers servers() const;
};

class Path final : public common::Path<Operation> {
public:
	using Base = typename common::Path<Operation>;
	using Servers = __detail::ListAdaptor<Server>;
	using Parameters = __detail::ListAdaptor<Parameter>;
	using Base::Base;

	SIESTA_OPENAPI_VALUE_FIELD(summary, std::string_view)
	SIESTA_OPENAPI_VALUE_FIELD(description, std::string_view)
	SIESTA_OPENAPI_OBJECT_FIELD(servers, Servers)
	SIESTA_OPENAPI_OBJECT_FIELD(parameters, Parameters)
};
using Paths = __detail::MapAdaptor<Path>;

class Components final : public __detail::Object<Components> {
public:
	using __detail::Object<Components>::Object;

	using Schemas = __detail::MapAdaptor<JsonSchema>;
	using Responses = __detail::MapAdaptor<Response>;
	using Parameters = __detail::MapAdaptor<Parameter>;
	using RequestBodies = __detail::MapAdaptor<RequestBody>;
	using SecuritySchemes = __detail::MapAdaptor<SecurityScheme>;
	using Links = __detail::MapAdaptor<Link>;

	SIESTA_OPENAPI_OBJECT_FIELD(schemas, Schemas)
	SIESTA_OPENAPI_OBJECT_FIELD(responses, Responses)
	SIESTA_OPENAPI_OBJECT_FIELD(parameters, Parameters)
	SIESTA_OPENAPI_OBJECT_FIELD(examples, Examples)
	SIESTA_OPENAPI_OBJECT_FIELD(requestBodies, RequestBodies)
	SIESTA_OPENAPI_OBJECT_FIELD(headers, Headers)
	SIESTA_OPENAPI_OBJECT_FIELD(securitySchemes, SecuritySchemes)
	SIESTA_OPENAPI_OBJECT_FIELD(links, Links)
	SIESTA_OPENAPI_OBJECT_FIELD(callbacks, Paths)
};

class OpenAPIv3 final : public OpenAPI {
public:
	static constexpr uint8_t version = 3;

	OpenAPIv3(const OpenAPIv3&) = delete;
	OpenAPIv3(OpenAPIv3&&) noexcept = default;

	using Servers = __detail::ListAdaptor<Server>;
	using Paths = __detail::MapAdaptor<Path>;

	std::string_view openapi() const;
	Info info() const;
	Servers servers() const;
	Paths paths() const;
	// webhooks
	Components components() const;
	// security
	Tags tags() const;
	ExternalDocumentation externalDocs() const;
};

void PrintStructDefinitions(
	const OpenAPIv3& file,
	const std::filesystem::path& input,
	const std::filesystem::path& output);

} // namespace openapi::v3