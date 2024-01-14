#pragma once

#include <filesystem>
#include <functional>
#include <iterator>
#include <string>
#include <string_view>

#include <simdjson.h>

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

// Synthesize a function name give a path and its verb.
// Use this to get a C++-compatible function name when the globally unique operationId is unavailable.
std::string SynthesizeFunctionName(std::string_view pathstr, RequestMethod verb);

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

		Iterator() noexcept
			: simdjson::dom::array::iterator() {}
		Iterator(simdjson::dom::array::iterator&& it) noexcept
			: simdjson::dom::array::iterator(std::move(it)) {}
		// inline T operator*() const { return T(simdjson::dom::array::iterator::operator*()); }
		inline T operator*() const {
			auto res = simdjson::dom::array::iterator::operator*();
			return T(res);
		}
	};
	using iterator_type = Iterator;

	ListAdaptor() noexcept
		: simdjson::dom::array()
		, _is_valid(false) {}
	ListAdaptor(const simdjson::dom::array& array) noexcept
		: simdjson::dom::array(array)
		, _is_valid(true) {}
	inline Iterator begin() const noexcept { return _is_valid ? Iterator(simdjson::dom::array::begin()) : Iterator(); }
	inline Iterator end() const noexcept { return _is_valid ? Iterator(simdjson::dom::array::end()) : begin(); }
	inline size_t size() const noexcept { return _is_valid ? simdjson::dom::array::size() : 0; }
	inline bool empty() const noexcept { return (size() == 0); }

	const T& front() const noexcept { return *(this->begin()); }

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

		Iterator() noexcept
			: simdjson::dom::object::iterator() {}
		Iterator(simdjson::dom::object::iterator&& it) noexcept
			: simdjson::dom::object::iterator(std::move(it)) {}
		inline const value_type operator*() const { return value_type{this->key(), T(this->value().get_object())}; }
	};
	using iterator_type = Iterator;
	MapAdaptor() noexcept
		: simdjson::dom::object()
		, _is_valid(false) {}
	MapAdaptor(const simdjson::dom::object& obj) noexcept
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
class Object {
public:
	Object() noexcept
		: _is_valid(false)
		, _json() {}
	Object(simdjson::dom::object&& json) noexcept
		: _is_valid(true)
		, _json(json) {}
	Object(const Object& other) noexcept
		: _is_valid(other._is_valid)
		, _json(other._json) {}
	Object(Object&& other) noexcept
		: _is_valid(std::move(other._is_valid))
		, _json(std::move(other._json)) {}
	Object(const simdjson::internal::simdjson_result_base<simdjson::dom::object>& ec) noexcept
		: _is_valid(ec.error() == simdjson::error_code())
		, _json(_is_valid ? ec.value_unsafe() : decltype(_json)()) {}

	// True if this object is valid JSON, otherwise false.
	inline operator bool() const noexcept { return _is_valid; }

protected:
	template <typename U>
	U _GetObjectIfExist(std::string_view key) const noexcept {
		const auto& v = _json.at_key(key);
		return simdjson_noerror(v) ? U(v) : U();
	}
	template <typename U>
	U _GetValueIfExist(std::string_view key) const noexcept {
		const auto& v = _json.at_key(key);
		return simdjson_noerror(v) ? v.get<U>().value() : U();
	}

	bool _is_valid;
	simdjson::dom::object _json;
};

} // namespace __detail

// Namespace used to hold objects that are identical across versions of OpenAPI specification.
namespace common {

class ExternalDocumentation final : public __detail::Object<ExternalDocumentation> {
public:
	using __detail::Object<ExternalDocumentation>::Object;

	std::string_view description() const;
	std::string_view url() const;
};

class Contact final : public __detail::Object<Contact> {
public:
	using __detail::Object<Contact>::Object;

	std::string_view name() const;
	std::string_view url() const;
	std::string_view email() const;
};

class License final : public __detail::Object<License> {
public:
	using __detail::Object<License>::Object;

	std::string_view name() const;
	std::string_view identifier() const;
	std::string_view url() const;
};

class Info final : public __detail::Object<Info> {
public:
	using __detail::Object<Info>::Object;

	std::string_view title() const;
	std::string_view description() const;
	std::string_view termsOfService() const;
	Contact contact() const;
	License license() const;
	std::string_view version() const;
};

class SecuritySchema : public __detail::Object<SecuritySchema> {
public:
	using __detail::Object<SecuritySchema>::Object;

	std::string_view type() const;
	std::string_view description() const;
	std::string_view name() const;
	std::string_view in() const;
};

class Tag : public __detail::Object<Tag> {
public:
	using __detail::Object<Tag>::Object;

	std::string_view name() const;
	std::string_view description() const;
	ExternalDocumentation externalDocs() const;
};
using Tags = __detail::ListAdaptor<Tag>;

class Parameter : public __detail::Object<Parameter> {
public:
	using __detail::Object<Parameter>::Object;

	std::string_view name() const;
	std::string_view in() const;
	std::string_view description() const;
	bool required() const;
};

class Operation : public __detail::Object<Operation> {
public:
	using __detail::Object<Operation>::Object;
	using Tags = __detail::ListAdaptor<std::string_view>;

	Tags tags() const;
	std::string_view summary() const;
	std::string_view description() const;
	ExternalDocumentation externalDocs() const;
	std::string_view operationId() const;
	bool deprecated() const;
};

template <typename OperationType>
class Path : public __detail::Object<Path<OperationType>> {
public:
	using Base = typename __detail::Object<Path<OperationType>>;
	using Base::Base;
	using Operations = __detail::MapAdaptor<OperationType>;

