// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "openapi3.hpp"
#include "util.hpp"
#include <algorithm>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace codegen {

// --- Shared parameter description ---

struct ClientParam {
	std::string name;
	std::string wire_name;
	std::string cpp_type;
	bool required = false;
	std::string description;
	std::string location;
	std::string schema_type;
	std::string format;
};

// --- Auth type, shared between client and python generators ---

enum class AuthType { None, ApiKey, HttpBearer };

// --- Unified endpoint IR consumed by all backends ---

struct Endpoint {
	std::string method;
	std::string path;
	std::string path_template;
	std::string function_name;
	std::string summary;
	std::string description;
	std::string cpp_verb;
	std::vector<ClientParam> params;
	bool has_request_body = false;
	std::string body_type;
	std::string body_content_type;
	AuthType auth_type = AuthType::None;
	std::string auth_header_name;
};

// --- Shared helpers used during endpoint parsing ---

inline std::string refComponentName(std::string_view ref) {
	size_t pos = ref.rfind('/');
	if (pos != std::string_view::npos) return std::string(ref.substr(pos + 1));
	return std::string(ref);
}

inline std::string resolveRefName(std::string_view ref) {
	return "api::" + refComponentName(ref);
}

inline std::string sanitizeParamName(std::string name) {
	if (name == "token" || name == "result" || name == "error" || name == "next" || name == "type" ||
		name == "metadata" || name == "include" || name == "order" || name == "event_types") {
		name = "param_" + name;
	}
	for (char& c : name) {
		if (c == '[' || c == ']' || c == '(' || c == ')' || c == '{' || c == '}' || c == '.' || c == ',')
			c = '_';
	}
	return name;
}

inline bool isSupportedMethod(std::string_view method) {
	return method == "get" || method == "post" || method == "put" || method == "delete" ||
	       method == "patch" || method == "head" || method == "options";
}

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
		} else if (c == ':' || c == '-') {
			result += '_';
		} else {
			result += c;
			first_char = false;
		}
	}
	return result;
}

inline std::string schemaToCppType(const openapi::v3::JsonSchema& schema) {
	if (schema.IsRef()) return resolveRefName(schema.ref());

	auto t_type = schema.type();
	if (!t_type.empty()) {
		std::string t(t_type);
		if (t == "string") {
			auto fmt = schema.format();
			if (fmt.empty()) return "std::string";
			std::string f(fmt);
			if (f == "date-time" || f == "date") return "std::string";
			if (f == "byte" || f == "base64") return "std::string";
			if (f == "binary") return "std::string";
			return "std::string";
		}
		if (t == "integer") {
			auto fmt = schema.format();
			if (fmt.empty()) return "int64_t";
			std::string f(fmt);
			if (f == "int32") return "int32_t";
			if (f == "int64") return "int64_t";
			if (f == "uint32") return "uint32_t";
			if (f == "uint64") return "uint64_t";
			return "int64_t";
		}
		if (t == "number") {
			auto fmt = schema.format();
			if (fmt.empty()) return "double";
			std::string f(fmt);
			if (f == "float") return "float";
			if (f == "double") return "double";
			return "double";
		}
		if (t == "boolean") return "bool";
		if (t == "array") return "std::vector<boost::json::value>";
		if (t == "object") return "std::string";
	}
	return "std::string";
}

inline ClientParam resolveParameter(const openapi::v3::Parameter& raw_param,
                                   const std::unordered_map<std::string, ClientParam>& fetched_params) {
	// $ref parameters: resolve from fetched_params to avoid DOM navigation
	if (raw_param.IsRef()) {
		std::string ref_name = refComponentName(raw_param.ref());
		auto it = fetched_params.find(ref_name);
		if (it != fetched_params.end()) return it->second;
	}

	ClientParam cp;
	try {
		cp.name = std::string(raw_param.name());
		cp.wire_name = cp.name;
		cp.description = std::string(raw_param.description());
		auto schema = raw_param.schema();
		cp.schema_type = std::string(schema.type());
		if (!schema.format().empty()) cp.format = std::string(schema.format());
		cp.cpp_type = schemaToCppType(schema);
	} catch (...) {}
	cp.location = std::string(raw_param.in());
	try { cp.required = raw_param.required(); } catch (...) {}
	return cp;
}

// --- Main entry point: parse all endpoints from an OpenAPIv3 spec ---
// Backends consume the result, no longer parse independently.
std::vector<Endpoint> parseEndpoints(const openapi::v3::OpenAPIv3& spec);

} // namespace codegen
