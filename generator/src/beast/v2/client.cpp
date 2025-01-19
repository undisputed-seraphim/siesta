// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <beast/beast.hpp>
#include <beast/v2/beastv2.hpp>

namespace fs = ::std::filesystem;
using namespace std::literals;

namespace siesta::beast::v2 {

void V2Printer::print_client(const fs::path& output_dir) {
	cli_hpp = output_dir / (name + "_client.hpp");
	cli_cpp = output_dir / (name + "_client.cpp");
	cli_hpp_ofs = std::ofstream(cli_hpp);
	// cli_cpp_ofs = std::ofstream(output_dir / (name + "_client.cpp"));
	print_client_header();
}

void V2Printer::print_client_header() {
	auto& out = cli_hpp_ofs;
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
		<< "#include \"" << name << "_defs.hpp\"\n"
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

	print_method_declarations(indent);

	indent.pop_back();
	out << "}; // class\n";
	// write_method_bodies(out, file);
	out << "} // namespace openapi";
}

void V2Printer::print_method_declarations(std::string& indent) {
	auto& out = cli_hpp_ofs;
	for (const auto& [pathstr, path] : file.paths()) {
		for (const auto& [opstr, op] : path.operations()) {
			print_method_declaration(pathstr, path, opstr, op, indent);

			out << indent << "{\n";
			indent.push_back('\t');
			print_function_body(pathstr, path, opstr, op, indent);
			indent.pop_back();
			out << indent << "}\n";
		}
	}
}

void V2Printer::print_method_declaration(
	std::string_view pathstr,
	const openapi::v2::Path& path,
	std::string_view opstr,
	const openapi::v2::Operation& op,
	std::string& indent) {
	auto& out = cli_hpp_ofs;
	write_multiline_comment(out, op.description(), indent);
	out << indent << "auto "
		<< (op.operationId().empty() ? openapi::SynthesizeFunctionName(pathstr, openapi::RequestMethodFromString(opstr))
									 : op.operationId())
		<< '(';
	print_query_parameters(pathstr, path, opstr, op);
	out << ')';
	out << std::endl;
}

void V2Printer::print_query_parameters(
	std::string_view pathstr,
	const openapi::v2::Path& path,
	std::string_view opstr,
	const openapi::v2::Operation& op) {
	auto& out = cli_hpp_ofs;
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

void V2Printer::print_function_body(
	std::string_view pathstr,
	const openapi::v2::Path& path,
	std::string_view opstr,
	const openapi::v2::Operation& op,
	std::string& indent) {
	auto& out = cli_hpp_ofs;

	// Compose a path pattern, set as constexpr.
	const auto params = op.parameters();
	const bool has_path_param =
		std::any_of(params.begin(), params.end(), [](const openapi::v2::Parameter& p) { return p.in() == "path"; });
	const bool has_query_param =
		std::any_of(params.begin(), params.end(), [](const openapi::v2::Parameter& p) { return p.in() == "query"; });
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
	out << indent << "request_type req;\n";
	if (has_path_param || has_query_param) {
		out << indent << "req.target(fmt::format(path, ";
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
		out << indent << "req.target(path);\n";
	}

	std::string operation = "::boost::beast::http::verb::"s + std::string(opstr);
	if (opstr == "delete") {
		operation.push_back('_');
	}
	out << indent << "req.method(" << operation << ");\n";

	print_client_body(op, indent);
	out << indent << "return this->async_submit_request(std::move(req), token);\n";

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

void V2Printer::print_client_body(const openapi::v2::Operation& op, std::string& indent) {
	const auto& parameters = op.parameters();
	const auto it = std::find_if(
		parameters.begin(), parameters.end(), [](const openapi::v2::Parameter& p) { return p.in() == "body"; });
	if (it == std::end(parameters)) {
		return;
	}

	auto& out = cli_hpp_ofs;
	if (op.consumes().empty() || *op.consumes().begin() == "application/json") {
		out << indent << "::boost::json::monotonic_resource json_rsc(_json_buffer.data(), _json_buffer.size());\n";
		out << indent << "req.set(::boost::beast::http::field::content_type, \"application/json\");\n";
		const std::string_view bodyname = (*it).name().empty() ? "body"sv : (*it).name();
		out << indent << "req.body().assign(::boost::json::serialize(::boost::json::value_from(" << bodyname
			<< ", &json_rsc)));\n";
		return;
	}

	if (*op.consumes().begin() == "application/x-www-form-urlencoded") {
		out << indent << "req.set(::boost::beast::http::field::content_type, \"application/x-www-form-urlencoded\");\n";
		std::string formdata_str = "";
		std::string form_params = "";
		for (const auto& param : parameters) {
			formdata_str += param.name();
			formdata_str += "={}&";
			form_params += param.name();
			form_params += ',';
		}
		formdata_str.pop_back();
		form_params.pop_back();
		out << indent << "constexpr std::string_view form = \"" << formdata_str << "\";\n";
		out << indent << "req.body().assign(fmt::format(form," << form_params << "));\n";
		return;
	}

	if (*op.consumes().begin() == "multipart/form-data") {
		out << indent
			<< "req.set(::boost::beast::http::field::content_type, \"multipart/form-data; boundary=multipart\");\n";
		out << indent << "req.set(::boost::beast::http::field::body, \"--multipart\");\n";
		for (const auto& param : parameters) {
			if (param.in() != "formData") {
				continue;
			}
			out << indent << "req.set(::boost::beast::http::field::content_disposition, \"form-data; name=\\\""
				<< param.name() << "\\\"\");\n";
			out << indent << "req.body().assign(string_cast(" << param.name() << "));\n";
			// Data goes here...
			out << indent << "// http::async_write(_stream, req);\n";
		}
		// TODO need to correctly implement form multipart
		out << indent << "req.set(::boost::beast::http::field::body, \"--multipart--\");\n";
		out << indent << "// http::async_write(_stream, req);\n";
		return;
	}
}

} // namespace siesta::beast::v2