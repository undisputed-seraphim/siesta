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

class BeastServerPythonGenerator : public ICodeGenerator {
public:
	explicit BeastServerPythonGenerator() = default;

	void operator()(const CodegenArgs& args, const std::filesystem::path& output_dir) override;

private:
	void emitServerPy(std::ostream& out, const std::vector<Endpoint>& endpoints);

	std::string module_name_;
	std::string ns_;
};

} // namespace codegen
