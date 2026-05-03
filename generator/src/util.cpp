// SPDX-License-Identifier: Apache-2.0
#include <algorithm>

#include "util.hpp"

#include "schema_ast.hpp"

using namespace std::literals;

namespace codegen {

std::string primitiveToCpp(
	schema::PrimitiveKind kind,
	const std::optional<schema::IntegerFormat>& fmt,
	const std::optional<schema::NumberFormat>& num_fmt) {
	switch (kind) {
	case schema::PrimitiveKind::String:
		return "std::string";
	case schema::PrimitiveKind::Integer:
		if (fmt && *fmt == schema::IntegerFormat::Int64)
			return "int64_t";
		if (fmt && *fmt == schema::IntegerFormat::UInt64)
			return "uint64_t";
		if (fmt && *fmt == schema::IntegerFormat::UInt32)
			return "uint32_t";
		return "int32_t";
	case schema::PrimitiveKind::Number:
		if (num_fmt && *num_fmt == schema::NumberFormat::Double)
			return "double";
		return "float";
	case schema::PrimitiveKind::Boolean:
		return "bool";
	case schema::PrimitiveKind::Null:
		return "std::nullptr_t";
	}
	return "std::string";
}

void sanitize(std::string& input) {
	if (input.empty()) {
		return;
	}

	// Replace characters with underscore.
	std::replace_if(
		input.begin(),
		input.end(),
		[](char c) -> bool {
			constexpr auto chars = std::array{'/', '-', '.', ':', '+', ' ', '(', ')', '@'};
			return std::any_of(chars.begin(), chars.end(), [c](char d) { return c == d; });
		},
		'_');

	// Reserved keywords in C++.
	constexpr auto reserved = std::array{
		"operator"sv,
		"long"sv,
		"short"sv,
		"public"sv,
		"protected"sv,
		"private"sv,
		"default"sv,
		"delete"sv,
		"namespace"sv,
		"template"sv,
		"static"sv,
		"const"sv,
		"volatile"sv,
		"virtual"sv,
		"explicit"sv,
		"friend"sv,
		"typedef"sv,
		"typename"sv,
		"enum"sv,
		"struct"sv,
		"class"sv,
		"union"sv,
		"if"sv,
		"else"sv,
		"for"sv,
		"while"sv,
		"do"sv,
		"switch"sv,
		"case"sv,
		"break"sv,
		"continue"sv,
		"return"sv,
		"goto"sv,
		"try"sv,
		"catch"sv,
		"throw"sv,
		"new"sv,
		"delete"sv,
		"this"sv,
		"sizeof"sv,
		"alignof"sv,
		"decltype"sv,
		"noexcept"sv,
		"nullptr"sv,
		"true"sv,
		"false"sv,
		"bool"sv,
		"char"sv,
		"wchar_t"sv,
		"char16_t"sv,
		"char32_t"sv,
		"signed"sv,
		"unsigned"sv,
		"int"sv,
		"float"sv,
		"double"sv,
		"void"sv,
		"auto"sv,
		"register"sv,
		"extern"sv,
		"mutable"sv,
		"inline"sv,
		"constexpr"sv,
		"consteval"sv,
		"constinit"sv,
		"concept"sv,
		"requires"sv,
		"co_await"sv,
		"co_yield"sv,
		"co_return"sv,
		"module"sv,
		"export"sv,
		"import"sv,
		// GCC/Clang predefined macros that collide as identifiers
		"unix"sv,
		"linux"sv,
		"i386"sv,
		"x86_64"sv,
		"amd64"sv,
		"arm"sv,
		"aarch64"sv,
		"__unix"sv,
		"__linux"sv,
		"__linux__"sv,
		"__unix__"sv,
		"unix__"sv,
	};
	if (std::any_of(reserved.begin(), reserved.end(), [&input](const std::string_view& kw) { return input == kw; })) {
		input.push_back('_');
	}

	// Names cannot start with a number.
	if (std::isdigit(input[0])) {
		input.insert(0, 1, '_');
	}
}

std::string sanitize(std::string_view input) {
	std::string ret(input);
	sanitize(ret);
	return ret;
}

std::string sanitize_enum_identifier(std::string_view input) {
	std::string result(input);

	// First apply basic sanitization
	sanitize(result);

	// Additional handling for dots and other edge cases
	// Replace dots with underscores (e.g., "api_key.created" -> "api_key_created")
	for (char& c : result) {
		if (c == '.') {
			c = '_';
		}
	}

	// If still starts with a number, add underscore prefix
	if (!result.empty() && std::isdigit(result[0])) {
		result.insert(0, 1, '_');
	}

	return result;
}

void write_multiline_comment(std::ostream& out, std::string_view comment, std::string_view indent) {
	while (!comment.empty()) {
		while (!comment.empty() && std::isspace(comment.front())) comment.remove_prefix(1);
		while (!comment.empty() && std::isspace(comment.back())) comment.remove_suffix(1);
		auto br = comment.find_first_of('\n');
		out << indent << "// " << comment.substr(0, br) << '\n';
		if (br == std::string_view::npos)
			break;
		comment.remove_prefix(br);
	}
}

std::string escapeCppString(const std::string& s) {
	std::string result;
	result.reserve(s.size() * 2);
	for (char c : s) {
		switch (c) {
		case '\\':
			result += "\\\\";
			break;
		case '"':
			result += "\\\"";
			break;
		case '\n':
			result += "\\n";
			break;
		case '\r':
			result += "\\r";
			break;
		case '\t':
			result += "\\t";
			break;
		default:
			result += c;
		}
	}
	return result;
}

} // namespace codegen
