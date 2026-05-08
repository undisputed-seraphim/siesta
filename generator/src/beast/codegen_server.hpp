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

class BeastServerGenerator : public ICodeGenerator {
public:
	void operator()(const CodegenArgs& args, const std::filesystem::path& output_dir) override;

private:
	void emitServerHpp(std::ostream& out, const std::vector<Endpoint>& endpoints);
	void emitServerCpp(std::ostream& out, const std::vector<Endpoint>& endpoints);
};

} // namespace codegen
