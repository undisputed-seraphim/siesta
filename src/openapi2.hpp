#pragma once

#include <iterator>
#include <string>
#include <string_view>

#include <simdjson.h>

#include "util.hpp"

namespace openapi {

enum class RequestMethod {
	POST,
	PUT,
	GET,
	DELETE,
	PATCH, // common ones
	HEAD,
	CONNECT,
	OPTIONS,
	TRACE, // uncommon ones
	UNKNOWN
};

enum class JsonType { Object, Array, Primitive, Reference };

RequestMethod RequestMethodFromString(std::string_view key);
std::string_view RequestMethodToString(RequestMethod rm);
std::string_view JsonTypeToCppType(std::string_view type, std::string_view format = "");

namespace __detail {

// Returns true if there is no error on this simdjson result.
template <typename T>
inline bool simdjson_noerror(const simdjson::internal::simdjson_result_base<T>& ec) noexcept {
	return ec.error() == simdjson::error_code();
}

template <typename T>
class ListAdaptor final : public simdjson::dom::array {
public:
	class Iterator final : public simdjson::dom::array::iterator {
	public:
		using difference_type = simdjson::dom::array::iterator::difference_type;
		using value_type = T;
		using pointer = T*;
		using reference = T&;
		using iterator_category = std::bidirectional_iterator_tag;

		Iterator()
			: simdjson::dom::array::iterator() {}
		Iterator(simdjson::dom::array::iterator&& it)
			: simdjson::dom::array::iterator(std::move(it)) {}
		//inline T operator*() const { return T(simdjson::dom::array::iterator::operator*()); }
		inline T operator*() const {
			auto res = simdjson::dom::array::iterator::operator*();
			return T(res);
		}
	};
	using iterator_type = Iterator;

	ListAdaptor()
		: simdjson::dom::array()
		, _is_valid(false) {}
	ListAdaptor(const simdjson::dom::array& array)
		: simdjson::dom::array(array)
		, _is_valid(true) {}
	inline Iterator begin() const noexcept { return _is_valid ? Iterator(simdjson::dom::array::begin()) : Iterator(); }
	inline Iterator end() const noexcept { return _is_valid ? Iterator(simdjson::dom::array::end()) : begin(); }
	inline size_t size() const noexcept { return _is_valid ? simdjson::dom::array::size() : 0; }
	inline bool empty() const noexcept { return (size() == 0); }

private:
	bool _is_valid;
};

template <typename T>
class MapAdaptor final : public simdjson::dom::object {
public:
	class Iterator final : public simdjson::dom::object::iterator {
	public:
		struct value_type {
			std::string_view str;
			T val;
		};

		Iterator()
			: simdjson::dom::object::iterator() {}
		Iterator(simdjson::dom::object::iterator&& it)
			: simdjson::dom::object::iterator(std::move(it)) {}
		inline const value_type operator*() const { return value_type{this->key(), T(this->value().get_object())}; }
	};
	using iterator_type = Iterator;
	MapAdaptor()
		: simdjson::dom::object()
		, _is_valid(false) {}
	MapAdaptor(const simdjson::dom::object& obj)
		: simdjson::dom::object(obj)
		, _is_valid(true) {}
	inline Iterator begin() const noexcept { return _is_valid ? Iterator(simdjson::dom::object::begin()) : Iterator(); }
	inline Iterator end() const noexcept { return _is_valid ? Iterator(simdjson::dom::object::end()) : begin(); }
	inline size_t size() const noexcept { return _is_valid ? simdjson::dom::object::size() : 0; }
	inline bool empty() const noexcept { return (size() == 0); }

private:
	bool _is_valid;
};

template <typename T>
class OpenAPIObject {
public:
	OpenAPIObject()
		: _is_valid(false)
		, _json() {}
	OpenAPIObject(simdjson::dom::object&& json)
		: _is_valid(true)
		, _json(json) {}
	OpenAPIObject(const OpenAPIObject& other)
		: _is_valid(other._is_valid)
		, _json(other._json) {}
	OpenAPIObject(OpenAPIObject&& other)
		: _is_valid(std::move(other._is_valid))
		, _json(std::move(other._json)) {}
	OpenAPIObject(const simdjson::internal::simdjson_result_base<simdjson::dom::object>& ec)
		: _is_valid(ec.error() == simdjson::error_code())
		, _json(_is_valid ? ec.value_unsafe() : decltype(_json)()) {}

