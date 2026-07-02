// SPDX-License-Identifier: Apache-2.0
#include "Backend/Beast/ServerEmitter.hpp"
#include "Backend/Shared/DispatchEmitter.hpp"
#include "Support/Filenames.hpp"
#include "Support/Utils.hpp"
#include <string_view>
#include <unordered_map>

namespace codegen {

// ═══════════════════════════════════════════════════════════════
//  Internal helpers (beast-specific dispatch bodies)
// ═══════════════════════════════════════════════════════════════

namespace {

void emitTableData(std::ostream& out, const DispatchSets& ds) {
	emitMatchPath(out);
	emitStaticPathMap(out, ds, "http::", "::siesta::beast::__detail::MapHash");
	emitParamPathArray(out, ds, "http::");
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
	out << "\t\tauto& [fn, name] = it->second;\n";
	out << "\t\tServer::request_context _rctx{&req, std::move(session), name};\n";
	out << "\t\tif (!this->run_interceptors(_rctx)) {\n";
	out << "\t\t\tif (_rctx.error)\n";
	out << "\t\t\t\t_rctx.session->send(_rctx.session->make_error_response(*_rctx.error));\n";
	out << "\t\t\telse\n";
	out << "\t\t\t\t_rctx.session->send(_rctx.session->make_response(500));\n";
	out << "\t\t\treturn;\n";
		out << "\t\t}\n";
		out << "\t\tauto _s = std::move(_rctx.session);\n";
		out << "\t\ttry {\n";
		out << "\t\t\t(this->*(fn))(req, _s);\n";
		out << "\t\t} catch (const std::exception& e) {\n";
		out << "\t\t\t_s->send(_s->make_error_response(\n";
		out << "\t\t\t\tsiesta::Error{siesta::ErrorCode::INVALID_ARGUMENT, \"invalid request\"}));\n";
		out << "\t\t} catch (...) {\n";
		out << "\t\t\t_s->send(_s->make_error_response(\n";
		out << "\t\t\t\tsiesta::Error{siesta::ErrorCode::INTERNAL, \"unexpected error\"}));\n";
		out << "\t\t}\n";
		out << "\t\treturn;\n";
		out << "\t}\n\n";
}

void emitParamDispatch(std::ostream& out, const DispatchSets& ds) {
	if (ds.param_eps.empty()) return;
	out << "\tfor (const auto& [pattern, verb_fn] : PARAM_PATHS) {\n";
	out << "\t\tif (match_path(pattern, target) && verb_fn.first == method) {\n";
	out << "\t\t\tauto& [fn, name] = verb_fn.second;\n";
	out << "\t\t\tServer::request_context _rctx{&req, std::move(session), name};\n";
	out << "\t\t\tif (!this->run_interceptors(_rctx)) {\n";
	out << "\t\t\t\tif (_rctx.error)\n";
	out << "\t\t\t\t\t_rctx.session->send(_rctx.session->make_error_response(*_rctx.error));\n";
	out << "\t\t\t\telse\n";
	out << "\t\t\t\t\t_rctx.session->send(_rctx.session->make_response(500));\n";
	out << "\t\t\t\treturn;\n";
		out << "\t\t\t}\n";
		out << "\t\t\tauto _s = std::move(_rctx.session);\n";
		out << "\t\t\ttry {\n";
		out << "\t\t\t\t(this->*(fn))(req, _s);\n";
		out << "\t\t\t} catch (const std::exception& e) {\n";
		out << "\t\t\t\t_s->send(_s->make_error_response(\n";
		out << "\t\t\t\t\tsiesta::Error{siesta::ErrorCode::INVALID_ARGUMENT, \"invalid request\"}));\n";
		out << "\t\t\t} catch (...) {\n";
		out << "\t\t\t\t_s->send(_s->make_error_response(\n";
		out << "\t\t\t\t\tsiesta::Error{siesta::ErrorCode::INTERNAL, \"unexpected error\"}));\n";
		out << "\t\t\t}\n";
		out << "\t\t\treturn;\n";
		out << "\t\t}\n";
		out << "\t}\n\n";
}

void emit404Fallback(std::ostream& out) {
	out << "\t// 404 Not Found\n";
	out << "\tsession->send(session->make_error_response(siesta::Error{\n";
	out << "\t\tsiesta::ErrorCode::NOT_FOUND, \"not found\"}));\n";
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

	emitTableData(out, ds);

	out << "} // anonymous namespace\n";
	out << "\n";

	emitHandleRequestBody(out, endpoints);

	out << "} // namespace " << ns << "\n";
}

} // namespace codegen
