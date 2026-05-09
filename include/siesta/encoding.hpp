// SPDX-License-Identifier: Apache-2.0
#pragma once
/// Shared encoding utilities for generated siesta clients.
/// Included by each generated openapi_defs.hpp — not duplicated per project.

#include <cstdint>
#include <string>
#include <string_view>

namespace siesta {

inline std::string url_encode(std::string_view sv) {
	std::string result;
	result.reserve(sv.size() * 3);
	for (unsigned char c : sv) {
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		    (c >= '0' && c <= '9') || c == '-' || c == '_' ||
		    c == '.' || c == '~') {
			result += c;
		} else {
			result += '%';
			result += "0123456789ABCDEF"[c >> 4];
			result += "0123456789ABCDEF"[c & 15];
		}
	}
	return result;
}

inline std::string query_value(int32_t v)  { return std::to_string(v); }
inline std::string query_value(int64_t v)  { return std::to_string(v); }
inline std::string query_value(uint32_t v) { return std::to_string(v); }
inline std::string query_value(uint64_t v) { return std::to_string(v); }
inline std::string query_value(double v)   { return std::to_string(v); }
inline std::string query_value(float v)    { return std::to_string(v); }
inline std::string query_value(bool v)     { return v ? "true" : "false"; }
inline std::string query_value(const std::string& v) { return url_encode(v); }

// Catch-all: fires a readable static_assert when no overload matches.
// Enum types should have their own query_value overloads (emitted per schema).
template <typename T>
inline std::string query_value(const T&) {
	static_assert(sizeof(T) == 0, "query_value: no overload for this type.");
	return {};
}

} // namespace siesta
