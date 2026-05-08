// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "codegen_base.hpp"
#include "endpoint_ir.hpp"
#include "openapi3.hpp"
#include "schema_ast.hpp"
#include "util.hpp"
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
};

} // namespace codegen