	Operations operations() const { return Operations(Base::_json); }
};

// Ref is a template that decorates any object with a "get $ref" function.
template <typename VariantType>
class Ref : public VariantType {
public:
	using Type = typename std::decay_t<VariantType>;
	using Type::Type;

	std::string_view ref() const { return Type::template _GetValueIfExist<std::string_view>("$ref"); }
	bool IsRef() const noexcept { return !ref().empty(); }
};

} // namespace common

namespace json_schema {

class JsonSchema : public common::Ref<__detail::Object<JsonSchema>> {
public:
	using Base = common::Ref<__detail::Object<JsonSchema>>;
	using Base::Base;

	enum class Type {
		unknown,
		string,
		number,
		integer,
		boolean,
		object,
		array,
	};
	// Returns true for strings, numbers, and booleans.
	static bool IsPrimitive(Type) noexcept;
	static bool IsPrimitive(const JsonSchema&) noexcept;

	template <typename VisitorType>
	Type Visit(VisitorType&& v) const;

	Type Type_() const noexcept;

	std::string_view type() const;
	std::string_view name() const;
	std::string_view format() const;
	std::string_view example() const;

	using SchemaList = __detail::ListAdaptor<JsonSchema>;
	SchemaList anyOf() const;
	SchemaList oneOf() const;
};

class String final : public JsonSchema {
protected:
	using JsonSchema::JsonSchema;

public:
	uint64_t minLength() const;
	uint64_t maxLength() const;
	std::string_view pattern() const;
};

class Number : public JsonSchema {
protected:
	using JsonSchema::JsonSchema;

public:
	int64_t maximum() const;
	bool exclusiveMaximum() const;
	int64_t minimum() const;
	bool exclusiveMinimum() const;
	int64_t multipleOf() const;
};

class Integer final : public Number {
protected:
	using Number::Number;

public:
	// Same as number
};

class Boolean final : public JsonSchema {
protected:
	using JsonSchema::JsonSchema;

public:
};

class Object final : public JsonSchema {
protected:
	using JsonSchema::JsonSchema;

public:
	using Properties = __detail::MapAdaptor<JsonSchema>;

	uint64_t minProperties() const;
	uint64_t maxProperties() const;
	bool required() const;
	Properties properties() const;
};

class Array final : public JsonSchema {
protected:
	using JsonSchema::JsonSchema;

public:
	using Items = JsonSchema;

	Items items() const;
	uint64_t minItems() const;
	uint64_t maxItems() const;
	uint64_t minContains() const;
	uint64_t maxContains() const;
	bool uniqueItems() const;
};

template <typename VisitorType>
JsonSchema::Type JsonSchema::Visit(VisitorType&& v) const {
	using Visitor = typename std::decay_t<VisitorType>;
	const Type type = this->Type_();
	if constexpr (std::is_invocable_v<Visitor, const String&>) {
		if (type == Type::string) {
			std::forward<Visitor>(v)(static_cast<const String&>(*this));
		}
	}
	if constexpr (std::is_invocable_v<Visitor, const Number&>) {
		if (type == Type::number) {
			std::forward<Visitor>(v)(static_cast<const Number&>(*this));
		}
	}
	if constexpr (std::is_invocable_v<Visitor, const Integer&>) {
		if (type == Type::integer) {
			std::forward<Visitor>(v)(static_cast<const Integer&>(*this));
		}
	}
	if constexpr (std::is_invocable_v<Visitor, const Boolean&>) {
		if (type == Type::boolean) {
			std::forward<Visitor>(v)(static_cast<const Boolean&>(*this));
		}
	}
	if constexpr (std::is_invocable_v<Visitor, const Array&>) {
		if (type == Type::array) {
			std::forward<Visitor>(v)(static_cast<const Array&>(*this));
		}
	}
	if constexpr (std::is_invocable_v<Visitor, const json_schema::Object&>) {
		if (type == Type::object) {
			std::forward<Visitor>(v)(static_cast<const json_schema::Object&>(*this));
		}
	}
	if constexpr (std::is_invocable_v<Visitor, std::string_view>) {
		if (this->IsRef()) {
			std::forward<Visitor>(v)(this->ref());
		}
	}
	return type;
}

} // namespace json_schema

// Root object, must be alive throughout the entire procedure
class OpenAPI : protected __detail::Object<OpenAPI> {
public:
	OpenAPI() noexcept;
	OpenAPI(const OpenAPI&) = delete;
	OpenAPI(OpenAPI&&) noexcept;

	bool Load(const std::string& path) noexcept;
	int MajorVersion() const noexcept;

protected:
	simdjson::dom::parser _parser; // Lifetime of document depends on lifetime of parser, so parser must be kept alive.
								   // simdjson::dom::element _root;
};

} // namespace openapi

#ifndef XSTR
#ifndef STR
#define XSTR(a) STR(a)
#define STR(a) #a
#endif // STR
#endif // XSTR

#ifndef VALUE_FIELD
#define SIESTA_OPENAPI_VALUE_FIELD(name, type)                                                                         \
	type name() const { return _GetValueIfExist<type>(STR(name)); }
#endif // VALUE_FIELD

#ifndef OBJECT_FIELD
#define SIESTA_OPENAPI_OBJECT_FIELD(name, type)                                                                        \
	type name() const { return _GetObjectIfExist<type>(STR(name)); }
#endif // OBJECT_FIELD