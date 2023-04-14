#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>

#include "openapi2.hpp"
#include "util.hpp"

namespace fs = std::filesystem;
using namespace std::literals;

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
}

void beast_server_hpp(const fs::path& input, const fs::path& output, openapi::OpenAPI2& file) {
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
		<< std::endl;

	std::string indent;
	indent.push_back('\t');
	out << "class Server {\n";
	for (const auto& [pathstr, path] : file.paths()) {
		for (const auto& [opstr, op] : path.operations()) {
			write_multiline_comment(out, op.description(), indent);
			write_query_details(out, op, indent);
			out << indent << "virtual void " << op.operationId() << "() = 0;\n\n";
		}
	}
	indent.pop_back();
	out << "}; // class\n";
}

void beast_server_cpp(const fs::path& input, const fs::path& output, openapi::OpenAPI2& file) {
	auto out = std::ofstream(output / (input.stem().string() + "_server.cpp"));
}

void beast_client_hpp(const fs::path& input, const fs::path& output, openapi::OpenAPI2& file) {
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
		<< "namespace beast = boost::beast;\n"
		<< "namespace ip    = boost::asio::ip;\n"
		<< std::endl;
	out << "class Client {\n";

	std::string indent;
	indent.push_back('\t');
	for (const auto& [pathstr, path] : file.paths()) {
		for (const auto& [opstr, op] : path.operations()) {
			write_multiline_comment(out, op.description(), indent);
			if (!op.operationId().empty()) {
				out << indent << "void " << op.operationId() << '(';
			} else {
				out << indent << "void " << openapi::SynthesizeFunctionName(pathstr, openapi::RequestMethodFromString(opstr)) << '(';
			}
			for (const auto& param : op.parameters()) {
				out << openapi::JsonTypeToCppType(param.type()) << ' ' << param.name() << ", ";
			}
			if (!op.parameters().empty()) {
				out.seekp(-2, std::ios::end);
			}
			out << ");\n";
			out << std::endl;
		}
	}
	indent.pop_back();
	out << "}; // class\n";
}

void beast_client_cpp(const fs::path& input, const fs::path& output, openapi::OpenAPI2& file) {
	const auto header_path = output / (input.stem().string() + "_client.hpp");
	auto out = std::ofstream(output / (input.stem().string() + "_client.cpp"));
	out << "#include \"" << header_path.filename().string() << "\"\n";
}

} // namespace

void beast(const fs::path& input, const fs::path& output, openapi::OpenAPI2& file) {
	::beast_server_hpp(input, output, file);
	::beast_server_cpp(input, output, file);

	::beast_client_hpp(input, output, file);
	::beast_client_cpp(input, output, file);
}
