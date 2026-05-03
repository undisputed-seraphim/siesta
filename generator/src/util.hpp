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
} // namespace codegen

void ltrim(std::string_view& s);

void rtrim(std::string_view& s);

void trim(std::string_view& s);

bool compare_ignore_case(std::string_view l, std::string_view r) noexcept;

// Modifies a string to produce words that can be used as variable names in C++.
// This includes replacing hyphens with underscores, editing names that begin with a number,
// and modifying names that are C++ keywords.
void sanitize(std::string& input);
std::string sanitize(std::string_view input);

// Sanitize enum identifiers - handles dots and other special cases not covered by sanitize()
std::string sanitize_enum_identifier(std::string_view input);

void write_multiline_comment(std::ostream& out, std::string_view comment, std::string_view indent = "");

void decompose_http_query(std::string_view raw, std::function<void(std::string_view, std::string_view)>&& kv_cb);

// Escape a string for embedding in C++ source code
std::string escapeCppString(const std::string& s);

// Check if a type name is a synthetic C++ type (not a real AST type)
bool isSyntheticCppType(const std::string& name);

// Get the final component of a path (e.g., "/foo/bar/baz" -> "baz")
std::string_view component_path(std::string_view path) noexcept;

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
#define LOG_PARSE(...) log_to("PARSE", __VA_ARGS__)
#define LOG_DEP(...) log_to("DEP", __VA_ARGS__)
#define LOG_EMIT(...) log_to("EMIT", __VA_ARGS__)
#define LOG_SORT(...) log_to("SORT", __VA_ARGS__)
