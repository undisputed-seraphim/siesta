// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <concepts>
#include <string>

#include <fmt/format.h>
template <typename... Args>
std::string format(std::string_view form, Args&&... args) {
	return fmt::format(form, std::forward<Args>(args)...);
}


inline std::string& string_cast(std::string& str) noexcept {
	return str;
}

inline const std::string& string_cast(const std::string& str) noexcept {
	return str;
}

inline std::string string_cast(bool b) {
	return b ? "true" : "false";
}

inline std::string string_cast(std::floating_point auto v) {
	return std::to_string(v);
}

inline std::string string_cast(std::integral auto v) {
	return std::to_string(v);
}