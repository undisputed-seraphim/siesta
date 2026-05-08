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

class BeastServerPythonGenerator : public ICodeGenerator {
public:
	explicit BeastServerPythonGenerator() = default;

	void operator()(const CodegenArgs& args, const std::filesystem::path& output_dir) override;

private:
	void emitServerPy(std::ostream& out, const std::vector<Endpoint>& endpoints);

	std::string module_name_;
};

} // namespace codegen
