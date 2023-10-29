#include "beast.hpp"
#include <string>
#include <unordered_map>

namespace fs = std::filesystem;
using namespace std::literals;

namespace siesta::beast {

const std::unordered_map<std::string_view, std::string_view> verbMap = {
	{"", ""},
	{"delete", "delete_"},
	{"get", "get"},
	{"head", "head"},
	{"post", "post"},
	{"put", "put"},
	{"connect", "connect"},
	{"options", "options"},
	{"trace", "trace"},
	// WebDAV
	{"copy", "copy"},
	{"lock", "lock"},
	{"mkcol", "mkcol"},
	{"move", "move"},
	{"propfind", "propfind"},
	{"proppatch", "proppatch"},
	{"search", "search"},
	{"unlock", "unlock"},
	{"bind", "bind"},
	{"rebind", "rebind"},
	{"unbind", "unbind"},
	{"acl", "acl"},
};

void write_query_details(std::ostream& os, const openapi::Operation& op, std::string& indent) {
	for (const auto& p : op.parameters()) {
		if (p.in() == "body") {
			if (auto schema = p.schema(); schema) {
				os << indent << "// \\param[in] " << (schema.IsReference() ? schema.reference() : schema.type())
				   << " (body) ";
			}
		} else {
			os << indent << "// \\param[in] " << p.name() << " (" << p.in() << ") ";
		}
		os << '\n';
	}
}

void write_server_function_body(
	std::ostream& os,
	const openapi::OpenAPI2& file,
	std::string_view pathstr,
	const openapi::Path& path,
	std::string_view opstr,
	const openapi::Operation& op,
	std::string& indent) {}

void write_dispatcher_function(std::ostream& out, std::string_view className, const openapi::OpenAPI2& file) {
	// TODO: Replace this with a trie
	out << "const std::unordered_map<std::pair<std::string_view, http::verb>, Server::fnptr_t, "
		   "::siesta::beast::__detail::MapHash> g_pathmap = {\n";
	for (const auto& [pathstr, path] : file.paths()) {
		// TODO: Need to clean the path string here
		const auto fullPathStr = std::string(file.basePath()).append(pathstr);
		for (const auto& [opstr, op] : path.operations()) {
			const std::string fnName =
				(op.operationId().empty()
					 ? openapi::SynthesizeFunctionName(pathstr, openapi::RequestMethodFromString(opstr))
					 : std::string(op.operationId()));
			out << "\t{{\"" << fullPathStr << "\"sv, http::verb::" << verbMap.at(opstr) << "}, &Server::" << fnName
				<< "},\n";
		}
	}
	out << "};\n\n";

	std::string indent = "\t";
	out << "void " << className << "::handle_request(const request req, Session::Ptr session) {\n"
		<< indent << "fnptr_t fnptr = g_pathmap.at({req.target(), req.method()});\n"
		<< indent << "(this->*fnptr)(req, std::move(session));\n";

	out << "}\n" << std::endl;
}

void beast_server_hpp(const fs::path& input, const fs::path& output, const openapi::OpenAPI2& file) {
	auto out = std::ofstream(output / (input.stem().string() + "_server.hpp"));
	out << "#pragma once\n"
		<< "#include <boost/asio.hpp>\n"
		<< "#include <boost/asio/ip/tcp.hpp>\n"
		<< "#include <boost/beast/core.hpp>\n"
		<< "#include <boost/beast/http.hpp>\n"
		<< "#include <functional>\n"
		<< "#include <memory>\n"
		<< "#include <string>\n"
		<< "#include <string_view>\n"
		<< '\n'
		<< "#include \"" << input.stem().string() << "_defs.hpp\"\n"
		<< "#include <siesta/beast/server.hpp>\n"
		<< '\n'
		<< "namespace swagger {\n"
		<< std::endl;
	out << "class Server : public ::siesta::beast::ServerBase {\n"
		<< "public:\n";

	std::string indent;
	indent.push_back('\t');
	out << indent << "using ::siesta::beast::ServerBase::Config;\n"
		<< indent << "using ::siesta::beast::ServerBase::ServerBase;\n"
		<< indent << "using ::siesta::beast::ServerBase::Session;\n\n"
		<< indent << "// Function pointer type of a request endpoint.\n"
		<< indent << "using fnptr_t = void (Server::*)(const Server::request, Server::Session::Ptr);\n\n"
		<< indent << "void handle_request(const request, Session::Ptr) final;\n"
		<< '\n';

	for (const auto& [pathstr, path] : file.paths()) {
		for (const auto& [opstr, op] : path.operations()) {
			write_multiline_comment(out, op.description(), indent);
			write_query_details(out, op, indent);
			out << indent << "virtual void "
				<< (op.operationId().empty()
						? openapi::SynthesizeFunctionName(pathstr, openapi::RequestMethodFromString(opstr))
						: op.operationId())
				<< "(const request, Session::Ptr) = 0;\n\n";
		}
	}
	indent.pop_back();
	out << "}; // class\n";
	out << "} // namespace swagger\n";
}

void beast_server_cpp(const fs::path& input, const fs::path& output, const openapi::OpenAPI2& file) {
	const auto header_path = output / (input.stem().string() + "_server.hpp");
	auto out = std::ofstream(output / (input.stem().string() + "_server.cpp"));
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

	write_dispatcher_function(out, "Server", file);

	std::string indent;

	// for (const auto& [pathstr, path] : file.paths()) {
	//	for (const auto& [opstr, op] : path.operations()) {
	//		const std::string functionName =
	//			(op.operationId().empty()
	//				 ? openapi::SynthesizeFunctionName(pathstr, openapi::RequestMethodFromString(opstr))
	//				 : std::string(op.operationId()));
	//		out << "void Server::" << std::move(functionName) << "(";
	//		//write_query_parameters(out, pathstr, path, opstr, op, indent);
	//		out << ") {\n";
	//		indent.push_back('\t');
	//		write_server_function_body(out, file, pathstr, path, opstr, op, indent);
	//		indent.pop_back();
	//		out << "}\n";
	//		out << std::endl;
	//	}
	// }
	out << "} // namespace swagger\n";
}

} // namespace siesta::beast