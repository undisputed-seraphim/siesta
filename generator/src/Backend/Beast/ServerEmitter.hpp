// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "IR/EndpointIR.hpp"
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace codegen {

void emitServerHpp(std::ostream& out, const std::vector<Endpoint>& endpoints, std::string_view ns);
void emitServerCpp(std::ostream& out, const std::vector<Endpoint>& endpoints, std::string_view ns);

} // namespace codegen
