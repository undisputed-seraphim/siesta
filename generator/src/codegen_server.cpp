// SPDX-License-Identifier: Apache-2.0
#include "codegen_server.hpp"
#include "openapi.hpp"
#include "openapi3.hpp"
#include <algorithm>
#include <fstream>

namespace codegen {

void ServerGenerator::operator()(const CodegenArgs& args, const std::filesystem::path& output_dir) {
	if (!args.spec || !args.endpoints || args.endpoints->empty()) {
		return;
	}

	const auto& endpoints = *args.endpoints;

	std::filesystem::create_directories(output_dir);
	{
		auto hpp_path = output_dir / "server.hpp";
		std::ofstream out(hpp_path);
		if (out) {
			emitServerHpp(out, endpoints);
		}
	}
	{
		auto cpp_path = output_dir / "server.cpp";
		std::ofstream out(cpp_path);
		if (out) {
			emitServerCpp(out, endpoints);
		}
	}
}

void ServerGenerator::emitServerHpp(std::ostream& out, const std::vector<Endpoint>& endpoints) {
	out << "#pragma once\n";
	out << "#include <boost/asio.hpp>\n";
	out << "#include <boost/asio/ip/tcp.hpp>\n";
	out << "#include <boost/beast/core.hpp>\n";
	out << "#include <boost/beast/http.hpp>\n";
	out << "#include <functional>\n";
	out << "#include <memory>\n";
	out << "#include <string>\n";
	out << "#include <string_view>\n";
	out << "\n";
	out << "#include \"openapi_defs.hpp\"\n";
	out << "#include <siesta/beast/server.hpp>\n";
	out << "\n";
	out << "namespace openapi {\n";
	out << "\n";
	out << "class Server : public ::siesta::beast::ServerBase {\n";
	out << "public:\n";
	out << "\tusing Config = ::siesta::beast::ServerBase::Config;\n";
	out << "\tusing ::siesta::beast::ServerBase::ServerBase;\n";
	out << "\tusing ::siesta::beast::ServerBase::Session;\n";
	out << "\tusing request = ::siesta::beast::ServerBase::request;\n";
	out << "\n";
	out << "\tusing fnptr_t = void (Server::*)(const request, Session::Ptr);\n";
	out << "\n";
	out << "\tvoid handle_request(const request, Session::Ptr) final;\n";
	out << "\n";

	for (const auto& ep : endpoints) {
		if (!ep.summary.empty()) {
			write_multiline_comment(out, ep.summary, "\t");
		}
		out << "\tvirtual void " << ep.function_name << "(const request, Session::Ptr) = 0;\n";
		out << "\n";
	}

	out << "}; // class Server\n";
	out << "\n";
	out << "} // namespace openapi\n";
}

void ServerGenerator::emitServerCpp(std::ostream& out, const std::vector<Endpoint>& endpoints) {
	out << "#include \"server.hpp\"\n";
	out << "\n";
	out << "#include <string_view>\n";
	out << "#include <unordered_map>\n";
	out << "#include <utility>\n";
	out << "\n";
	out << "namespace http = ::boost::beast::http;\n";
	out << "using std::literals::string_view_literals::operator\"\"sv;\n";
	out << "\n";
	out << "namespace openapi {\n";
	out << "namespace {\n";
	out << "\n";
	out << "using fnptr_t = Server::fnptr_t;\n";
	out << "\n";

	std::vector<const Endpoint*> static_eps;
	std::vector<const Endpoint*> param_eps;
	for (const auto& ep : endpoints) {
		if (ep.path_template.find("{}") != std::string::npos) {
			param_eps.push_back(&ep);
		} else {
			static_eps.push_back(&ep);
		}
	}

	if (!static_eps.empty()) {
		out << "const std::unordered_map<std::pair<std::string_view, http::verb>, Server::fnptr_t,\n";
		out << "    ::siesta::beast::__detail::MapHash> STATIC_PATHS = {\n";
		for (const auto* ep : static_eps) {
			out << "\t{{\"" << escapeCppString(ep->path) << "\"sv, http::verb::" << ep->cpp_verb
				<< "}, &Server::" << ep->function_name << "},\n";
		}
		out << "};\n\n";
	}

	out << "bool match_path(std::string_view pattern, std::string_view target) {\n";
	out << "\twhile (!pattern.empty() && !target.empty()) {\n";
	out << "\t\tif (pattern.front() == '/' && target.front() == '/') {\n";
	out << "\t\t\tpattern.remove_prefix(1);\n";
	out << "\t\t\ttarget.remove_prefix(1);\n";
	out << "\t\t\tcontinue;\n";
	out << "\t\t}\n";
	out << "\t\tauto pp = pattern.find('/');\n";
	out << "\t\tauto tp = target.find('/');\n";
	out << "\t\tauto ps = pattern.substr(0, pp);\n";
	out << "\t\tauto ts = target.substr(0, tp);\n";
	out << "\t\tif (ps != \"{}\"sv && ps != ts) return false;\n";
	out << "\t\tpattern = pp == std::string_view::npos ? \"\"sv : pattern.substr(pp);\n";
	out << "\t\ttarget  = tp == std::string_view::npos ? \"\"sv : target.substr(tp);\n";
	out << "\t}\n";
	out << "\treturn pattern == target;\n";
	out << "}\n";
	out << "\n";

	if (!param_eps.empty()) {
		out << "const std::pair<std::string_view, std::pair<http::verb, fnptr_t>> PARAM_PATHS[] = {\n";
		for (const auto* ep : param_eps) {
			out << "\t{\"" << escapeCppString(ep->path_template) << "\"sv, {http::verb::" << ep->cpp_verb
				<< ", &Server::" << ep->function_name << "}},\n";
		}
		out << "};\n\n";
	}

	out << "} // anonymous namespace\n";
	out << "\n";

	out << "void Server::handle_request(const request req, Session::Ptr session) {\n";
	out << "\tauto target = std::string_view(req.target());\n";
	out << "\tif (auto q = target.find('?'); q != std::string_view::npos) target = target.substr(0, q);\n";
	out << "\tconst auto method = req.method();\n";
	out << "\n";

	if (!static_eps.empty()) {
		out << "\tif (auto it = STATIC_PATHS.find({target, method}); it != STATIC_PATHS.end()) {\n";
		out << "\t\treturn (this->*(it->second))(req, std::move(session));\n";
		out << "\t}\n";
		out << "\n";
	}

	if (!param_eps.empty()) {
		out << "\tfor (const auto& [pattern, verb_fn] : PARAM_PATHS) {\n";
		out << "\t\tif (match_path(pattern, target) && verb_fn.first == method) {\n";
		out << "\t\t\treturn (this->*(verb_fn.second))(req, std::move(session));\n";
		out << "\t\t}\n";
		out << "\t}\n";
		out << "\n";
	}

	out << "\t// 404 Not Found\n";
	out << "\tauto& resp = session->get_response();\n";
	out << "\tresp.result(http::status::not_found);\n";
	out << "\tsession->write();\n";
	out << "}\n";
	out << "\n";
	out << "} // namespace openapi\n";
}

} // namespace codegen
