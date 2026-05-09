// SPDX-License-Identifier: Apache-2.0
#include "beast/codegen_client.hpp"
#include "openapi.hpp"
#include "openapi3.hpp"
#include <fstream>
#include <unordered_set>

namespace codegen {

void BeastClientGenerator::operator()(const CodegenArgs& args, const std::filesystem::path& output_dir) {
	if (!args.spec || !args.endpoints || args.endpoints->empty()) {
		return;
	}

	const auto& endpoints = *args.endpoints;

	auto auth = detectAuth(endpoints);
	if (auth.type != AuthType::None) {
		auth_type_ = auth.type;
		auth_member_name_ = "_" + auth.name;
		auth_param_name_ = auth.name;
		if (auth.type == AuthType::HttpBearer) {
			auth_value_member_ = "_auth_header";
		}
	}

	std::filesystem::create_directories(output_dir);
	auto client_path = output_dir / filenames::CLIENT_HPP;
	std::ofstream out(client_path);
	if (out) {
		generateClientHpp(out, endpoints, args.ast);
	}
}

void BeastClientGenerator::emitClassHeader(std::ostream& out) {
	out << "\n";
	out << "class Client : public ::siesta::beast::ClientBase {\n";
	if (auth_type_ != AuthType::None) {
		out << "\tstd::string " << auth_member_name_ << ";\n";
		if (auth_type_ == AuthType::HttpBearer) {
			out << "\tstd::string " << auth_value_member_ << ";\n";
		}
	}
	out << "public:\n";
	if (auth_type_ != AuthType::None) {
		out << "\tClient(::boost::asio::io_context& ctx, std::string " << auth_param_name_
			<< ", Config conf = Config())\n";
		out << "\t\t: ClientBase(ctx, conf)\n";
		out << "\t\t, " << auth_member_name_ << "(std::move(" << auth_param_name_ << "))";
		if (auth_type_ == AuthType::HttpBearer) {
			out << "\n\t\t, " << auth_value_member_ << "(\"Bearer \" + " << auth_member_name_ << ")";
		}
		out << " {}\n";
	} else {
		out << "\tusing ::siesta::beast::ClientBase::ClientBase;\n";
	}
	out << "\tusing ::siesta::beast::ClientBase::Config;\n";
	out << "\tusing ::siesta::beast::ClientBase::shared_from_this;\n";
	out << "\n";
}

void BeastClientGenerator::emitEndpoint(std::ostream& out, const Endpoint& ep) {
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

void BeastClientGenerator::emitMethodSignature(std::ostream& out, const Endpoint& ep) {
	out << "\tauto " << ep.function_name << "(";

	bool has_previous = false;

	if (ep.has_request_body) {
		out << "const " << ep.body_type << "& body";
		has_previous = true;
	}

	for (size_t i = 0; i < ep.params.size(); ++i) {
		if (has_previous) {
			out << ", ";
		}
		const auto& p = ep.params[i];
		if (!p.required) {
			out << "std::optional<" << p.cpp_type << "> ";
		} else {
			out << p.cpp_type << " ";
		}
		out << p.name;
		has_previous = true;
	}

	if (has_previous) {
		out << ", ";
	}
	out << "::boost::asio::completion_token_for<void(outcome_type)> auto&& token";
	out << ")";
}

void BeastClientGenerator::emitPathParams(std::ostream& out, const std::vector<const ClientParam*>& path_params) {
	for (const auto* p : path_params) {
		bool is_string = p->is_string_type;
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
				out << "\t\t\tif (auto _p = target_path.find(\"{}\"); _p != std::string::npos) target_path.replace(_p, 2, *"
					<< p->name << ");\n";
			} else {
				out << "\t\t\tif (auto _p = target_path.find(\"{}\"); _p != std::string::npos) target_path.replace(_p, 2, std::to_string(*"
					<< p->name << "));\n";
			}
			out << "\t\t}\n";
		}
	}
}

