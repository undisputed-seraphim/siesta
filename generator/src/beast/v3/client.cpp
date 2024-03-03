// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <beast/beast.hpp>
#include <beast/v3/beastv3.hpp>

namespace fs = ::std::filesystem;
using namespace std::literals;

namespace siesta::beast::v3 {

std::string_view get_ref_objname(std::string_view reference) noexcept {
	const std::size_t pos = reference.find_last_of('/');
	return reference.substr(pos + 1);
}

std::pair<std::string_view, std::optional<openapi::v3::Parameter>>
getParameterByRef(const openapi::v3::OpenAPIv3& file, std::string_view ref) {
	size_t pos = ref.find_first_of('/');
	ref.remove_prefix(pos + 1);
	pos = ref.find_first_of('/');
	if (ref.substr(0, pos) == "components") {
		ref.remove_prefix(pos + 1);
		pos = ref.find_first_of('/');
		if (ref.substr(0, pos) == "parameters") {
			ref.remove_prefix(pos + 1);
			for (const auto& [paramname, param] : file.components().parameters()) {
				if (paramname == ref) {
					return std::pair{ref, std::optional{param}};
				}
			}
		}
	}
	return std::pair{ref, std::nullopt};
}

std::pair<std::string_view, std::optional<openapi::v3::JsonSchema>>
getSchemaByRef(const openapi::v3::OpenAPIv3& file, std::string_view ref) {
	size_t pos = ref.find_first_of('/');
	ref.remove_prefix(pos + 1);
	pos = ref.find_first_of('/');
	if (ref.substr(0, pos) == "components") {
		ref.remove_prefix(pos + 1);
		pos = ref.find_first_of('/');
		if (ref.substr(0, pos) == "schemas") {
			ref.remove_prefix(pos + 1);
			for (const auto& [schemaname, schema] : file.components().schemas()) {
				if (schemaname == ref) {
					return std::pair{ref, std::optional{schema}};
				}
			}
		}
	}
	return std::pair{ref, std::nullopt};
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
		<< "#include <fmt/format.h>\n"
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
			_os << openapi::JsonTypeToCppType(type, format) << ' ' << _name << ", ";
		}
		void operator()(const String& schema) const { print_primitive(schema.type(), schema.format()); }
		void operator()(const Number& schema) const { print_primitive(schema.type(), schema.format()); }
		void operator()(const Integer& schema) const { print_primitive(schema.type(), schema.format()); }
		void operator()(const Boolean& schema) const { print_primitive(schema.type(), schema.format()); }
		void operator()(const Object& schema) const { printf("warning: skipped object %s\n", _name.data()); }
		void operator()(const Array& schema) const { printf("warning: skipped array %s\n", _name.data()); }
	};

	auto& out = cli_hpp_ofs;

	for (const auto& param : op.parameters()) {
		auto sanitized_paramname = sanitize(param.name());
		if (param.IsRef()) {
			auto realparam = getParameterByRef(file, param.ref());
			if (realparam.second) {
				realparam.second.value().schema().Visit(SchemaVisitor{out, realparam.first});
			} else {
				printf("warning: object referenced by %s was not found.\n", param.ref().data());
			}
		}

		if (auto schema = param.schema(); schema) {
			if (schema.IsRef()) {
				std::string_view objname = get_ref_objname(schema.ref());
				out << sanitize(objname) << ' ' << sanitized_paramname;
			} else {
				out << openapi::JsonTypeToCppType(schema.type(), schema.format()) << ' ' << param.name();
			}
			out << ", ";
		}
	}
	if (!op.parameters().empty()) {
		out.seekp(-2, std::ios::end);
	} else {
		out << "void";
	}
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
	const bool has_path_param =
		std::any_of(params.begin(), params.end(), [](const openapi::v3::Parameter& p) { return p.in() == "path"; });
	const bool has_query_param =
		std::any_of(params.begin(), params.end(), [](const openapi::v3::Parameter& p) { return p.in() == "query"; });

	std::string full_path;
	if (has_path_param) {
		full_path += clean_path_string(pathstr);
	} else {
		full_path += std::string(pathstr);
	}
	if (has_query_param) {
		full_path.push_back('?');
		for (const auto& p : params) {
			if (p.IsRef()) {
				auto realparam = getParameterByRef(file, p.ref());
				if (realparam.second) {
					if (realparam.second.value().in() == "query") {
						full_path += std::string(realparam.first) + "={},";
					}
				}
			}
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

	print_client_json_body(op, indent);
	// print_client_multipart_body(op, indent);
	print_client_form_body(op, indent);
	out << indent << "return this->async_submit_request(std::move(req), token);\n";
}

void V3Printer::print_client_json_body(const openapi::v3::Operation& op, std::string& indent) {}

void V3Printer::print_client_form_body(const openapi::v3::Operation& op, std::string& indent) {}

} // namespace siesta::beast::v3