	// True if this object is valid JSON, otherwise false.
	inline operator bool() const noexcept { return _is_valid; }

protected:
	template <typename U>
	U _GetObjectIfExist(std::string_view key) const {
		const auto& v = _json.at_key(key);
		return simdjson_noerror(v) ? U(v) : U();
	}
	template <typename U>
	U _GetValueIfExist(std::string_view key) const {
		const auto& v = _json.at_key(key);
		return simdjson_noerror(v) ? v.get<U>().value() : U();
	}

	bool _is_valid;
	simdjson::dom::object _json;
};

} // namespace __detail

class ExternalDocumentation : public __detail::OpenAPIObject<ExternalDocumentation> {
public:
	using __detail::OpenAPIObject<ExternalDocumentation>::OpenAPIObject;

	std::string_view description() const;
	std::string_view url() const;
};

class Tag : public __detail::OpenAPIObject<Tag> {
public:
	using __detail::OpenAPIObject<Tag>::OpenAPIObject;

	std::string_view name() const;
	std::string_view description() const;
	ExternalDocumentation externalDocs() const;
};

// A limited subset of JSON-Schema's items object. It is used by parameter definitions that are not located in "body".
class Item : public __detail::OpenAPIObject<Item> {
public:
	using __detail::OpenAPIObject<Item>::OpenAPIObject;
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

	std::string_view name() const;
	std::string_view in() const;
	std::string_view description() const;
	bool required() const;

	// If 'in' is 'body'
	Schema schema() const;
};

class Header : public Item {
public:
	using Item::Item;
	Header(Item& item)
		: Item(item) {}

	std::string_view description() const;
};

class Response : public __detail::OpenAPIObject<Response> {
public:
	using __detail::OpenAPIObject<Response>::OpenAPIObject;
	using Headers = __detail::MapAdaptor<Header>;

	std::string_view description() const;
	Schema schema() const;
	Headers headers() const;
};

class Operation : public __detail::OpenAPIObject<Operation> {
public:
	using __detail::OpenAPIObject<Operation>::OpenAPIObject;
	using Tags = __detail::ListAdaptor<std::string_view>;
	using Parameters = __detail::ListAdaptor<Parameter>;
	using Responses = __detail::MapAdaptor<Response>;

	Tags tags() const;
	std::string_view summary() const;
	std::string_view description() const;
	ExternalDocumentation externalDocs() const;
	std::string_view operationId() const;
	Parameters parameters() const;
	Responses responses() const;
	bool deprecated() const;
};

class Path : public __detail::OpenAPIObject<Path> {
public:
	using __detail::OpenAPIObject<Path>::OpenAPIObject;
	using Operations = __detail::MapAdaptor<Operation>;

	Operations operations() const { return Operations(_json); }
};

class Info : public __detail::OpenAPIObject<Info> {
public:
	using __detail::OpenAPIObject<Info>::OpenAPIObject;

	std::string_view title() const;
	std::string_view description() const;
	std::string_view terms_of_service() const;
	std::string_view version() const;
};

class OpenAPI2 : private __detail::OpenAPIObject<OpenAPI2> {
public:
	OpenAPI2() noexcept {}
	OpenAPI2(const OpenAPI2&) = delete;
	OpenAPI2(OpenAPI2&&) noexcept;

	using Paths = __detail::MapAdaptor<Path>;
	using Definitions = __detail::MapAdaptor<Schema>;
	using Parameters = __detail::MapAdaptor<Parameter>;
	using Tags = __detail::ListAdaptor<Tag>;

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

	// Should return false if JSON parsing fails or if file is not an OpenAPI swagger file.
	bool Load(const std::string& path);

	Schema GetDefinedSchemaByReference(std::string_view);

private:

	simdjson::dom::parser _parser; // Lifetime of document depends on lifetime of parser, so parser must be kept alive.
								   // simdjson::dom::element _root;
};

// Synthesize a function name give a path and its verb.
// Use this to get a C++-compatible function name when the globally unique operationId is unavailable.
std::string SynthesizeFunctionName(std::string_view pathstr, RequestMethod verb);

} // namespace openapi
