// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "codegen_base.hpp"
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

struct ServerEndpoint {
	std::string method;
	std::string path;
	std::string path_pattern;
	std::string function_name;
	std::string summary;
};

class ServerGenerator : public ICodeGenerator {
public:
	void operator()(const CodegenArgs& args, const std::filesystem::path& output_dir) override;

private:
	std::vector<ServerEndpoint> parseEndpoints(const openapi::v3::OpenAPIv3& spec);
	void emitServerHpp(std::ostream& out, const std::vector<ServerEndpoint>& endpoints);
	void emitServerCpp(std::ostream& out, const std::vector<ServerEndpoint>& endpoints);
};

} // namespace codegen
