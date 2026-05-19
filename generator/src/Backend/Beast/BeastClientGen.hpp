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

class BeastClientGenerator : public ICodeGenerator {
public:
	void operator()(const CodegenArgs& args, const std::filesystem::path& output_dir) override;

private:
	void emitClassHeader(std::ostream& out);
	void emitEndpoint(std::ostream& out, const Endpoint& ep);
	void emitMethodSignature(std::ostream& out, const Endpoint& ep);
	void emitMethodBody(std::ostream& out, const Endpoint& ep);
	void generateClientHpp(std::ostream& out, const std::vector<Endpoint>& endpoints);

	void emitPathParams(std::ostream& out, const std::vector<const ClientParam*>& path_params);
	void emitQueryParams(std::ostream& out, const std::vector<const ClientParam*>& params);
	void emitRequestBody(std::ostream& out, const Endpoint& ep);
	void emitHeaderParams(std::ostream& out, const std::vector<const ClientParam*>& params);

	AuthType auth_type_ = AuthType::None;
	std::string auth_member_name_;
	std::string auth_value_member_;
	std::string auth_param_name_;
	std::string ns_;
};

} // namespace codegen
