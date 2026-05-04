// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "codegen_base.hpp"
#include "codegen_client.hpp"
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

struct PyEndpoint {
	std::string method;
	std::string path;
	std::string function_name;
	std::string summary;
	std::string description;
	std::vector<ClientParam> params;
	bool has_request_body = false;
	std::string body_type;
	std::string body_content_type;
	ClientEndpoint::AuthType auth_type = ClientEndpoint::AuthType::None;
	std::string auth_header_name;
};

class PythonGenerator : public ICodeGenerator {
public:
	explicit PythonGenerator() = default;

	void operator()(const CodegenArgs& args, const std::filesystem::path& output_dir) override;

private:
	std::vector<PyEndpoint> parseEndpoints(const openapi::v3::OpenAPIv3& spec);

	ClientParam resolveAndMapParameter(const openapi::v3::Parameter& raw_param,
	                                   const std::unordered_map<std::string, ClientParam>& fetched_params);
	std::string schemaToCppType(const openapi::v3::JsonSchema& schema);

	void emitModulePreamble(std::ostream& out, const std::string& module_name);
	void emitModuleBody(std::ostream& out, const std::vector<PyEndpoint>& endpoints);
	void emitEndpointWrapper(std::ostream& out, const PyEndpoint& ep, bool is_last);

	std::string module_name_;
};

} // namespace codegen
