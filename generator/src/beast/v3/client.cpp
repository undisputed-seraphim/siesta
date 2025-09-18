// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <beast/beast.hpp>
#include <beast/v3/beastv3.hpp>
#include <util.hpp>

namespace fs = ::std::filesystem;
using namespace std::literals;

namespace siesta::beast::v3 {

const openapi::v3::Parameter V3Printer::resolveIfRef(const openapi::v3::Parameter& p) {
	if (p.IsRef()) {
		if (const auto [name, param] = file.components().GetParameterByRef(p.ref()); param) {
			return param.value();
		} else {
			throw std::runtime_error("Malformed reference " + std::string(name));
		}
	}
	return p;
}

const openapi::v3::JsonSchema V3Printer::resolveIfRef(const openapi::v3::JsonSchema& p) {
	if (p.IsRef()) {
		if (const auto [name, schema] = file.components().GetSchemaByRef(p.ref()); schema) {
			return schema.value();
		} else {
			throw std::runtime_error("Malformed reference " + std::string(name));
		}
	}
	return p;
}

void V3Printer::print_client(const fs::path& output_dir) {
	cli_hpp = output_dir / (name + "_client.hpp");
	cli_cpp = output_dir / (name + "_client.cpp");
	cli_hpp_ofs = std::ofstream(cli_hpp);
	cli_cpp_ofs = std::ofstream(cli_cpp);
	print_client_header();
}

void V3Printer::print_client_header() {
	auto& out = cli_hpp_ofs;
	out << "#pragma once\n"
		<< "#include <boost/asio.hpp>\n"
		<< "#include <boost/asio/ip/tcp.hpp>\n"
		<< "#include <boost/beast/core.hpp>\n"
		<< "#include <boost/beast/http.hpp>\n"
		<< "#include <boost/json.hpp>\n"
		<< "#include <format>\n"
		<< "#include <functional>\n"
		<< "#include <memory>\n"
		<< "#include <string>\n"
		<< "#include <string_view>\n"
		<< '\n'
		<< "#include \"" << name << "_defs.hpp\"\n"
		<< "#include <siesta/beast/client.hpp>\n"
		<< '\n'
		<< "namespace openapi {\n"
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

	out << "} // namespace openapi";
}

void V3Printer::print_method_declarations(std::string& indent) {
	auto& out = cli_hpp_ofs;
	for (const auto& [pathstr, path] : file.paths()) {
		write_multiline_comment(out, path.description(), indent);
		for (const auto& [opname, op] : path.operations()) {
			print_method_declaration(pathstr, path, opname, op, indent);

			out << indent << "{\n";
			indent.push_back('\t');
			print_function_body(pathstr, path, opname, op, indent);
			indent.pop_back();
			out << indent << "}\n";
		}
	}
}

void V3Printer::print_method_declaration(
	std::string_view pathstr,
	const openapi::v3::Path& path,
	std::string_view opstr,
	const openapi::v3::Operation& op,
	std::string& indent) {
	auto& out = cli_hpp_ofs;
	const auto verb = openapi::RequestMethodFromString(opstr);
	write_multiline_comment(out, op.description(), indent);
	std::string function_name =
		op.operationId().empty() ? openapi::SynthesizeFunctionName(pathstr, verb) : std::string(op.operationId());

	out << indent << "auto " << function_name << '(';
	print_query_parameters(pathstr, path, opstr, op);
	out << ")\n";
}

void V3Printer::print_query_parameters(
	std::string_view pathstr,
	const openapi::v3::Path& path,
	std::string_view opstr,
	const openapi::v3::Operation& op) {

	using namespace openapi::v3;
	struct SchemaVisitor final {
		std::ostream& _os;
		std::string_view _name;

		void print_primitive(std::string_view type, std::string_view format) const {
			_os << openapi::JsonTypeToCppType(type, format) << ' ' << sanitize(_name) << ", ";
		}
		void operator()(const String& schema) const { print_primitive(schema.type(), schema.format()); }
		void operator()(const Number& schema) const { print_primitive(schema.type(), schema.format()); }
		void operator()(const Integer& schema) const { print_primitive(schema.type(), schema.format()); }
		void operator()(const Boolean& schema) const { print_primitive(schema.type(), schema.format()); }
		void operator()(const Object& schema) const { printf("warning: skipped object %s\n", _name.data()); }
		void operator()(const Array& schema) const {
			_os << "const std::vector<" << openapi::JsonTypeToCppType(schema.items().type(), schema.items().format())
				<< ">& " << sanitize(_name) << ", ";
		}
	};

	auto& out = cli_hpp_ofs;

	for (const auto& param : op.parameters()) {
		const auto realp = resolveIfRef(param);
		const auto sanitized_paramname = sanitize(realp.name());
		realp.schema().Visit(SchemaVisitor{out, sanitized_paramname});
	}
	out << "::boost::asio::completion_token_for<void(outcome_type)> auto&& token";
}

void V3Printer::print_function_body(
	std::string_view pathstr,
	const openapi::v3::Path& path,
	std::string_view opstr,
	const openapi::v3::Operation& op,
	std::string& indent) {
	auto& out = cli_hpp_ofs;

	// Compose a path pattern, set as constexpr.
	const auto params = op.parameters();
	const bool has_path_param = std::any_of(params.begin(), params.end(), [this](const openapi::v3::Parameter& p) {
		return resolveIfRef(p).in() == "path";
	});
	const bool has_query_param = std::any_of(params.begin(), params.end(), [this](const openapi::v3::Parameter& p) {
		return resolveIfRef(p).in() == "query";
	});
	const bool is_post = (opstr == "post");

	std::string full_path;
	full_path += has_path_param ? clean_path_string(pathstr) : std::string(pathstr);

	if (has_query_param && !is_post) {
		full_path.push_back('?');
		for (const auto& p : params) {
			if (const auto realparam = resolveIfRef(p); realparam.in() == "query") {
				full_path += std::string(realparam.name()) + "={}&";
			}
		}
		full_path.pop_back();
	}

	out << indent << "constexpr std::string_view path = \"" << full_path << "\";\n";
	out << indent << "request_type req;\n";
	if (has_path_param || (has_query_param && !is_post)) {
		out << indent << "req.target(std::format(path, ";
	}
	if (has_path_param) {
		for (const auto& p : params) {
			if (const auto realp = resolveIfRef(p); realp.in() == "path") {
				out << realp.name() << ", ";
			}
		}
	}
	if (has_query_param && !is_post) {
		for (const auto& p : params) {
			if (const auto realp = resolveIfRef(p); realp.in() == "query") {
				out << realp.name() << ", ";
			}
		}
	}
	if (has_path_param || (has_query_param && !is_post)) {
		out.seekp(-2, std::ios::end);
		out << "));\n";
	} else {
		out << indent << "req.target(path);\n";
	}

	if (is_post) {
		out << indent << "constexpr std::string_view queryfmt = \"";
		for (const auto& p : params) {
			if (const auto realparam = resolveIfRef(p); realparam.in() == "query") {
				out << std::string(realparam.name()) << "={}&";
			}
		}
		out << "\";\n";
		out << indent << "req.body().append(std::format(queryfmt, ";
		for (const auto& p : params) {
			if (const auto realp = resolveIfRef(p); realp.in() == "query") {
				out << realp.name() << ", ";
			}
		}
		out.seekp(-2, std::ios::end);
		out << "));\n";
	}

	std::string operation = "::boost::beast::http::verb::"s + std::string(opstr);
	if (opstr == "delete") {
		operation.push_back('_');
	}
	out << indent << "req.method(" << operation << ");\n";

	print_client_body(op, indent);
	out << indent << "return this->async_submit_request(std::move(req), token);\n";
}

void V3Printer::print_client_body(const openapi::v3::Operation& op, std::string& indent) {
	const auto& parameters = op.parameters();
	const auto it = std::find_if(
		parameters.begin(), parameters.end(), [](const openapi::v3::Parameter& p) { return p.in() == "body"; });
	if (it == std::end(parameters)) {
		return;
	}

	auto& out = cli_hpp_ofs;
	for (const auto& [type, val] : (*it).content()) {
		if (type == "application/json") {
			const auto& schema = resolveIfRef(val.schema());
			out << indent << "::boost::json::monotonic_resource json_rsc(_json_buffer.data(), _json_buffer.size());\n";
			out << indent << "req.set(::boost::beast::http::field::content_type, \"application/json\");\n";
			const std::string_view bodyname = schema.name().empty() ? "body"sv : schema.name();
			out << indent << "req.body().assign(::boost::json::serialize(::boost::json::value_from(" << bodyname
				<< ", &json_rsc)));\n";
		}
	}
}

} // namespace siesta::beast::v3