// SPDX-License-Identifier: Apache-2.0
#include "codegen_client.hpp"
#include "openapi.hpp"
#include "openapi3.hpp"
#include <fstream>
#include <unordered_set>

namespace codegen {

void ClientGenerator::operator()(const CodegenArgs& args, const std::filesystem::path& output_dir) {
	if (!args.spec || !args.endpoints || args.endpoints->empty()) {
		return;
	}

	const auto& endpoints = *args.endpoints;

	// Determine class-wide auth config from the first secured endpoint
	for (const auto& ep : endpoints) {
		if (ep.auth_type != AuthType::None) {
			auth_type_ = ep.auth_type;
			if (auth_type_ == AuthType::ApiKey) {
				auth_member_name_ = "_api_key";
				auth_param_name_ = "api_key";
			} else if (auth_type_ == AuthType::HttpBearer) {
				auth_member_name_ = "_bearer_token";
				auth_param_name_ = "bearer_token";
			}
			break;
		}
	}

	std::filesystem::create_directories(output_dir);
	auto client_path = output_dir / "client.hpp";
	std::ofstream out(client_path);
	if (out) {
		generateClientHpp(out, endpoints);
	}
}

void ClientGenerator::emitClassHeader(std::ostream& out) {
	out << "\n";
	out << "class Client : public ::siesta::beast::ClientBase {\n";
	if (auth_type_ != AuthType::None) {
		out << "\tstd::string " << auth_member_name_ << ";\n";
	}
	out << "public:\n";
	if (auth_type_ != AuthType::None) {
		out << "\tClient(::boost::asio::io_context& ctx, std::string " << auth_param_name_
			<< ", Config conf = Config())\n";
		out << "\t\t: ClientBase(ctx, conf)\n";
		out << "\t\t, " << auth_member_name_ << "(std::move(" << auth_param_name_ << ")) {}\n";
	} else {
		out << "\tusing ::siesta::beast::ClientBase::ClientBase;\n";
	}
	out << "\tusing ::siesta::beast::ClientBase::Config;\n";
	out << "\tusing ::siesta::beast::ClientBase::shared_from_this;\n";
	out << "\n";
}

void ClientGenerator::emitEndpoint(std::ostream& out, const Endpoint& ep) {
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

void ClientGenerator::emitMethodSignature(std::ostream& out, const Endpoint& ep) {
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

void ClientGenerator::emitMethodBody(std::ostream& out, const Endpoint& ep) {
	out << "\t\tconstexpr std::string_view path = \"" << escapeCppString(ep.path_template) << "\";\n";
	out << "\t\trequest_type req;\n";

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
		std::vector<const ClientParam*> required_query;
		std::vector<const ClientParam*> optional_query;
		for (const auto* p : query_params) {
			if (p->required) {
				required_query.push_back(p);
			} else {
				optional_query.push_back(p);
			}
		}

		if (!required_query.empty() || !optional_query.empty()) {
			out << "\t\tbool target_has_query = false;\n";
		}
		for (const auto* p : required_query) {
			bool is_vector = p->cpp_type.find("vector") != std::string::npos;
			bool is_string = p->cpp_type.find("string") != std::string::npos;
			if (is_vector) {
				if (is_string) {
					out << "\t\tfor (size_t _i = 0; _i < (" << p->name << ").size(); ++_i) {\n";
					out << "\t\t\tif (_i > 0 || target_has_query) target_path += \"&\";\n";
					out << "\t\t\telse target_path += \"?\"; target_has_query = true;\n";
					out << "\t\t\ttarget_path += \"" << p->wire_name << "=\" + url_encode((" << p->name << ")[_i]);\n";
					out << "\t\t}\n";
				} else {
					out << "\t\tfor (size_t _i = 0; _i < (" << p->name << ").size(); ++_i) {\n";
					out << "\t\t\tif (_i > 0 || target_has_query) target_path += \"&\";\n";
					out << "\t\t\telse target_path += \"?\"; target_has_query = true;\n";
					out << "\t\t\ttarget_path += \"" << p->wire_name << "=\" + query_value((" << p->name << ")[_i]);\n";
					out << "\t\t}\n";
				}
			} else if (is_string) {
				out << "\t\tif (target_has_query) target_path += \"&\"; else target_path += \"?\"; target_has_query = true;\n";
				out << "\t\ttarget_path += \"" << p->wire_name << "=\" + url_encode(" << p->name << ");\n";
			} else {
				out << "\t\tif (target_has_query) target_path += \"&\"; else target_path += \"?\"; target_has_query = true;\n";
				out << "\t\ttarget_path += \"" << p->wire_name << "=\" + query_value(" << p->name << ");\n";
			}
		}

		if (!optional_query.empty()) {
			out << "\t\tstd::string query_params;\n";
			for (const auto* p : optional_query) {
				bool is_vector = p->cpp_type.find("vector") != std::string::npos;
				bool is_string = p->cpp_type.find("string") != std::string::npos;
				bool is_vector_string = is_vector && is_string;
				out << "\t\tif (" << p->name << ".has_value()) {\n";
				if (is_vector_string) {
					out << "\t\t\tfor (size_t _i = 0; _i < (*(" << p->name << ")).size(); ++_i) {\n";
					out << "\t\t\t\tif (_i > 0) query_params += \"&\";\n";
					out << "\t\t\t\tquery_params += \"" << p->wire_name << "=\" + url_encode((*(" << p->name << "))[_i]);\n";
					out << "\t\t\t}\n";
				} else if (is_vector) {
					out << "\t\t\tfor (size_t _i = 0; _i < (*(" << p->name << ")).size(); ++_i) {\n";
					out << "\t\t\t\tif (_i > 0) query_params += \"&\";\n";
					out << "\t\t\t\tquery_params += \"" << p->wire_name << "=\" + query_value((*(" << p->name
						<< "))[_i]);\n";
					out << "\t\t\t}\n";
				} else if (is_string) {
					out << "\t\t\tif (!query_params.empty()) query_params += \"&\";\n";
					out << "\t\t\tquery_params += \"" << p->wire_name << "=\" + url_encode(*(" << p->name << "));\n";
				} else {
					out << "\t\t\tif (!query_params.empty()) query_params += \"&\";\n";
					out << "\t\t\tquery_params += \"" << p->wire_name << "=\" + query_value(*(" << p->name << "));\n";
				}
				out << "\t\t}\n";
			}
			out << "\t\tif (!query_params.empty()) {\n";
			out << "\t\t\tif (target_has_query) target_path += \"&\" + query_params;\n";
			out << "\t\t\telse target_path += \"?\" + query_params;\n";
			out << "\t\t}\n";
		}
		out << "\t\treq.target(target_path);\n";
	} else {
		out << "\t\treq.target(path);\n";
	}

	if (ep.has_request_body) {
		out << "\t\treq.body() = boost::json::serialize(boost::json::value_from(body));\n";
		out << "\t\treq.set(::boost::beast::http::field::content_type, \"" << ep.body_content_type << "\");\n";
		out << "\t\treq.prepare_payload();\n";
	}

	std::string verb = ep.method == "delete" ? "delete_" : ep.method;
	out << "\t\treq.method(::boost::beast::http::verb::" << verb << ");\n";

	if (ep.auth_type == AuthType::ApiKey) {
		out << "\t\treq.set(\"" << ep.auth_header_name << "\", " << auth_member_name_ << ");\n";
	} else if (ep.auth_type == AuthType::HttpBearer) {
		out << "\t\treq.set(\"" << ep.auth_header_name << "\", \"Bearer \" + " << auth_member_name_ << ");\n";
	}

	for (const auto& p : ep.params) {
		if (p.location == "header") {
			if (p.required) {
				out << "\t\treq.set(\"" << p.wire_name << "\", " << p.name << ");\n";
			} else {
				out << "\t\tif (" << p.name << ".has_value()) req.set(\"" << p.wire_name << "\", *(" << p.name << "));\n";
			}
		}
	}

	out << "\t\treturn this->async_submit_request(std::move(req), token);\n";
}

void ClientGenerator::generateClientHpp(std::ostream& out, const std::vector<Endpoint>& endpoints) {
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

	out << "inline std::string query_value(const auto& val) {\n";
	out << "    return boost::json::value_to<std::string>(boost::json::value_from(val));\n";
	out << "}\n\n";

	emitClassHeader(out);

	for (const auto& ep : endpoints) {
		emitEndpoint(out, ep);
	}

	out << "}; // class Client\n";
	out << "\n";
	out << "} // namespace " << ns << "\n";
}

} // namespace codegen
