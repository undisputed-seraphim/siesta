// SPDX-License-Identifier: Apache-2.0
#include "codegen_client.hpp"
#include "endpoint_util.hpp"
#include "openapi.hpp"
#include "openapi3.hpp"
#include <unordered_set>

namespace codegen {

ClientGenerator::ClientGenerator(const schema::NormalizedAST& ast, const openapi::v3::OpenAPIv3& spec)
	: ast_(ast)
	, spec_(spec) {
	endpoints_ = parseEndpoints();
}

ClientParam ClientGenerator::resolveAndMapParameter(const openapi::v3::Parameter& raw_param,
                                                      const std::unordered_map<std::string, ClientParam>& fetched_params) {
	ClientParam p;

	// Check if this is a $ref
	if (raw_param.IsRef()) {
		std::string ref_name = refComponentName(raw_param.ref());
		auto it = fetched_params.find(ref_name);
		if (it != fetched_params.end()) {
			p = it->second;
			return p;
		}
	}

	// Inline parameter or ref resolution failed - use typed methods
	p.name = std::string(raw_param.name());
	p.location = std::string(raw_param.in());
	p.required = raw_param.required();
	p.description = std::string(raw_param.description());

	auto schema = raw_param.schema();
	p.schema_type = std::string(schema.type());
	if (!schema.format().empty()) {
		p.format = std::string(schema.format());
	}
	p.cpp_type = schemaToCppType(schema);

	return p;
}

std::string ClientGenerator::schemaToCppType(const openapi::v3::JsonSchema& schema) {
	if (schema.IsRef()) {
		return resolveRefName(schema.ref());
	}

	std::string_view type = schema.type();
	std::string_view format = schema.format();

	if (type == "string") {
		return "std::string";
	} else if (type == "integer") {
		if (format == "int64")
			return "int64_t";
		if (format == "uint32")
			return "uint32_t";
		if (format == "uint64")
			return "uint64_t";
		return "int32_t";
	} else if (type == "number") {
		return "double";
	} else if (type == "boolean") {
		return "bool";
	} else if (type == "array") {
		const auto& arr = static_cast<const openapi::v3::Array&>(schema);
		auto items = arr.items();
		std::string elem_type = schemaToCppType(items);
		return "std::vector<" + elem_type + ">";
	} else if (type == "object") {
		return "std::string";
	}

	return "std::string";
}

std::vector<ClientEndpoint> ClientGenerator::parseEndpoints() {
	std::vector<ClientEndpoint> endpoints;
	const auto& paths = spec_.paths();
	const auto& comp_params_raw = spec_.components().parameters();

	// Pre-fetch components/parameters to avoid simdjson on-demand re-iteration
	std::unordered_map<std::string, ClientParam> fetched_params;
	for (const auto& [n, p_obj] : comp_params_raw) {
		ClientParam cp;
		cp.name = std::string(p_obj.name());
		cp.location = std::string(p_obj.in());
		cp.required = p_obj.required();
		cp.description = std::string(p_obj.description());
		auto schema = p_obj.schema();
		cp.schema_type = std::string(schema.type());
		if (!schema.format().empty()) {
			cp.format = std::string(schema.format());
		}
		cp.cpp_type = schemaToCppType(schema);
		fetched_params[std::string(n)] = std::move(cp);
	}

	// Pre-fetch components/requestBodies similarly
	const auto& comp_bodies_raw = spec_.components().requestBodies();
	std::unordered_map<std::string, std::string> body_schema_names;
	for (const auto& [n, b_obj] : comp_bodies_raw) {
		auto content = b_obj.content();
		for (const auto& [ct, mt] : content) {
			(void)ct;
			auto schema = mt.schema();
			if (schema.IsRef()) {
				body_schema_names[std::string(n)] = resolveRefName(schema.ref());
			} else {
				body_schema_names[std::string(n)] = schemaToCppType(schema);
			}
			break;
		}
	}

	// Collect path-level parameters into a map by name
	std::unordered_map<std::string, ClientParam> path_params_map;
	for (const auto& [path_sv, path_obj] : paths) {
		auto path_params_list = path_obj.parameters();
		for (const auto& param : path_params_list) {
			ClientParam cp = resolveAndMapParameter(param, fetched_params);
			path_params_map[cp.name] = cp;
		}
	}

	for (const auto& [path_sv, path_obj] : paths) {
		std::string path(path_sv);
		auto path_ops = path_obj.operations();

		for (const auto& [method_sv, op_obj] : path_ops) {
			std::string method(method_sv);
			std::transform(method.begin(), method.end(), method.begin(), ::tolower);

			if (!isSupportedMethod(method)) {
				continue;
			}

			ClientEndpoint ep;
			ep.method = method;
			ep.path = path;

			try {
				auto summary = op_obj.summary();
				if (!summary.empty()) {
					ep.summary = std::string(summary);
				}
				auto description = op_obj.description();
				if (!description.empty()) {
					ep.description = std::string(description);
				}
			} catch (...) {
			}

			ep.function_name = generateFunctionName(method, path);

			// Merge path-level and operation-level parameters, preserving order
			std::unordered_map<std::string, ClientParam> merged_params = path_params_map;
			std::vector<std::string> param_order;
			for (const auto& [name, _] : path_params_map) {
				param_order.push_back(name);
			}

			auto op_params_list = op_obj.parameters();
			for (const auto& param : op_params_list) {
				ClientParam cp = resolveAndMapParameter(param, fetched_params);
				if (merged_params.find(cp.name) == merged_params.end()) {
					param_order.push_back(cp.name);
				}
				merged_params[cp.name] = cp;
			}

			// Convert to ordered vector: path params first, then query, then header
			std::vector<ClientParam> ordered_params;
			for (const auto& name : param_order) {
				const auto& cp = merged_params[name];
				if (cp.location == "path") {
					ordered_params.push_back(cp);
				}
			}
			for (const auto& name : param_order) {
				const auto& cp = merged_params[name];
				if (cp.location == "query") {
					ordered_params.push_back(cp);
				}
			}
			for (const auto& name : param_order) {
				const auto& cp = merged_params[name];
				if (cp.location == "header") {
					ordered_params.push_back(cp);
				}
			}

			// Sanitize parameter names to avoid conflicts
			for (auto& p : ordered_params) {
				p.name = sanitizeParamName(p.name);
			}
			ep.params = std::move(ordered_params);

			// Extract request body info
			auto req_body = op_obj.requestBody();
			if (req_body) {
				auto ref_opt = req_body.TryGetRef();
				if (ref_opt.has_value()) {
					std::string body_name = refComponentName(ref_opt.value());
					auto it = body_schema_names.find(body_name);
					if (it != body_schema_names.end()) {
						ep.body_type = it->second;
						ep.has_request_body = true;
					}
				} else {
					// Inline request body
					auto content = req_body.content();
					for (const auto& [ct, mt] : content) {
						ep.body_content_type = std::string(ct);
						auto schema = mt.schema();
						if (schema) {
							ep.body_type = schemaToCppType(schema);
						}
						if (ep.body_type.empty()) {
							ep.body_type = "std::string";
						}
						ep.has_request_body = true;
						break;
					}
				}
			}

			// Build path template
			std::string template_str = path;
			bool has_path_placeholders = path.find('{') != std::string_view::npos;

			if (has_path_placeholders) {
				for (const auto& pp : ep.params) {
					if (pp.location == "path") {
						std::string placeholder = "{" + pp.name + "}";
						size_t pos = template_str.find(placeholder);
						while (pos != std::string::npos) {
							template_str.replace(pos, placeholder.length(), "{}");
							pos = template_str.find(placeholder, pos + 2);
						}
					}
				}
			}

			// Append query parameters
			std::vector<const ClientParam*> query_params;
			for (const auto& p : ep.params) {
				if (p.location == "query") {
					query_params.push_back(&p);
				}
			}

			if (!query_params.empty()) {
				template_str += '?';
				for (size_t i = 0; i < query_params.size(); ++i) {
					if (i > 0) {
						template_str += '&';
					}
					template_str += query_params[i]->name + "={}";
				}
			}

			ep.path_template = template_str;

			endpoints.push_back(std::move(ep));
		}
	}

	return endpoints;
}

void ClientGenerator::emitClassHeader(std::ostream& out) {
	out << "\n";
	out << "class Client : public ::siesta::beast::ClientBase {\n";
	out << "public:\n";
	out << "\tusing ::siesta::beast::ClientBase::ClientBase;\n";
	out << "\tusing ::siesta::beast::ClientBase::Config;\n";
	out << "\tusing ::siesta::beast::ClientBase::shared_from_this;\n";
	out << "\n";
}

void ClientGenerator::emitEndpoint(std::ostream& out, const ClientEndpoint& ep) {
	std::string text = ep.description.empty() ? ep.summary : ep.description;
	if (!text.empty()) {
		write_multiline_comment(out, text, "\t");
	}

	emitMethodSignature(out, ep);
	out << "\n\t{\n";
	emitMethodBody(out, ep);
	out << "\t}\n";
	out << "\n";
}

void ClientGenerator::emitMethodSignature(std::ostream& out, const ClientEndpoint& ep) {
	out << "\tauto " << ep.function_name << "(";

	for (size_t i = 0; i < ep.params.size(); ++i) {
		if (i > 0) {
			out << ", ";
		}
		const auto& p = ep.params[i];
		if (!p.required) {
			out << "std::optional<" << p.cpp_type << "> ";
		} else {
			out << p.cpp_type << " ";
		}
		out << p.name;
	}

	if (!ep.params.empty()) {
		out << ", ";
	}
	out << "::boost::asio::completion_token_for<void(outcome_type)> auto&& token";
	out << ")";
}

void ClientGenerator::emitMethodBody(std::ostream& out, const ClientEndpoint& ep) {
	out << "\t\tconstexpr std::string_view path = \"" << escapeCppString(ep.path_template) << "\";\n";
	out << "\t\trequest_type req;\n";

	// Build format args list
	std::vector<const ClientParam*> path_params;
	std::vector<const ClientParam*> query_params;

	for (const auto& p : ep.params) {
		if (p.location == "path") {
			path_params.push_back(&p);
		} else if (p.location == "query") {
			query_params.push_back(&p);
		}
	}

	bool has_format_args = !path_params.empty() || !query_params.empty();

	if (has_format_args) {
		out << "\t\tstd::string target_path(static_cast<std::string_view>(path));\n";
		for (const auto* p : path_params) {
			bool is_string = p->cpp_type.find("string") != std::string::npos;
			if (p->required) {
				if (is_string) {
					out << "\t\tif (auto _p = target_path.find(\"{}\"); _p != std::string::npos) target_path.replace(_p, 2, "
						<< p->name << ");\n";
				} else {
					out << "\t\tif (auto _p = target_path.find(\"{}\"); _p != std::string::npos) target_path.replace(_p, 2, std::to_string("
						<< p->name << "));\n";
				}
			} else {
				out << "\t\tif (" << p->name << ".has_value()) {\n";
				if (is_string) {
					out << "\t\t\tif (auto _p = target_path.find(\"{}\"); _p != std::string::npos) target_path.replace(_p, 2, *("
						<< p->name << "));\n";
				} else {
					out << "\t\t\tif (auto _p = target_path.find(\"{}\"); _p != std::string::npos) target_path.replace(_p, 2, std::to_string(*("
						<< p->name << ")));\n";
				}
				out << "\t\t}\n";
			}
		}
		out << "\t\tstd::string query_params;\n";
		for (const auto* p : query_params) {
			bool is_vector = p->cpp_type.find("vector") != std::string::npos;
			bool is_string = p->cpp_type.find("string") != std::string::npos;
			bool is_vector_string = is_vector && is_string;
			if (p->required) {
				if (is_vector_string) {
					out << "\t\tfor (size_t _i = 0; _i < (" << p->name << ").size(); ++_i) {\n";
					out << "\t\t\tif (_i > 0) query_params += \"&\";\n";
					out << "\t\t\tquery_params += \"" << p->name << "=\" + (" << p->name << ")[_i];\n";
					out << "\t\t}\n";
				} else if (is_vector) {
					out << "\t\tfor (size_t _i = 0; _i < (" << p->name << ").size(); ++_i) {\n";
					out << "\t\t\tif (_i > 0) query_params += \"&\";\n";
					out << "\t\t\tquery_params += \"" << p->name << "=\" + query_value((" << p->name << ")[_i]);\n";
					out << "\t\t}\n";
				} else if (is_string) {
					out << "\t\tif (!query_params.empty()) query_params += \"&\";\n";
					out << "\t\tquery_params += \"" << p->name << "=\" + (" << p->name << ");\n";
				} else {
					out << "\t\tif (!query_params.empty()) query_params += \"&\";\n";
					out << "\t\tquery_params += \"" << p->name << "=\" + query_value(" << p->name << ");\n";
				}
			} else {
				out << "\t\tif (" << p->name << ".has_value()) {\n";
				if (is_vector_string) {
					out << "\t\t\tfor (size_t _i = 0; _i < (*(" << p->name << ")).size(); ++_i) {\n";
					out << "\t\t\t\tif (_i > 0) query_params += \"&\";\n";
					out << "\t\t\t\tquery_params += \"" << p->name << "=\" + (*(" << p->name << "))[_i];\n";
					out << "\t\t\t}\n";
				} else if (is_vector) {
					out << "\t\t\tfor (size_t _i = 0; _i < (*(" << p->name << ")).size(); ++_i) {\n";
					out << "\t\t\t\tif (_i > 0) query_params += \"&\";\n";
					out << "\t\t\t\tquery_params += \"" << p->name << "=\" + query_value((*(" << p->name
						<< "))[_i]);\n";
					out << "\t\t\t}\n";
				} else if (is_string) {
					out << "\t\t\tif (!query_params.empty()) query_params += \"&\";\n";
					out << "\t\t\tquery_params += \"" << p->name << "=\" + (*(" << p->name << "));\n";
				} else {
					out << "\t\t\tif (!query_params.empty()) query_params += \"&\";\n";
					out << "\t\t\tquery_params += \"" << p->name << "=\" + query_value(*(" << p->name << "));\n";
				}
				out << "\t\t}\n";
			}
		}
	} else {
		out << "\t\treq.target(path);\n";
	}

	std::string verb = ep.method == "delete" ? "delete_" : ep.method;
	out << "\t\treq.method(::boost::beast::http::verb::" << verb << ");\n";

	// Header parameters
	for (const auto& p : ep.params) {
		if (p.location == "header") {
			out << "\t\treq.set(\"" << p.name << "\", " << p.name << ");\n";
		}
	}

	out << "\t\treturn this->async_submit_request(std::move(req), token);\n";
}
void ClientGenerator::generateClientHpp(std::ostream& out) {
	out << "#pragma once\n";
	out << "#include <boost/asio.hpp>\n";
	out << "#include <boost/asio/ip/tcp.hpp>\n";
	out << "#include <boost/beast/core.hpp>\n";
	out << "#include <boost/beast/http.hpp>\n";
	out << "#include <boost/json.hpp>\n";
	out << "#include <memory>\n";
	out << "#include <optional>\n";
	out << "#include <string>\n";
	out << "#include <string_view>\n";
	out << "\n";

	std::string ns = "openapi";
	out << "#include \"" << ns << "_defs.hpp\"\n";
	out << "#include <siesta/beast/client.hpp>\n";
	out << "\n";
	out << "namespace " << ns << " {\n";

	// Helper to convert any value to query string parameter
	out << "inline std::string query_value(const auto& val) {\n";
	out << "    return boost::json::value_to<std::string>(boost::json::value_from(val));\n";
	out << "}\n\n";

	emitClassHeader(out);

	for (const auto& ep : endpoints_) {
		emitEndpoint(out, ep);
	}

	out << "}; // class Client\n";
	out << "\n";
	out << "} // namespace " << ns << "\n";
}

} // namespace codegen
