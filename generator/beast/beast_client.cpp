#include "beast.hpp"

namespace fs = std::filesystem;
using namespace std::literals;

namespace siesta::beast {

void write_client_multipart_body(std::ostream& out, const openapi::Operation& op, std::string& indent) {
	const auto& parameters = op.parameters();
	if (std::none_of(
			parameters.begin(), parameters.end(), [](const openapi::Parameter& p) { return p.in() == "formData"; })) {
		return;
	}
	out << indent << "_request.set(::boost::beast::http::field::content_type, \"multipart/form-data; boundary=multipart\");\n";
	out << indent << "_request.set(::boost::beast::http::field::body, \"--multipart\");\n";
	for (const auto& param : parameters) {
		if (param.in() != "formData") {
			continue;
		}
		out << indent << "_request.set(::boost::beast::http::field::content_disposition, \"form-data; name=\\\"" << param.name()
			<< "\\\"\");\n";
		out << indent << "_request.body().assign(string_cast(" << param.name() << "));\n";
		// Data goes here...
		out << indent << "// http::async_write(_stream, _request);\n";
	}
	out << indent << "_request.set(::boost::beast::http::field::body, \"--multipart--\");\n";
	out << indent << "// http::async_write(_stream, _request);\n";
}

void write_client_function_body(
	std::ostream& out,
	const openapi::OpenAPI2& file,
	std::string_view pathstr,
	const openapi::Path& path,
	std::string_view opstr,
	const openapi::Operation& op,
	std::string& indent) {

	// Compose a path pattern, set as constexpr.
	const auto params = op.parameters();
	const bool has_path_param =
		std::any_of(params.begin(), params.end(), [](const openapi::Parameter& p) { return p.in() == "path"; });
	const bool has_query_param =
		std::any_of(params.begin(), params.end(), [](const openapi::Parameter& p) { return p.in() == "query"; });
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
	out << indent << "constexpr std::string_view path = \"" << full_path << "\";\n";
	out << indent << "::boost::json::monotonic_resource json_rsc(_json_buffer.data(), _json_buffer.size());\n";
	out << indent << "_request = {};\n";
	if (has_path_param || has_query_param) {
		out << indent << "_request.target(fmt::format(path, ";
		for (const auto& p : params) {
			if (p.in() == "path") {
				out << p.name() << ", ";
			}
		}
		for (const auto& p : params) {
			if (p.in() == "query") {
				out << p.name() << ", ";
			}
		}
		out.seekp(-2, std::ios::end);
		out << "));\n";
	} else {
		out << indent << "_request.target(path);\n";
	}

	std::string operation = "::boost::beast::http::verb::"s + std::string(opstr);
	if (opstr == "delete") {
		operation.append("_");
	}
	out << indent << "_request.method(" << operation << ");\n";
	for (const auto& p : op.parameters()) {
		if (p.in() == "body") {
			const std::string_view bodyname = p.name().empty() ? "body"sv : p.name();
			out << indent << "_request.set(::boost::beast::http::field::content_type, \"application/json\");\n";
			out << indent << "_request.body().assign(::boost::json::serialize(::boost::json::value_from(" << bodyname
				<< ", &json_rsc)));\n";
		} else if (p.in() == "header") {
			out << indent << "_request.set(\"" << p.name() << "\", " << sanitize(p.name()) << ");\n";
		}
		// p.in() == query is handled by URL formatting
	}
	write_client_multipart_body(out, op, indent);
	out << indent << "return this->async_submit_request(token);\n";

	for (const auto& [respstr, resp] : op.responses()) {
		out << indent << "//" << respstr << '\t';
		if (auto schema = resp.schema(); schema) {
			out << ' ' << (resp.schema().IsReference() ? resp.schema().reference() : resp.schema().type()) << '\n';
		} else {
			out << " nothing\n";
		}
		for (const auto& [hdrstr, hdr] : resp.headers()) {
			out << indent << "//" << hdrstr << '\n';
		}
	}
}

void write_method_bodies(std::ostream& out, const openapi::OpenAPI2& file) {
	std::string indent;
	for (const auto& [pathstr, path] : file.paths()) {
		for (const auto& [opstr, op] : path.operations()) {
			write_client_function_body(out, file, pathstr, path, opstr, op, indent);
		}
	}
}

void write_query_parameters(
	std::ostream& out,
	std::string_view pathstr,
	const openapi::Path& path,
	std::string_view opstr,
	const openapi::Operation& op) {
	auto verb = openapi::RequestMethodFromString(opstr);
	for (const auto& param : op.parameters()) {
		auto sanitized_paramname = sanitize(param.name());
		if (auto schema = param.schema(); schema) {
			if (schema.IsReference()) {
				out << sanitize(schema.reference()) << ' ' << sanitized_paramname << ", ";
			} else {
				// TODO
				const auto paramname = openapi::SynthesizeFunctionName(pathstr, verb) + std::string(param.name());
				out << "const " << paramname << "& " << sanitized_paramname << ", ";
			}
		} else {
			out << openapi::JsonTypeToCppType(param.type()) << ' ' << sanitized_paramname << ", ";
		}
	}

	out << "::boost::asio::completion_token_for<void(outcome_type)> auto&& token";
}

void write_method_declaration(
	std::ostream& out,
	std::string_view pathstr,
	const openapi::Path& path,
	std::string_view opstr,
	const openapi::Operation& op,
	std::string& indent) {
	write_multiline_comment(out, op.description(), indent);
	out << indent << "auto "
		<< (op.operationId().empty() ? openapi::SynthesizeFunctionName(pathstr, openapi::RequestMethodFromString(opstr))
									 : op.operationId())
		<< '(';
	write_query_parameters(out, pathstr, path, opstr, op);
	out << ')';
	out << std::endl;
}

void write_method_declarations(std::ostream& out, const openapi::OpenAPI2& file, std::string& indent) {
	for (const auto& [pathstr, path] : file.paths()) {
		for (const auto& [opstr, op] : path.operations()) {
			write_method_declaration(out, pathstr, path, opstr, op, indent);

			out << indent << "{\n";
			indent.push_back('\t');
			write_client_function_body(out, file, pathstr, path, opstr, op, indent);
			indent.pop_back();
			out << indent << "}\n";
		}
	}
}

void beast_client_hpp(const fs::path& input, const fs::path& output, const openapi::OpenAPI2& file) {
	auto out = std::ofstream(output / (input.stem().string() + "_client.hpp"));
	out << "#pragma once\n"
		<< "#include <boost/asio.hpp>\n"
		<< "#include <boost/asio/ip/tcp.hpp>\n"
		<< "#include <boost/beast/core.hpp>\n"
		<< "#include <boost/beast/http.hpp>\n"
		<< "#include <boost/json.hpp>\n"
		<< "#include <fmt/format.h>\n"
		<< "#include <functional>\n"
		<< "#include <memory>\n"
		<< "#include <string>\n"
		<< "#include <string_view>\n"
		<< '\n'
		<< "#include \"" << input.stem().string() << "_defs.hpp\"\n"
		<< "#include <siesta/beast/client.hpp>\n"
		<< '\n'
		<< "namespace swagger {\n"
		<< std::endl;
	out << "class Client : public ::siesta::beast::ClientBase {\n"
		<< "public:\n";

	std::string indent;
	indent.push_back('\t');
	out << indent << "using ::siesta::beast::ClientBase::ClientBase;\n"
		<< indent << "using ::siesta::beast::ClientBase::Config;\n"
		<< indent << "using ::siesta::beast::ClientBase::shared_from_this;\n"
		<< '\n';

	write_method_declarations(out, file, indent);

	indent.pop_back();
	out << "}; // class\n";
	//write_method_bodies(out, file);
	out << "} // namespace swagger\n";
}

//
// NO LONGER IN USE
//
void beast_client_cpp(const fs::path& input, const fs::path& output, const openapi::OpenAPI2& file) {
	const auto header_path = output / (input.stem().string() + "_client.hpp");
	auto out = std::ofstream(output / (input.stem().string() + "_client.cpp"));
	out << "#include <boost/json.hpp>\n"
		<< "#include <fmt/format.h>\n"
		<< '\n'
		<< "#include \"" << header_path.filename().string() << "\"\n"
		<< '\n'
		<< "namespace http = ::boost::beast::http;\n"
		<< "namespace json = ::boost::json;\n"
		<< "using namespace std::literals;\n"
		<< "using request = ::boost::beast::http::request<::boost::beast::http::string_body>;\n"
		<< "using response = ::boost::beast::http::response<::boost::beast::http::string_body>;\n"
		<< '\n'
		<< "namespace swagger {\n"
		<< std::endl;

	std::string indent;
	for (const auto& [pathstr, path] : file.paths()) {
		for (const auto& [opstr, op] : path.operations()) {
			const std::string functionName =
				(op.operationId().empty()
					 ? openapi::SynthesizeFunctionName(pathstr, openapi::RequestMethodFromString(opstr))
					 : std::string(op.operationId()));
			out << "void Client::" << std::move(functionName) << "(";
			write_query_parameters(out, pathstr, path, opstr, op);
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

} // namespace siesta::beast