void BeastClientGenerator::emitQueryParams(std::ostream& out, const std::vector<const ClientParam*>& params) {
	for (const auto* p : params) {
		bool is_vector = p->is_vector_type;
		bool is_string = p->is_string_type;
		bool is_vector_string = p->is_vector_string_type;

		if (p->required) {
			if (is_vector_string) {
				out << "\t\tfor (size_t _i = 0; _i < " << p->name << ".size(); ++_i) {\n";
				out << "\t\t\t_sep(); query += \"" << p->wire_name << "=\" + url_encode(" << p->name << "[_i]);\n";
				out << "\t\t}\n";
			} else if (is_vector) {
				out << "\t\tfor (size_t _i = 0; _i < " << p->name << ".size(); ++_i) {\n";
				out << "\t\t\t_sep(); query += \"" << p->wire_name << "=\" + query_value(" << p->name << "[_i]);\n";
				out << "\t\t}\n";
			} else if (is_string) {
				out << "\t\t_sep(); query += \"" << p->wire_name << "=\" + url_encode(" << p->name << ");\n";
			} else {
				out << "\t\t_sep(); query += \"" << p->wire_name << "=\" + query_value(" << p->name << ");\n";
			}
		} else {
			out << "\t\tif (" << p->name << ".has_value()) {\n";
			if (is_vector_string) {
				out << "\t\t\tfor (size_t _i = 0; _i < " << p->name << "->size(); ++_i) {\n";
				out << "\t\t\t\t_sep(); query += \"" << p->wire_name << "=\" + url_encode((*" << p->name << ")[_i]);\n";
				out << "\t\t\t}\n";
			} else if (is_vector) {
				out << "\t\t\tfor (size_t _i = 0; _i < " << p->name << "->size(); ++_i) {\n";
				out << "\t\t\t\t_sep(); query += \"" << p->wire_name << "=\" + query_value((*" << p->name << ")[_i]);\n";
				out << "\t\t\t}\n";
			} else if (is_string) {
				out << "\t\t\t_sep(); query += \"" << p->wire_name << "=\" + url_encode(*" << p->name << ");\n";
			} else {
				out << "\t\t\t_sep(); query += \"" << p->wire_name << "=\" + query_value(*" << p->name << ");\n";
			}
			out << "\t\t}\n";
		}
	}
}

void BeastClientGenerator::emitRequestBody(std::ostream& out, const Endpoint& ep) {
	if (!ep.has_request_body) return;
	out << "\t\treq.body() = boost::json::serialize(boost::json::value_from(body));\n";
	out << "\t\treq.set(::boost::beast::http::field::content_type, \"" << ep.body_content_type << "\");\n";
	out << "\t\treq.prepare_payload();\n";
}

void BeastClientGenerator::emitHeaderParams(std::ostream& out, const std::vector<const ClientParam*>& header_params) {
	for (const auto* p : header_params) {
		if (p->required)
			out << "\t\treq.set(\"" << p->wire_name << "\", " << p->name << ");\n";
		else
			out << "\t\tif (" << p->name << ".has_value()) req.set(\"" << p->wire_name << "\", *" << p->name << ");\n";
	}
}

void BeastClientGenerator::emitMethodBody(std::ostream& out, const Endpoint& ep) {
	out << "\t\tconstexpr std::string_view path = \"" << escapeCppString(ep.path_template) << "\";\n";
	out << "\t\trequest_type req;\n";

	std::vector<const ClientParam*> path_params;
	std::vector<const ClientParam*> query_params;

	for (const auto& p : ep.params) {
		if (p.location == "path")       path_params.push_back(&p);
		else if (p.location == "query") query_params.push_back(&p);
	}

	bool has_path = !path_params.empty();
	bool has_query = !query_params.empty();
	bool all_query_required = has_query;
	for (const auto* q : query_params) {
		if (!q->required) { all_query_required = false; break; }
	}

	if (!has_path && !has_query) {
		out << "\t\treq.target(path);\n";
	} else if (!has_path && has_query) {
		out << "\t\tstd::string query;\n";
		out << "\t\tauto _sep = [&query]{ if (!query.empty()) query += '&'; };\n";
		emitQueryParams(out, query_params);
		if (all_query_required) {
			out << "\t\tstd::string target_path(path);\n";
			out << "\t\ttarget_path += '?';\n";
			out << "\t\ttarget_path += query;\n";
			out << "\t\treq.target(target_path);\n";
		} else {
			out << "\t\tif (!query.empty()) {\n";
			out << "\t\t\tstd::string target_path(path);\n";
			out << "\t\t\ttarget_path += '?';\n";
			out << "\t\t\ttarget_path += query;\n";
			out << "\t\t\treq.target(target_path);\n";
			out << "\t\t} else {\n";
			out << "\t\t\treq.target(path);\n";
			out << "\t\t}\n";
		}
	} else if (has_path && !has_query) {
		out << "\t\tstd::string target_path(path);\n";
		emitPathParams(out, path_params);
		out << "\t\treq.target(target_path);\n";
	} else {
		out << "\t\tstd::string target_path(path);\n";
		emitPathParams(out, path_params);
		out << "\t\tstd::string query;\n";
		out << "\t\tauto _sep = [&query]{ if (!query.empty()) query += '&'; };\n";
		emitQueryParams(out, query_params);
		out << "\t\tif (!query.empty()) { target_path += '?'; target_path += query; }\n";
		out << "\t\treq.target(target_path);\n";
	}

	emitRequestBody(out, ep);

	out << "\t\treq.method(::boost::beast::http::verb::" << ep.cpp_verb << ");\n";

	if (ep.auth_type == AuthType::ApiKey) {
		out << "\t\treq.set(\"" << ep.auth_header_name << "\", " << auth_member_name_ << ");\n";
	} else if (ep.auth_type == AuthType::HttpBearer) {
		out << "\t\treq.set(\"" << ep.auth_header_name << "\", " << auth_value_member_ << ");\n";
	}

	std::vector<const ClientParam*> header_params;
	for (const auto& p : ep.params) {
		if (p.location == "header") header_params.push_back(&p);
	}
	emitHeaderParams(out, header_params);

	out << "\t\treturn this->async_submit_request(std::move(req), token);\n";
}

