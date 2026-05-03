#pragma once

#include "codegen_client.hpp"
#include "openapi3.hpp"
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace codegen {

// Extract the final path component from a $ref URL (e.g. "#/components/parameters/Symbol" → "Symbol")
inline std::string refComponentName(std::string_view ref) {
	size_t pos = ref.rfind('/');
	if (pos != std::string_view::npos) {
		return std::string(ref.substr(pos + 1));
	}
	return std::string(ref);
}

// Convert $ref to C++ type name (e.g. "Symbol" → "api::Symbol")
inline std::string resolveRefName(std::string_view ref) {
	return "api::" + refComponentName(ref);
}

// Sanitize parameter names to avoid C++ keyword conflicts
inline std::string sanitizeParamName(std::string name) {
	if (name == "token" || name == "result" || name == "error" || name == "next" || name == "type" ||
		name == "metadata" || name == "include" || name == "order" || name == "event_types") {
		name = "param_" + name;
	}
	for (char& c : name) {
		if (c == '[' || c == ']' || c == '(' || c == ')' || c == '{' || c == '}' || c == '.' || c == ',') {
			c = '_';
		}
	}
	return name;
}

// Check if an HTTP method is supported
inline bool isSupportedMethod(std::string_view method) {
	return method == "get" || method == "post" || method == "put" || method == "delete" ||
	       method == "patch" || method == "head" || method == "options";
}

// Convert HTTP method + path to a C++ function name
inline std::string generateFunctionName(std::string_view method, std::string_view path) {
	std::string result(method);
	result += "__";
	bool first_char = true;
	for (char c : path) {
		if (c == '/') {
			if (!first_char) result += '_';
		} else if (c == '{') {
			result += '_';
		} else if (c == '}') {
			// skip
		} else if (c == ':' || c == '-') {
			result += '_';
		} else {
			result += c;
			first_char = false;
		}
	}
	return result;
}

} // namespace codegen
