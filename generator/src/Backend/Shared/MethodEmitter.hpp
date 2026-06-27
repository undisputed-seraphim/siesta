// SPDX-License-Identifier: Apache-2.0
#pragma once
/// Transport-agnostic method body emitters.
/// Shared across all codegen backends (beast, nghttp2, nghttp3).

#include "IR/EndpointIR.hpp"
#include <ostream>
#include <string_view>
#include <vector>

namespace codegen {

// C++ function signature from Endpoint params (transport-independent).
// completion_token_type defaults to "outcome_type" for beast; backends
// with a different outcome type pass it explicitly.
void emitMethodSignature(std::ostream& out, const Endpoint& ep,
                         std::string_view completion_token_type = "outcome_type");

// Path template {} replacement — pure string_view manipulation.
void emitPathParams(std::ostream& out, const std::vector<const ClientParam*>& path_params);

// Query string building — appends to local variable `query`.
// Expects caller to have declared: std::string query; auto _sep = [...]();
void emitQueryParams(std::ostream& out, const std::vector<const ClientParam*>& params);

} // namespace codegen
