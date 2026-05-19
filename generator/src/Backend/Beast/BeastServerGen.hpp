// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "IR/CodegenArgs.hpp"
#include "IR/EndpointIR.hpp"
#include "Frontend/openapi3.hpp"
#include "Frontend/AST.hpp"
#include "Support/Utils.hpp"
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace openapi::v3 {
class OpenAPIv3;
}

namespace codegen {

class BeastServerGenerator : public ICodeGenerator {
public:
	void operator()(const CodegenArgs& args, const std::filesystem::path& output_dir) override;

private:
	void emitServerHpp(std::ostream& out, const std::vector<Endpoint>& endpoints);
	void emitServerCpp(std::ostream& out, const std::vector<Endpoint>& endpoints);

	std::string ns_;
};

} // namespace codegen
