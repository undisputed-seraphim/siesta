#pragma once

#include "Frontend/AST.hpp"
#include "Frontend/IFrontend.hpp"
#include "IR/EndpointIR.hpp"
#include "IR/RPCIR.hpp"
#include <string>
#include <vector>

namespace codegen {

class OpenRPCFrontend : public IFrontend {
public:
	bool parse(const std::filesystem::path& input) override;

	const schema::NormalizedAST& ast() const override { return ast_; }
	const std::vector<Endpoint>& endpoints() const override { return endpoints_; }
	std::string module_name() const override { return module_name_; }

private:
	schema::NormalizedAST ast_;
	std::vector<Endpoint> endpoints_;
	std::string module_name_;
};

} // namespace codegen
