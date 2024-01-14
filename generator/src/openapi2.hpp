#pragma once

#include "openapi.hpp"

namespace openapi::v2 {

using common::ExternalDocumentation;
using common::Info;
using common::Tags;

// A limited subset of JSON-Schema's items object. It is used by parameter definitions that are not located in "body".
// TODO: Convert this to the more complete implementation of a JSON schema in openapi.hpp.
class Item : public __detail::Object<Item> {
public:
	using __detail::Object<Item>::Object;
	using Items = __detail::MapAdaptor<Item>;

	std::string_view type() const;
	std::string_view format() const;
	// Required if type is "array". Describes the type of items in the array.
	// Notes: Is actually singular.
	Item items() const;
	std::string_view collectionFormat() const;
	double maximum() const;
	bool exclusiveMaximum() const;
	double minimum() const;
	bool exclusiveMinimum() const;
	int64_t maxLength() const;
	int64_t minLength() const;
	std::string_view pattern() const;
	int64_t maxItems() const;
	int64_t minItems() const;
	bool uniqueItems() const;
	double multipleOf() const;

	bool IsReference() const noexcept;
	// If the key of this item is $ref, then retrieve the name of the referenced object type.
	std::string_view reference() const;
};

// Protected inheritance because not everything in the base class exists in this subclass.
// Make public the fields and functions on a case-by-case basis.
class Schema2 : public json_schema::JsonSchema {
public:
	using Base = typename json_schema::JsonSchema;
	using Base::Base;
	using Base::Type;
};

class String final : public Schema2 {
public:
	using Schema2::Schema2;

	uint64_t minLength() const;
	uint64_t maxLength() const;
	std::string_view pattern() const;
};

class Number final : public Schema2 {
public:
	using Schema2::Schema2;

	uint64_t maximum() const;
	bool exclusiveMaximum() const;
	uint64_t minimum() const;
	bool exclusiveMinimum() const;
	int64_t multipleOf() const;
};

class Object final : public Schema2 {
public:
	using Schema2::Schema2;
	using Enum = __detail::ListAdaptor<std::string_view>;

	std::string_view title() const;
	std::string_view description() const;
	int64_t maxProperties() const;
	int64_t minProperties() const;
	bool required() const;
	Enum enum_() const;
	Schema2 properties() const;
};

class Array final : public Schema2 {
public:
	using Schema2::Schema2;

	std::string_view collectionFormat() const;
	Schema2 items() const;
	int64_t minItems() const;
	int64_t maxItems() const;
	bool uniqueItems() const;
};

class Schema : public Item {
public:
	using Item::Item;
	using Enum = __detail::ListAdaptor<std::string_view>;
	using NestedSchema = __detail::MapAdaptor<Schema>;
	Schema(Item& item)
		: Item(item) {}

	std::string_view title() const;
	std::string_view description() const;
	// ... other JSON schema properties inherited from Item ...
	int64_t maxProperties() const;
	int64_t minProperties() const;
	bool required() const;
	Enum enum_() const;
	NestedSchema properties() const;
};

class Parameter : public Item {
public:
	using Item::Item;
	Parameter(Item& item)
		: Item(item) {}

	enum class Location {
		path,
		query,
		header,
		body,
		form,
		unknown,
	};

	// Returns strongly typed enum representing the location of the path.
	Location In() const noexcept;

	std::string_view name() const;
	std::string_view in() const;
	std::string_view description() const;
	bool required() const;

	// If 'in' is 'body'
	Schema schema() const;

	// TODO add visitor
	template <typename VisitorType>
	static constexpr Location Visit(const Parameter&, VisitorType&&);
};

class BodyParameter final : public Parameter {
public:
	using Parameter::Parameter;

	Schema schema() const;
};

class Header final : public Schema2 {
public:
	using Schema2::Schema2;

	std::string_view description() const;
};

class Response : public __detail::Object<Response> {
public:
	using __detail::Object<Response>::Object;
	using Headers = __detail::MapAdaptor<Schema2>;

	std::string_view description() const;
	Schema schema() const;
	Headers headers() const;
};

class Operation final : public common::Operation {
public:
	using common::Operation::Operation;
	using Consumes = __detail::ListAdaptor<std::string_view>;
	using Produces = __detail::ListAdaptor<std::string_view>;
	using Parameters = __detail::ListAdaptor<Parameter>;
	using Responses = __detail::MapAdaptor<Response>;

	Parameters parameters() const;
	Responses responses() const;
	Consumes consumes() const;
	Produces produces() const;
};

using Path = common::Path<Operation>;

class OpenAPIv2 final : public OpenAPI {
public:
	static constexpr uint8_t version = 2;

	OpenAPIv2(const OpenAPIv2&) = delete;
	OpenAPIv2(OpenAPIv2&&) noexcept = default;

	using Paths = __detail::MapAdaptor<Path>;
	using Definitions = __detail::MapAdaptor<Schema>;
	using Parameters = __detail::MapAdaptor<Parameter>;

	std::string_view swagger() const;
	Info info() const;
	std::string_view host() const;
	std::string_view basePath() const;
	Paths paths() const;
	Definitions definitions() const;
	Parameters parameters() const;
	// responses
	Tags tags() const;
	ExternalDocumentation externalDocs() const;

	Schema GetDefinedSchemaByReference(std::string_view);
};

void PrintStructDefinitions(
	const OpenAPIv2& file,
	const std::filesystem::path& input,
	const std::filesystem::path& output);

// Template impls
template <typename VisitorType>
constexpr Parameter::Location Parameter::Visit(const Parameter& parameter, VisitorType&& v) {
	using Visitor = typename std::decay_t<VisitorType>;
	const Location location = parameter.In();

	if constexpr (std::is_invocable_v<Visitor, const BodyParameter&>) {
		if (location == Location::body) {
			std::forward<Visitor>(v)(static_cast<const BodyParameter&>(parameter));
		}
	}
	return location;
}

} // namespace openapi::v2
