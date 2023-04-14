#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>

#include "openapi2.hpp"
#include "util.hpp"

namespace fs = std::filesystem;
using namespace std::literals;

namespace {

std::string clean_path_string(std::string_view original) {
	std::string copy;
	copy.reserve(original.size());
	bool is_braced = false;
	bool is_colon = false;
	for (const char c : original) {
		if (c == '{') {
			is_braced = true;
		}
		if (c == '}') {
			is_braced = false;
		}
		if (c == ':' && !is_colon) {
			copy.push_back('{');
			is_colon = true;
			is_braced = true;
			continue;
		}
		if (c == '/' && is_colon) {
			copy.push_back('}');
			is_colon = false;
			is_braced = false;
		}
		if ((!is_braced || c == '{') && (!is_colon)) {
			copy.push_back(c);
		}
	}
	if (is_colon) {
		copy.push_back('}');
	}
	return copy;
}

void write_client_multipart_body(std::ostream& os, const openapi::Operation& op, std::string& indent) {
	const auto& parameters = op.parameters();
	if (std::none_of(parameters.begin(), parameters.end(), [](const openapi::Parameter& p) { return p.in() == "formData"; })) {
		return;
	}
	os << indent << "req.set(http::field::content_type, \"multipart/form-data; boundary=multipart\");\n";
	os << indent << "req.set(http::field::body, \"--multipart\");\n";
	for (const auto& param : parameters) {
		if (param.in() != "formData") {
			continue;
		}
		os << indent << "req.set(http::field::content_disposition, \"form-data; name=\\\"" << param.name() << "\\\"\");\n";
		os << indent << "req.body().assign(string_cast(" << param.name() << "));\n";
		// Data goes here...
		os << indent << "// http::async_write(_stream, req);\n";
	}
	os << indent << "req.set(http::field::body, \"--multipart--\");\n";
	os << indent << "// http::async_write(_stream, req);\n";
}

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
}

void beast_server_hpp(const fs::path& input, const fs::path& output, const openapi::OpenAPI2& file) {
	auto out = std::ofstream(output / (input.stem().string() + "_server.hpp"));
	out << "#pragma once\n"
		<< "#include <boost/beast/core.hpp>\n"
		<< "#include <boost/beast/http.hpp>\n"
		<< "#include <boost/asio.hpp>\n"
		<< "#include <boost/asio/ip/tcp.hpp>\n"
		<< "#include <functional>\n"
		<< "#include <memory>\n"
		<< "#include <string>\n"
		<< "#include <string_view>\n"
		<< '\n'
		<< "namespace beast = boost::beast;\n"
		<< "namespace ip    = boost::asio::ip;\n"
		<< '\n'
		<< "namespace swagger {\n"
		<< std::endl;

	std::string indent;
	indent.push_back('\t');
	out << "class Server {\n";
	for (const auto& [pathstr, path] : file.paths()) {
		for (const auto& [opstr, op] : path.operations()) {
			write_multiline_comment(out, op.description(), indent);
			write_query_details(out, op, indent);
			out << indent << "virtual void "
				<< (op.operationId().empty() ? openapi::SynthesizeFunctionName(pathstr, openapi::RequestMethodFromString(opstr)) : op.operationId())
				<< "() = 0;\n\n";
		}
	}
	indent.pop_back();
	out << "}; // class\n";
	out << "} // namespace swagger\n";
}

void beast_server_cpp(const fs::path& input, const fs::path& output, const openapi::OpenAPI2& file) {
	auto out = std::ofstream(output / (input.stem().string() + "_server.cpp"));
}

void write_query_parameters(
	std::ostream& os,
	std::string_view pathstr,
	const openapi::Path& path,
	std::string_view opstr,
	const openapi::Operation& op,
	std::string& /*indent*/) {
	if (op.parameters().empty()) {
		return;
	}
	auto verb = openapi::RequestMethodFromString(opstr);
	for (const auto& param : op.parameters()) {
		auto sanitized_paramname = sanitize(param.name());
		if (auto schema = param.schema(); schema) {
			if (schema.IsReference()) {
				os << sanitize(schema.reference()) << ' ' << sanitized_paramname << ", ";
			} else {
				// TODO
				const auto paramname = openapi::SynthesizeFunctionName(pathstr, verb) + std::string(param.name());
				os << "const " << paramname << "& " << sanitized_paramname << ", ";
			}
		} else {
			os << openapi::JsonTypeToCppType(param.type()) << ' ' << sanitized_paramname << ", ";
		}
	}
	os.seekp(-2, std::ios::end);
}

