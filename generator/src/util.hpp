// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdarg>
#include <cstdio>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>

namespace schema {
enum class PrimitiveKind;
enum class IntegerFormat;
enum class NumberFormat;
} // namespace schema

namespace codegen {
std::string primitiveToCpp(
	schema::PrimitiveKind kind,
	const std::optional<schema::IntegerFormat>& fmt = std::nullopt,
	const std::optional<schema::NumberFormat>& num_fmt = std::nullopt);

// Modifies a string to produce words that can be used as variable names in C++.
void sanitize(std::string& input);
std::string sanitize(std::string_view input);

// Sanitize enum identifiers - handles dots and other special cases not covered by sanitize()
std::string sanitize_enum_identifier(std::string_view input);

void write_multiline_comment(std::ostream& out, std::string_view comment, std::string_view indent = "");

// Escape a string for embedding in C++ source code
std::string escapeCppString(const std::string& s);

// Check if a type name is a synthetic C++ type (not a real AST type)
inline constexpr bool isSyntheticCppType(const std::string& name) {
	return name.rfind("std::", 0) == 0 || name == "int" || name == "long" || name == "short" || name == "unsigned" ||
		   name == "signed" || name == "char" || name == "wchar_t" || name == "bool" || name == "float" ||
		   name == "double" || name == "void";
}

// Get the final component of a path (e.g., "/foo/bar/baz" -> "baz")
inline constexpr std::string_view component_path(std::string_view path) noexcept {
	size_t pos = path.find_last_of('/');
	return path.substr(pos + 1);
}

// ---------------------------------------------------------------------------
// Logging — all go to stderr, prefixed by phase for easy filtering
// ---------------------------------------------------------------------------
inline void log_to(std::string_view tag, const char* fmt, ...) {
	va_list args;
	va_start(args, fmt);
	std::cerr << "[" << tag << "] ";
	std::vfprintf(stderr, fmt, args);
	std::cerr << "\n";
	va_end(args);
}
} // namespace codegen

#define LOG_PARSE(...) ::codegen::log_to("PARSE", __VA_ARGS__)
#define LOG_DEP(...) ::codegen::log_to("DEP", __VA_ARGS__)
#define LOG_EMIT(...) ::codegen::log_to("EMIT", __VA_ARGS__)
#define LOG_SORT(...) ::codegen::log_to("SORT", __VA_ARGS__)
