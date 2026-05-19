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

class BeastPythonGenerator : public ICodeGenerator {
public:
	explicit BeastPythonGenerator() = default;

	void operator()(const CodegenArgs& args, const std::filesystem::path& output_dir) override;

private:
	void emitModulePreamble(std::ostream& out, const std::string& module_name);
	void emitModuleBody(std::ostream& out, const std::vector<Endpoint>& endpoints);
	void emitEndpointWrapper(std::ostream& out, const Endpoint& ep, bool is_last);

	std::string module_name_;
	std::string ns_;
};

} // namespace codegen
