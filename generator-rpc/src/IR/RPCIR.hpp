// SPDX-License-Identifier: Apache-2.0
#pragma once
/// Shared RPC method IR — consumed by all backends.
/// Analogous to generator/src/IR/EndpointIR.hpp but for RPC methods.

#include "Frontend/AST.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace codegen::rpc {

// ── Parameter definition ────────────────────────────────────────

struct RPCParam {
	std::string name;
	std::string cpp_type;           // resolved C++ type (e.g. "std::string", "int32_t")
	std::string description;
	bool required = false;
	bool deprecated = false;
	schema::TypeRef schema_ref;     // link to component schema if $ref'd
};

// ── Error definition ────────────────────────────────────────────

struct RPCError {
	int64_t code;                  // REQUIRED integer
	std::string message;           // REQUIRED short description
	// data: unstructured (not a schema — server-defined payload)
};

// ── Streaming mode (not in OpenRPC 1.2.6 — reserved for protobuf) ──

enum class StreamingMode {
	None,               // unary call
	ServerStreaming,    // client sends one request, server sends stream
	ClientStreaming,    // client sends stream, server sends one response
	Bidirectional        // both sides stream
};

// ── Method definition ───────────────────────────────────────────

struct RPCMethod {
	std::string name;                  // RPC method name (e.g. "subtract")
	std::string summary;
	std::string description;
	std::string param_structure;       // "by-name", "by-position", or "either" (default "either")

	std::vector<RPCParam> params;
	schema::TypeRef result_type;       // empty name = notification (no result)
	bool is_notification = false;      // true when result descriptor is absent
	std::vector<RPCError> errors;
	StreamingMode streaming = StreamingMode::None;  // always None for OpenRPC
	bool deprecated = false;
};

// ── Top-level schema ────────────────────────────────────────────

struct RPCSchema {
	std::string title;
	std::string version;
	std::vector<RPCMethod> methods;

	// Component schemas are in a NormalizedAST (reused from generator/)
	// The AST is populated by the SchemaParser, then methods reference
	// types via TypeRef links back into the AST.
};

} // namespace codegen::rpc
