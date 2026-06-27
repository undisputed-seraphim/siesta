// SPDX-License-Identifier: Apache-2.0
#include "Support/Filenames.hpp"
#include "Backend/Beast/BeastClientGen.hpp"
#include "Backend/Shared/MethodEmitter.hpp"
#include "Frontend/openapi.hpp"
#include "Frontend/openapi3.hpp"
#include <algorithm>
#include <fstream>
#include <unordered_set>

namespace codegen {

void BeastClientGenerator::operator()(const CodegenArgs& args, const std::filesystem::path& output_dir) {
	if (!args.endpoints || args.endpoints->empty()) {
		return;
	}

	const auto& endpoints = *args.endpoints;
	ns_ = args.ns;

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
		generateClientHpp(out, endpoints);
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

void BeastClientGenerator::emitWebSocketEndpoint(std::ostream& out, const Endpoint& ep) {
	std::string text = ep.description.empty() ? ep.summary : ep.description;
	if (!text.empty()) {
		write_multiline_comment(out, text, "\t");
	}

	auto streaming_note = [&]() -> std::string {
		switch (ep.streaming_mode) {
		case StreamingMode::ServerStream: return " server-streaming";
		case StreamingMode::ClientStream: return " client-streaming";
		case StreamingMode::Bidirectional: return " bidirectional";
		default: return "";
		}
	}();

	out << "\t// WebSocket" << streaming_note << "\n";

	// Signature: returns boost::asio::awaitable<outcome_type>
	out << "\t::boost::asio::awaitable<outcome_type> " << ep.function_name << "(";
	if (ep.has_request_body && !ep.body_type.empty()) {
		out << "const " << ep.body_type << "& body";
	}
	out << ")\n";
	out << "\t{\n";

	// Upgrade to WebSocket
	out << "\t\tauto ws = co_await this->upgrade_to_websocket(\n";
	out << "\t\t\t\"" << escapeCppString(ep.path) << "\"sv);\n";
	out << "\n";

	// Send initial request body if applicable
	if (ep.has_request_body && !ep.body_type.empty()) {
		out << "\t\t{\n";
		out << "\t\t\tauto sp = json_storage();\n";
		out << "\t\t\tauto jv = boost::json::value_from(body, sp);\n";
		out << "\t\t\tws.text(true);\n";
		out << "\t\t\tco_await ws.async_write(\n";
		out << "\t\t\t\t::boost::asio::buffer(boost::json::serialize(jv)));\n";
		out << "\t\t}\n";
	}

	// Read loop for server-streaming / bidirectional
	if (ep.streaming_mode == StreamingMode::ServerStream || ep.streaming_mode == StreamingMode::Bidirectional) {
		out << "\n\t\t::boost::beast::flat_buffer buf;\n";
		out << "\t\tfor (;;) {\n";
		out << "\t\t\tco_await ws.async_read(buf);\n";
		out << "\t\t\tif (ws.got_text()) {\n";
		out << "\t\t\t\tco_return outcome_type(boost::json::value_to<" << ep.body_type << ">(\n";
		out << "\t\t\t\t\tboost::json::parse(::boost::beast::buffers_to_string(buf.data()))));\n";
		out << "\t\t\t}\n";
		out << "\t\t\tbuf.clear();\n";
		out << "\t\t}\n";
	} else {
		// Client streaming: just close the WebSocket
		out << "\n\t\tco_await ws.async_close(::boost::beast::websocket::close_code::normal);\n";
	}

	out << "\t}\n";
	out << "\n";
}

void BeastClientGenerator::emitRequestBody(std::ostream& out, const Endpoint& ep) {
	if (!ep.has_request_body) return;
	out << "\t\tauto sp = json_storage();\n";
	out << "\t\treq.body() = boost::json::serialize(boost::json::value_from(body, sp));\n";
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

void BeastClientGenerator::generateClientHpp(std::ostream& out, const std::vector<Endpoint>& endpoints) {
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

	out << "#include \"" << filenames::DEFS_HPP << "\"\n";
	out << "#include <siesta/beast/client.hpp>\n";
	out << "\n";
	out << "namespace " << ns_ << " {\n";
	out << "using siesta::url_encode;\n";
	out << "using siesta::query_value;\n";
	out << "\n";

	emitClassHeader(out);

	for (const auto& ep : endpoints) {
		if (ep.is_websocket) continue; // TODO: enable after ClientBase gains upgrade_to_websocket
		emitEndpoint(out, ep);
	}

	out << "}; // class Client\n";
	out << "} // namespace " << ns_ << "\n";
}

} // namespace codegen