void write_client_function_body(
	std::ostream& os,
	const openapi::OpenAPI2& file,
	std::string_view pathstr,
	const openapi::Path& path,
	std::string_view opstr,
	const openapi::Operation& op,
	std::string& indent) {

	// Compose a path pattern, set as constexpr.
	const auto params = op.parameters();
	const bool has_path_param = std::any_of(params.begin(), params.end(), [](const openapi::Parameter& p) { return p.in() == "path"; });
	const bool has_query_param = std::any_of(params.begin(), params.end(), [](const openapi::Parameter& p) { return p.in() == "query"; });
	std::string full_path = std::string(file.basePath());
	if (has_path_param) {
		full_path += clean_path_string(pathstr);
	} else {
		full_path += std::string(pathstr);
	}
	if (has_query_param) {
		full_path.push_back('?');
		for (const auto& p : params) {
			if (p.in() == "query") {
				full_path += std::string(p.name()) + "={},";
			}
		}
		full_path.pop_back();
	}
	os << indent << "constexpr std::string_view path = \"" << full_path << "\";\n\n";
	os << indent << "::boost::json::monotonic_resource json_rsc(_json_buffer);\n\n";
	os << indent << "request req;\n";
	if (has_path_param || has_query_param) {
		os << indent << "req.target(fmt::format(path, ";
		for (const auto& p : params) {
			if (p.in() == "path") {
				os << p.name() << ", ";
			}
		}
		for (const auto& p : params) {
			if (p.in() == "query") {
				os << p.name() << ", ";
			}
		}
		os.seekp(-2, std::ios::end);
		os << "));\n";
	} else {
		os << indent << "req.target(path);\n";
	}

	std::string operation = "http::verb::"s + std::string(opstr);
	if (opstr == "delete") {
		operation.append("_");
	}
	os << indent << "req.method(" << operation << ");\n";
	for (const auto& p : op.parameters()) {
		if (p.in() == "body") {
			const std::string_view bodyname = p.name().empty() ? "body"sv : p.name();
			os << indent << "req.set(http::field::content_type, \"application/json\");\n";
			os << indent << "req.body().assign(json::serialize(json::value_from(" << bodyname << ", &json_rsc)));\n";
		} else if (p.in() == "header") {
			os << indent << "req.set(\"" << p.name() << "\", " << sanitize(p.name()) << ");\n";
		}
	}
	write_client_multipart_body(os, op, indent);
	os << indent << "http::async_write(_stream, req, std::bind_front(&Client::on_write, this));\n";

	for (const auto& [respstr, resp] : op.responses()) {
		os << indent << "//" << respstr << '\t';
		if (auto schema = resp.schema(); schema) {
			os << ' ' << (resp.schema().IsReference() ? resp.schema().reference() : resp.schema().type()) << '\n';
		} else {
			os << " nothing\n";
		}
		for (const auto& [hdrstr, hdr] : resp.headers()) {
			os << indent << "//" << hdrstr << '\n';
		}
	}
}

void beast_client_hpp(const fs::path& input, const fs::path& output, const openapi::OpenAPI2& file) {
	auto out = std::ofstream(output / (input.stem().string() + "_client.hpp"));
	out << "#pragma once\n"
		<< "#include <boost/beast/core.hpp>\n"
		<< "#include <boost/beast/http.hpp>\n"
		<< "#include <boost/asio.hpp>\n"
		<< "#include <boost/asio/ip/tcp.hpp>\n"
		<< "#include <functional>\n"
		<< "#include <memory>\n"
		<< "#include <string>\n"
		<< "#include <string_view>\n"
		<< '\n'
		<< "#include <siesta/beast/client.hpp>\n"
		<< "#include \"" << input.stem().string() << "_defs.hpp\"\n"
		<< '\n'
		<< "namespace swagger {\n"
		<< std::endl;
	out << "class Client : public ::siesta::ClientBase {\n"
		<< "public:\n";

	std::string indent;
	indent.push_back('\t');
	out << indent << "using ::siesta::ClientBase::Config;\n"
		<< indent << "using ::siesta::ClientBase::ClientBase;\n"
		<< '\n';

	for (const auto& [pathstr, path] : file.paths()) {
		for (const auto& [opstr, op] : path.operations()) {
			write_multiline_comment(out, op.description(), indent);
			out << indent << "void "
				<< (op.operationId().empty() ? openapi::SynthesizeFunctionName(pathstr, openapi::RequestMethodFromString(opstr)) : op.operationId()) << '(';
			write_query_parameters(out, pathstr, path, opstr, op, indent);
			out << ");\n";
			out << std::endl;
		}
	}

	indent.pop_back();
	out << "}; // class\n";
	out << "} // namespace swagger\n";
}

void beast_client_cpp(const fs::path& input, const fs::path& output, const openapi::OpenAPI2& file) {
	const auto header_path = output / (input.stem().string() + "_client.hpp");
	auto out = std::ofstream(output / (input.stem().string() + "_client.cpp"));
	out << "#include <boost/json.hpp>\n"
		<< "#include <fmt/format.h>\n"
		<< '\n'
		<< "#include \"" << header_path.filename().string() << "\"\n"
		<< '\n'
		<< "namespace asio = ::boost::asio;\n"
		<< "namespace http = ::boost::beast::http;\n"
		<< "namespace json = ::boost::json;\n"
		<< "using namespace std::literals;\n"
		<< "using request  = ::boost::beast::http::request<::boost::beast::http::string_body>;\n"
		<< "using response = ::boost::beast::http::response<::boost::beast::http::string_body>;\n"
		<< '\n'
		<< "namespace swagger {\n"
		<< std::endl;

	std::string indent;
	for (const auto& [pathstr, path] : file.paths()) {
		for (const auto& [opstr, op] : path.operations()) {
			out << "void Client::"
				<< (op.operationId().empty() ? openapi::SynthesizeFunctionName(pathstr, openapi::RequestMethodFromString(opstr)) : op.operationId()) << "(";
			write_query_parameters(out, pathstr, path, opstr, op, indent);
			out << ") {\n";
			indent.push_back('\t');
			write_client_function_body(out, file, pathstr, path, opstr, op, indent);
			indent.pop_back();
			out << "}\n";
			out << std::endl;
		}
	}
	out << "} // namespace swagger\n";
}

} // namespace

void beast(const fs::path& input, const fs::path& output, const openapi::OpenAPI2& file) {
	::beast_server_hpp(input, output, file);
	::beast_server_cpp(input, output, file);

	::beast_client_hpp(input, output, file);
	::beast_client_cpp(input, output, file);
}
