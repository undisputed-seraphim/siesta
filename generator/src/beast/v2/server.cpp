#include <algorithm>
#include <beast/beast.hpp>
#include <beast/v2/beastv2.hpp>

namespace fs = ::std::filesystem;
using namespace std::literals;

namespace siesta::beast::v2 {

void V2Printer::print_server(const std::filesystem::path& output_dir) {
	srv_hpp = output_dir / (name + "_server.hpp");
	srv_cpp = output_dir / (name + "_server.cpp");
	srv_hpp_ofs = std::ofstream(srv_hpp);
	srv_cpp_ofs = std::ofstream(srv_cpp);
	print_server_hpp();
	print_server_cpp();
}

void V2Printer::print_server_hpp() {
	auto& out = srv_hpp_ofs;
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
		<< "#include \"" << name << "_defs.hpp\"\n"
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
			print_query_details(op, indent);
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

void V2Printer::print_server_cpp() {
	auto& out = srv_cpp_ofs;
	out << "#include <boost/json.hpp>\n"
		<< "#include <fmt/format.h>\n"
		<< '\n'
		<< "#include \"" << srv_hpp.filename().string() << "\"\n"
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

	print_dispatcher_function("Server");

	std::string indent;
	out << "} // namespace swagger\n";
}

void V2Printer::print_query_details(const openapi::v2::Operation& op, std::string& indent) {
	auto& out = srv_hpp_ofs;
	for (const auto& p : op.parameters()) {
		if (p.in() == "body") {
			if (auto schema = p.schema(); schema) {
				out << indent << "// \\param[in] " << (schema.IsReference() ? schema.reference() : schema.type())
					<< " (body) ";
			}
		} else {
			out << indent << "// \\param[in] " << p.name() << " (" << p.in() << ") ";
		}
		out << '\n';
	}
}

void V2Printer::print_dispatcher_function(std::string className) {
	auto& out = srv_hpp_ofs;
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

} // namespace siesta::beast::v2