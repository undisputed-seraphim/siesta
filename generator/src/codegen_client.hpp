// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "codegen_base.hpp"
#include "openapi3.hpp"
#include "schema_ast.hpp"
#include "util.hpp"
#include <ostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace openapi::v3 {
class OpenAPIv3;
}

namespace codegen {

struct ClientParam {
	std::string name;
	std::string cpp_type;
	bool required = false;
	std::string description;
	std::string location;
	std::string schema_type;
	std::string format;
};

struct ClientEndpoint {
	std::string method;
	std::string path;
	std::string path_template;
	std::string function_name;
	std::string summary;
	std::string description;
	std::vector<ClientParam> params;
	bool has_request_body = false;
	std::string body_type;
	std::string body_content_type;
};

class ClientGenerator : public ICodeGenerator {
public:
	void operator()(const CodegenArgs& args, const std::filesystem::path& output_dir) override;

private:
	std::vector<ClientEndpoint> parseEndpoints(const openapi::v3::OpenAPIv3& spec);

	ClientParam resolveAndMapParameter(const openapi::v3::Parameter& raw_param,
	                                   const std::unordered_map<std::string, ClientParam>& fetched_params);
	std::string schemaToCppType(const openapi::v3::JsonSchema& schema);

	void emitClassHeader(std::ostream& out);
	void emitEndpoint(std::ostream& out, const ClientEndpoint& ep);
	void emitMethodSignature(std::ostream& out, const ClientEndpoint& ep);
	void emitMethodBody(std::ostream& out, const ClientEndpoint& ep);
	void generateClientHpp(std::ostream& out, const std::vector<ClientEndpoint>& endpoints);
};

} // namespace codegen
