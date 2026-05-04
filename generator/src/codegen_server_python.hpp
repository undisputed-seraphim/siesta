// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "codegen_base.hpp"
#include "codegen_server.hpp"
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

class ServerPythonGenerator : public ICodeGenerator {
public:
	explicit ServerPythonGenerator() = default;

	void operator()(const CodegenArgs& args, const std::filesystem::path& output_dir) override;

private:
	std::vector<ServerEndpoint> parseEndpoints(const openapi::v3::OpenAPIv3& spec);
	void emitServerPy(std::ostream& out, const std::vector<ServerEndpoint>& endpoints);

	std::string module_name_;
};

} // namespace codegen
