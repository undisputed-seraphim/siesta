#include <filesystem>
#include <fstream>

#include "openapi2.hpp"
#include "util.hpp"

namespace fs = std::filesystem;
using namespace std::literals;

// TODO: https://github.com/nghttp2/nghttp2/pull/915
// ^ Implement path matches

namespace {

void write_query_details(std::ostream& os, const openapi::Operation& op, std::string& indent) {
	for (const auto& p : op.parameters()) {
		if (p.in() == "body") {
			if (auto schema = p.schema(); schema) {
				os << indent << "// \\param[in] " << (schema.IsReference() ? schema.reference() : schema.type()) << " (body) ";
			}
		} else {
			os << indent << "// \\param[in] " << p.name() << " (" << p.in() << ") ";
		}
		os << '\n';
	}
	for (const auto& [code, r] : op.responses()) {
		os << indent << "// \\return ";
		if (auto schema = r.schema(); schema) {
			os << (schema.IsReference() ? schema.reference() : schema.type()) << ' ' << code << '\n';
		} else {
			os << code << '\n';
		}
	}
}

void write_helper_functions(std::ostream& os) {
	os << "void decompose_http_query(std::string_view raw, std::function<void(std::string_view, std::string_view)>&& kv_cb) {\n"
		  "\tdo {\n"
		  "\tconst size_t q_split = raw.find_first_of('&');\n"
		  "\tauto kv = raw.substr(0, q_split);\n"
		  "\tconst size_t kv_split = kv.find_first_of('=');\n"
		  "\tkv_cb(kv.substr(0, kv_split), kv.substr(kv_split + 1, std::string_view::npos));\n"
		  "\traw.remove_prefix(q_split == std::string_view::npos ? raw.size() : (q_split + 1));\n"
		  "\t} while (!raw.empty());\n"
		  "}\n"
		  "\n";
	os << "template <typename T>\n"
		  "T lexical_cast(std::string_view s) { T v; auto ec = std::from_chars(s.data(), s.data() + s.size(), v); return v; }\n";
	os << "template <>\n"
		  "std::string lexical_cast<std::string>(std::string_view s) { return std::string(s); }\n";
	os << '\n';
}

void write_query_parser(std::ostream& os, const openapi::Operation::Parameters& parameters, std::string& indent) {
	if (std::none_of(parameters.begin(), parameters.end(), [](const openapi::Parameter& p) { return p.in() == "query"; }))
		return;
	for (const auto& p : parameters) {
		if (p.in() != "query")
			continue;
		write_multiline_comment(os, p.description(), indent);
		//p.Print(os, p.name(), indent); // TODO
	}
	os << indent << "decompose_http_query(req.uri().raw_query, [&](std::string_view k, std::string_view v) {\n";
	indent.push_back('\t');
	for (const auto& p : parameters) {
		if (p.in() != "query")
			continue;
		os << indent << "if (k == \"" << p.name() << "\") { " << p.name() << " = lexical_cast(v); return; }\n";
	}
	indent.pop_back();
	os << indent << "});\n";
}

void write_query_validator(std::ostream& os, const openapi::Operation::Parameters& parameters, const std::string& indent) {
	if (std::none_of(parameters.begin(), parameters.end(), [](const openapi::Parameter& p) { return p.in() == "query"; }))
		return;
	if (!std::any_of(parameters.begin(), parameters.end(), [](const openapi::Parameter& p) { return p.required(); }))
		return;
	os << indent << "bool valid =";
	for (const auto& p : parameters) {
		if (p.in() != "query")
			continue;
		if (!p.required())
			continue;
		os << " !" << p.name() << ".empty() &&";
	}
	os.seekp(-3, std::ios::end);
	os << ";\n";
}

void write_body_parser(std::ostream& os, const openapi::Operation::Parameters& parameters, const std::string& indent) {
	for (const auto& p : parameters) {
		if (p.in() != "body")
			continue;
		write_multiline_comment(os, p.description(), indent);
		os << indent << p.schema().reference() << " obj;\n";
		os << indent << "req.on_data([&obj](const uint8_t* bytes, std::size_t size) -> void {\n";
		os << indent << "\t// TODO: Fill in obj here...\n";
		os << indent << "});\n";
	}
}

void write_response_handlers(std::ostream& os, const openapi::Operation::Responses& responses, std::string& indent) {
	for (const auto& [code, response] : responses) {
		write_multiline_comment(os, response.description(), indent);
		if (code == "default") {
			os << indent << "res.write_head(200); // Default\n";
			os << indent << "res.end();\n";
		} else {
			os << indent << "res.write_head(" << code << ");\n";
			os << indent << "res.end();\n";
		}
	}
}

void nghttps_server_hpp(const fs::path& input, const fs::path& output, openapi::OpenAPI2& file) {
	auto out = std::ofstream(output / (input.stem().string() + "_server.hpp"));
	out << "// DO NOT EDIT. Automatically generated from " << input.filename().string() << '\n';

	out << "#pragma once\n"
		<< "#include <nghttp2/nghttp2.h>\n"
		<< "#include <nghttp2/asio_http2.h>\n"
		<< "#include <nghttp2/asio_http2_server.h>\n"
		<< '\n'
		<< "using namespace std::literals;\n"
		<< "using Request = nghttp2::asio_http2::server::request;\n"
		<< "using Response = nghttp2::asio_http2::server::response;\n"
		<< '\n'
		<< "// This file contains function prototypes for each path/requestmethod pair.\n"
		<< "// Implement the function bodies for each prototype here.\n"
		<< std::endl;

	out << "class Server {\n"
		<< "public:\n";
	std::string indent;
	indent.push_back('\t');
	for (const auto& [pathstr, path] : file.paths()) {
		for (const auto& [opstr, op] : path.operations()) {
			write_multiline_comment(out, op.description(), indent);
			write_query_details(out, op, indent);
			out << indent << "virtual void " << op.operationId() << "(const Request& req, const Response& res) = 0;\n\n";
		}
	}
	out << indent << "void add_routes(nghttp2::asio_http2::server::http2& server);\n";
	indent.pop_back();
	out << "}; // Server\n";
}

void nghttp2_server_cpp(const fs::path& input, const fs::path& output, openapi::OpenAPI2& file) {
	auto out = std::ofstream(output / (input.stem().string() + "_server.cpp"));
	out << "// DO NOT EDIT. Automatically generated from " << input.filename().string() << '\n';

	out << "#include <charconv>\n"
		<< "#include <string_view>\n"
		<< "#include \"" << (input.stem().string() + "_paths.hpp") << "\"\n"
		<< "#include \"" << (input.stem().string() + "_defs.hpp") << "\"\n"
		<< '\n';
	write_helper_functions(out);

	out << "namespace _impl {\n\n";

	std::string indent;
	indent.push_back('\t');
	for (const auto& [pathstr, path] : file.paths()) {
		for (const auto& [opstr, op] : path.operations()) {
			out << "void " << op.operationId() << "(Server& server, const Request& req, const Response& res) {\n";
			write_query_parser(out, op.parameters(), indent);
			write_query_validator(out, op.parameters(), indent);
			write_body_parser(out, op.parameters(), indent);
			// out << indent << "int code = " << op.operationId() << "();\n";
			write_response_handlers(out, op.responses(), indent);
			out << "}\n\n";
		}
	}
	indent.pop_back();

	out << "} // namespace _impl\n\n";

	out << "void Server::add_routes(nghttp2::asio_http2::server::http2& server) {\n";
	indent.push_back('\t');
	for (const auto& [pathstr, path] : file.paths()) {
		out << indent << "server.handle(\"" << pathstr << "\", [this](const Request& req, const Response& res) {\n";
		indent.push_back('\t');
		for (const auto& [opstr, op] : path.operations()) {
			out << indent << "if (req.method() == \"" << opstr << "\") { return ::_impl::" << op.operationId() << "(*this, req, res); }\n";
		}
		out << indent << "res.write_head(405);\n";
		indent.pop_back();
		out << indent << "});\n";
	}
	indent.pop_back();
	out << "}\n" << std::endl;
}

} // namespace

// Writes header and impl files for nghttp2.
void nghttp2(const fs::path& input, const fs::path& output, openapi::OpenAPI2& file) {
	::nghttps_server_hpp(input, output, file);
	::nghttp2_server_cpp(input, output, file);
}
