#pragma once

#include <simdjson.h>

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