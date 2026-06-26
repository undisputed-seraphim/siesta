// SPDX-License-Identifier: Apache-2.0
#include "Backend/Beast/ServerEmitter.hpp"
#include "Support/Filenames.hpp"
#include "Support/Utils.hpp"
#include <string_view>
#include <unordered_map>

namespace codegen {

// ═══════════════════════════════════════════════════════════════
//  Internal helpers
// ═══════════════════════════════════════════════════════════════

namespace {

struct DispatchSets {
	std::vector<const Endpoint*> static_eps;
	std::vector<const Endpoint*> param_eps;
	std::vector<const Endpoint*> ws_eps;
};

void collectDispatchSets(const std::vector<Endpoint>& endpoints, DispatchSets& ds) {
	for (const auto& ep : endpoints) {
		if (ep.is_websocket) {
			ds.ws_eps.push_back(&ep);
		} else if (ep.path_template.find("{}") != std::string::npos) {
			ds.param_eps.push_back(&ep);
		} else {
			ds.static_eps.push_back(&ep);
		}
	}
}

void emitMatchPath(std::ostream& out) {
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
	out << "}\n\n";
}

void emitStaticPathMap(std::ostream& out, const DispatchSets& ds) {
	if (ds.static_eps.empty()) return;
	out << "const std::unordered_map<std::pair<std::string_view, http::verb>, fnptr_t,\n";
	out << "    ::siesta::beast::__detail::MapHash> STATIC_PATHS = {\n";
	for (const auto* ep : ds.static_eps) {
		out << "\t{{\"" << escapeCppString(ep->path) << "\"sv, http::verb::" << ep->cpp_verb
			<< "}, &Server::" << ep->function_name << "},\n";
		if (ep->cpp_verb == "get") {
			out << "\t{{\"" << escapeCppString(ep->path) << "\"sv, http::verb::head"
				<< "}, &Server::" << ep->function_name << "},\n";
		}
	}
	out << "};\n\n";
}

void emitParamPathArray(std::ostream& out, const DispatchSets& ds) {
	if (ds.param_eps.empty()) return;
	out << "const std::pair<std::string_view, std::pair<http::verb, fnptr_t>> PARAM_PATHS[] = {\n";
	for (const auto* ep : ds.param_eps) {
		out << "\t{\"" << escapeCppString(ep->path_template) << "\"sv, {http::verb::" << ep->cpp_verb
			<< ", &Server::" << ep->function_name << "}},\n";
		if (ep->cpp_verb == "get") {
			out << "\t{\"" << escapeCppString(ep->path_template) << "\"sv, {http::verb::head"
				<< ", &Server::" << ep->function_name << "}},\n";
		}
	}
	out << "};\n\n";
}

void emitWebSocketDispatch(std::ostream& out, const DispatchSets& ds) {
	if (ds.ws_eps.empty()) return;
	out << "\t// WebSocket upgrade\n";
	out << "\tif (::boost::beast::websocket::is_upgrade(req)) {\n";
	for (const auto* ep : ds.ws_eps) {
		out << "\t\tif (target == \"" << escapeCppString(ep->path) << "\"sv) {\n";
		out << "\t\t\tsession->upgrade_to_websocket(req,\n";
		out << "\t\t\t\t[this](auto& ws, const request& req, auto session) {\n";
		out << "\t\t\t\t\t" << ep->function_name << "(ws, req, std::move(session));\n";
		out << "\t\t\t\t});\n";
		out << "\t\t\treturn;\n";
		out << "\t\t}\n";
	}
	out << "\t}\n\n";
}

void emitStaticDispatch(std::ostream& out, const DispatchSets& ds) {
	if (ds.static_eps.empty()) return;
	out << "\tif (auto it = STATIC_PATHS.find({target, method}); it != STATIC_PATHS.end()) {\n";
	out << "\t\treturn (this->*(it->second))(req, std::move(session));\n";
	out << "\t}\n\n";
}

void emitParamDispatch(std::ostream& out, const DispatchSets& ds) {
	if (ds.param_eps.empty()) return;
	out << "\tfor (const auto& [pattern, verb_fn] : PARAM_PATHS) {\n";
	out << "\t\tif (match_path(pattern, target) && verb_fn.first == method) {\n";
	out << "\t\t\treturn (this->*(verb_fn.second))(req, std::move(session));\n";
	out << "\t\t}\n";
	out << "\t}\n\n";
}

void emit404Fallback(std::ostream& out) {
	out << "\t// 404 Not Found\n";
	out << "\thttp::response<http::string_body> resp{http::status::not_found, req.version()};\n";
	out << "\tresp.body() = \"{\\\"error\\\":\\\"not found\\\"}\";\n";
	out << "\tresp.set(http::field::content_type, \"application/json\");\n";
	out << "\tresp.prepare_payload();\n";
	out << "\tsession->send(std::move(resp));\n";
}

void emitHandleRequestBody(std::ostream& out, const std::vector<Endpoint>& endpoints) {
	DispatchSets ds;
	collectDispatchSets(endpoints, ds);

	out << "void Server::handle_request(const request req, Session::Ptr session) {\n";
	out << "\tauto target = std::string_view(req.target());\n";
	out << "\tif (auto q = target.find('?'); q != std::string_view::npos) target = target.substr(0, q);\n";
	out << "\tconst auto method = req.method();\n\n";
	emitWebSocketDispatch(out, ds);
	emitStaticDispatch(out, ds);
	emitParamDispatch(out, ds);
	emit404Fallback(out);
	out << "}\n\n";
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════
//  Public API
// ═══════════════════════════════════════════════════════════════

void emitServerHpp(std::ostream& out, const std::vector<Endpoint>& endpoints, std::string_view ns) {
	out << "#pragma once\n";
	out << "#include <boost/asio.hpp>\n";
	out << "#include <boost/asio/ip/tcp.hpp>\n";
	out << "#include <boost/beast/core.hpp>\n";
	out << "#include <boost/beast/http.hpp>\n";
	out << "#include <boost/beast/websocket.hpp>\n";
	out << "#include <functional>\n";
	out << "#include <memory>\n";
	out << "#include <string>\n";
	out << "#include <string_view>\n";
	out << "\n";
	out << "#include \"" << filenames::DEFS_HPP << "\"\n";
	out << "#include <siesta/beast/server.hpp>\n";
	out << "\n";
	out << "namespace " << ns << " {\n";
	out << "\n";
	out << "class Server : public ::siesta::beast::ServerBase {\n";
	out << "public:\n";
	out << "\tusing Config = ::siesta::beast::ServerBase::Config;\n";
	out << "\tusing ::siesta::beast::ServerBase::ServerBase;\n";
	out << "\tusing ::siesta::beast::ServerBase::Session;\n";
	out << "\tusing request = ::siesta::beast::ServerBase::request;\n";
	out << "\n";
	out << "\tvoid handle_request(const request, Session::Ptr) final;\n";
	out << "\n";

	for (const auto& ep : endpoints) {
		if (ep.is_websocket) {
			if (!ep.summary.empty()) {
				write_multiline_comment(out, ep.summary, "\t");
			}
			out << "\tvirtual void " << ep.function_name << "(\n";
			out << "\t\t::boost::beast::websocket::stream<\n";
			out << "\t\t\t::siesta::beast::ServerBase::stream_type&>& ws,\n";
			out << "\t\tconst request,\n";
			out << "\t\tSession::Ptr) = 0;\n";
			out << "\n";
			continue;
		}
		if (!ep.summary.empty()) {
			write_multiline_comment(out, ep.summary, "\t");
		}
		out << "\tvirtual void " << ep.function_name << "(const request, Session::Ptr) = 0;\n";
		out << "\n";
	}

	out << "}; // class Server\n";
	out << "\n";
	out << "} // namespace " << ns << "\n";
}

void emitServerCpp(std::ostream& out, const std::vector<Endpoint>& endpoints, std::string_view ns) {
	out << "#include \"server.hpp\"\n";
	out << "\n";
	out << "#include <string_view>\n";
	out << "#include <unordered_map>\n";
	out << "#include <utility>\n";
	out << "\n";
	out << "namespace http = ::boost::beast::http;\n";
	out << "using std::literals::string_view_literals::operator\"\"sv;\n";
	out << "\n";
	out << "namespace " << ns << " {\n";
	out << "namespace {\n";
	out << "\n";
	out << "using fnptr_t = void (Server::*)(const Server::request, Server::Session::Ptr);\n";
	out << "\n";

	DispatchSets ds;
	collectDispatchSets(endpoints, ds);

	emitMatchPath(out);
	emitStaticPathMap(out, ds);
	emitParamPathArray(out, ds);

	out << "} // anonymous namespace\n";
	out << "\n";

	emitHandleRequestBody(out, endpoints);

	out << "} // namespace " << ns << "\n";
}

} // namespace codegen
