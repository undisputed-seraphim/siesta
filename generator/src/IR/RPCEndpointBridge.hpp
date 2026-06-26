#pragma once
/// Converts RPC method definitions into the shared Endpoint IR.
/// After this bridge, RPC endpoints flow through the same backends as REST.

#include "IR/EndpointIR.hpp"
#include "IR/RPCIR.hpp"
#include "Support/Utils.hpp"
#include <string>

namespace codegen {

namespace {
constexpr StreamingMode mapStreaming(rpc::StreamingMode m) {
	switch (m) {
	case rpc::StreamingMode::ServerStreaming: return StreamingMode::ServerStream;
	case rpc::StreamingMode::ClientStreaming: return StreamingMode::ClientStream;
	case rpc::StreamingMode::Bidirectional:   return StreamingMode::Bidirectional;
	default:                                   return StreamingMode::None;
	}
}
} // anonymous namespace

inline void rpcToEndpoints(
	const std::vector<rpc::RPCMethod>& rpcs,
	std::vector<Endpoint>& out) {

	for (auto& m : rpcs) {
		Endpoint ep;
		ep.method        = "post";
		ep.cpp_verb      = "post";
		ep.path          = "/rpc/" + m.name;
		ep.path_template = ep.path;
		ep.function_name = sanitize(std::string_view(m.name));

		ep.summary       = m.summary;
		ep.description   = m.description;
		ep.has_request_body = !m.is_notification;

		// single request parameter = union of all params
		if (!m.params.empty()) {
			ep.body_type = m.params[0].cpp_type;
		}
		ep.body_content_type = "application/json";
		ep.is_websocket  = (m.streaming != rpc::StreamingMode::None);
		ep.streaming_mode = mapStreaming(m.streaming);
		ep.auth_type     = AuthType::None;

		out.push_back(std::move(ep));
	}
}

} // namespace codegen