void BeastClientGenerator::generateClientHpp(std::ostream& out, const std::vector<Endpoint>& endpoints,
                                               const schema::NormalizedAST& ast) {
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

	out << "inline std::string url_encode(std::string_view sv) {\n";
	out << "\tstd::string result;\n";
	out << "\tresult.reserve(sv.size() * 3);\n";
	out << "\tfor (unsigned char c : sv) {\n";
	out << "\t\tif ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {\n";
	out << "\t\t\tresult += c;\n";
	out << "\t\t} else {\n";
	out << "\t\t\tresult += '%';\n";
	out << "\t\t\tresult += \"0123456789ABCDEF\"[c >> 4];\n";
	out << "\t\t\tresult += \"0123456789ABCDEF\"[c & 15];\n";
	out << "\t\t}\n";
	out << "\t}\n";
	out << "\treturn result;\n";
	out << "}\n\n";

	out << "inline std::string query_value(int32_t v)  { return std::to_string(v); }\n";
	out << "inline std::string query_value(int64_t v)  { return std::to_string(v); }\n";
	out << "inline std::string query_value(uint32_t v) { return std::to_string(v); }\n";
	out << "inline std::string query_value(uint64_t v) { return std::to_string(v); }\n";
	out << "inline std::string query_value(double v)   { return std::to_string(v); }\n";
	out << "inline std::string query_value(float v)    { return std::to_string(v); }\n";
	out << "inline std::string query_value(bool v)     { return v ? \"true\" : \"false\"; }\n";
	out << "inline std::string query_value(const std::string& v) { return url_encode(v); }\n";

	// Enum overloads — zero-overhead, fail at compile time for unknown types
	for (const auto& [name, _] : ast.getTypes()) {
		const auto* type = ast.getType(name);
		if (!type) continue;
		std::visit(
			[&](const auto& t) {
				using T = std::decay_t<decltype(t)>;
				if constexpr (std::is_same_v<T, schema::EnumType>) {
					out << "inline std::string query_value(api::" << name << " val) {\n";
					out << "\tswitch (val) {\n";
					for (const auto& ev : t.values) {
						out << "\t\tcase api::" << name << "::" << sanitize_enum_identifier(ev.name)
						    << ": return \"" << escapeCppString(ev.value) << "\";\n";
					}
					out << "\t\tdefault: return \"\";\n";
					out << "\t}\n";
					out << "}\n";
				} else if constexpr (std::is_same_v<T, schema::PrimitiveType>) {
					if (!t.enum_values.empty()) {
						out << "inline std::string query_value(api::" << name << " val) {\n";
						out << "\tswitch (val) {\n";
						for (const auto& ev : t.enum_values) {
							out << "\t\tcase api::" << name << "::" << sanitize_enum_identifier(ev)
							    << ": return \"" << escapeCppString(ev) << "\";\n";
						}
						out << "\t\tdefault: return \"\";\n";
						out << "\t}\n";
						out << "}\n";
					}
				}
			},
			*type);
	}

	out << "\n";
	out << "// Catch-all: fires a readable static_assert when no overload matches.\n";
	out << "template <typename T>\n";
	out << "inline std::string query_value(const T&) {\n";
	out << "\tstatic_assert(sizeof(T) == 0, \"query_value: no overload for this type.\");\n";
	out << "\treturn {};\n";
	out << "}\n";

	emitClassHeader(out);

	for (const auto& ep : endpoints) {
		emitEndpoint(out, ep);
	}

	out << "}; // class Client\n";
	out << "} // namespace " << ns << "\n";
}

} // namespace codegen
