// SPDX-License-Identifier: Apache-2.0
#include "IR/EndpointIR.hpp"
#include "Frontend/openapi.hpp"
#include "Frontend/openapi3.hpp"
#include <algorithm>

namespace codegen {

std::vector<Endpoint> parseEndpoints(const openapi::v3::OpenAPIv3& spec) {
	std::vector<Endpoint> endpoints;
	const auto& comp_params_raw = spec.components().parameters();

	std::unordered_map<std::string, ClientParam> fetched_params;
	for (const auto& [n, p_obj] : comp_params_raw) {
		ClientParam cp;
		cp.name = std::string(p_obj.name());
		cp.wire_name = cp.name;
		cp.location = std::string(p_obj.in());
		cp.required = p_obj.required();
		cp.description = std::string(p_obj.description());
		auto schema = p_obj.schema();
		cp.schema_type = std::string(schema.type());
		if (!schema.format().empty()) cp.format = std::string(schema.format());
		cp.cpp_type = schemaToCppType(schema);
		fetched_params[std::string(n)] = std::move(cp);
	}

	const auto& comp_bodies_raw = spec.components().requestBodies();
	std::unordered_map<std::string, std::string> body_schema_names;
	for (const auto& [n, b_obj] : comp_bodies_raw) {
		auto content = b_obj.content();
		for (const auto& [ct, mt] : content) {
			(void)ct;
			auto schema = mt.schema();
			if (schema.IsRef())
				body_schema_names[std::string(n)] = resolveRefName(schema.ref());
			else
				body_schema_names[std::string(n)] = schemaToCppType(schema);
			break;
		}
	}

	struct SchemeInfo {
		std::string type, name, in, scheme;
	};
	std::unordered_map<std::string, SchemeInfo> security_schemes;
	for (const auto& [name, scheme] : spec.components().securitySchemes()) {
		SchemeInfo info;
		info.type = std::string(scheme.type());
		if (info.type == "apiKey") {
			info.name = std::string(scheme.name());
			info.in = std::string(scheme.in());
		} else if (info.type == "http") {
			info.scheme = std::string(scheme.scheme());
		}
		security_schemes[std::string(name)] = std::move(info);
	}
	bool has_global_security = spec.hasGlobalSecurity();

	// Single pass over paths — extract all data into C++ containers.
	// simdjson DOM references cannot survive past the paths() iteration
	// scope, so we materialise everything here.
	std::unordered_map<std::string, ClientParam> path_params_map;

	// Lightweight record of what we need per path
	struct ColOp {
		std::string path;
		std::string method;
		std::string summary;
		std::string description;
		std::vector<ClientParam> op_params;
		bool hasRequestBody = false;
		std::string bodyType;
		std::string bodyContentType;
		struct {
			bool is_ref = false;
			std::string ref_comp;
		} bodyRef;
		bool hasOpSecurity = false;
	};

	std::vector<ColOp> collected;

	const auto& paths = spec.paths();
	for (const auto& [path_sv, path_obj] : paths) {
		std::string path(path_sv);

		// Path-level parameters
		auto path_params_list = path_obj.parameters();
		for (const auto& param : path_params_list) {
			ClientParam cp = resolveParameter(param, fetched_params);
			path_params_map[cp.name] = cp;
		}

		// Operations
		auto path_ops = path_obj.operations();
		for (const auto& [method_sv, op_obj] : path_ops) {
			ColOp co;
			co.path = path;
			co.method = std::string(method_sv);
			try {
				auto sum = op_obj.summary();
				if (!sum.empty()) co.summary = std::string(sum);
				auto desc = op_obj.description();
				if (!desc.empty()) co.description = std::string(desc);
			} catch (...) {}

			auto op_params_list = op_obj.parameters();
			for (const auto& param : op_params_list) {
				co.op_params.push_back(resolveParameter(param, fetched_params));
			}

			auto req_body = op_obj.requestBody();
			if (req_body) {
				co.hasRequestBody = true;
				auto ref_opt = req_body.TryGetRef();
				if (ref_opt.has_value()) {
					co.bodyRef.is_ref = true;
					co.bodyRef.ref_comp = refComponentName(ref_opt.value());
				} else {
					auto content = req_body.content();
					for (const auto& [ct, mt] : content) {
						co.bodyContentType = std::string(ct);
						auto schema = mt.schema();
						if (schema) co.bodyType = schemaToCppType(schema);
						if (co.bodyType.empty()) co.bodyType = "std::string";
						break;
					}
				}
			}

			try { co.hasOpSecurity = op_obj.HasKey("security"); } catch (...) {}

			collected.push_back(std::move(co));
		}
	}

	// Build endpoints from collected data (no DOM access)
	for (auto& co : collected) {
		std::string method = co.method;
		std::transform(method.begin(), method.end(), method.begin(), ::tolower);

		if (!isSupportedMethod(method)) continue;

		Endpoint ep;
		ep.method = method;
		ep.path = co.path;
		ep.cpp_verb = method == "delete" ? "delete_" : method;
		if (!co.summary.empty()) ep.summary = co.summary;
		if (!co.description.empty()) ep.description = co.description;
		ep.function_name = generateFunctionName(method, co.path);

		// Merge path-level + operation-level params
		std::unordered_map<std::string, ClientParam> op_overrides;
		std::vector<std::string> param_order;
		for (const auto& [name, _] : path_params_map) param_order.push_back(name);

		for (auto& cp : co.op_params) {
			if (path_params_map.find(cp.name) == path_params_map.end() &&
			    op_overrides.find(cp.name) == op_overrides.end())
				param_order.push_back(cp.name);
			op_overrides[cp.name] = cp;
		}

		auto lookup = [&](const std::string& name) -> const ClientParam& {
			auto oit = op_overrides.find(name);
			return (oit != op_overrides.end()) ? oit->second : path_params_map.at(name);
		};

		std::vector<ClientParam> ordered;
		for (const auto& name : param_order) {
			const auto& cp = lookup(name);
			if (cp.location == "path") ordered.push_back(cp);
		}
		for (const auto& name : param_order) {
			const auto& cp = lookup(name);
			if (cp.location == "query") ordered.push_back(cp);
		}
		for (const auto& name : param_order) {
			const auto& cp = lookup(name);
			if (cp.location == "header") ordered.push_back(cp);
		}
		for (auto& p : ordered) p.name = sanitizeParamName(p.name);
		ep.params = std::move(ordered);

		// Request body
		if (co.hasRequestBody) {
			if (co.bodyRef.is_ref) {
				auto it = body_schema_names.find(co.bodyRef.ref_comp);
				if (it != body_schema_names.end()) {
					ep.body_type = it->second;
					ep.has_request_body = true;
				}
			} else {
				ep.body_content_type = co.bodyContentType;
				ep.body_type = co.bodyType;
				ep.has_request_body = true;
			}
		}

		// Security
		if ((co.hasOpSecurity || (!co.hasOpSecurity && has_global_security)) && !security_schemes.empty()) {
			const auto& [_, info] = *security_schemes.begin();
			if (info.type == "apiKey" && info.in == "header") {
				ep.auth_type = AuthType::ApiKey;
				ep.auth_header_name = info.name;
			} else if (info.type == "http" && info.scheme == "bearer") {
				ep.auth_type = AuthType::HttpBearer;
				ep.auth_header_name = "Authorization";
			}
		}

		// Build path template
		std::string tmpl = co.path;
		for (const auto& pp : ep.params) {
			if (pp.location == "path") {
				std::string ph = "{" + pp.name + "}";
				size_t pos = tmpl.find(ph);
				while (pos != std::string::npos) {
					tmpl.replace(pos, ph.length(), "{}");
					pos = tmpl.find(ph, pos + 2);
				}
			}
		}
		ep.path_template = tmpl;

		endpoints.push_back(std::move(ep));
	}

	return endpoints;
}

} // namespace codegen
