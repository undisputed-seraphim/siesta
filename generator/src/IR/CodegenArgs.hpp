// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "IR/DependencyGraph.hpp"
#include "IR/EndpointIR.hpp"
#include "Frontend/AST.hpp"
#include <filesystem>

namespace openapi::v3 {
class OpenAPIv3;
}

namespace codegen {

struct CodegenArgs {
	const schema::NormalizedAST& ast;
	const analysis::TopologicalOrder& order;
	const openapi::v3::OpenAPIv3* spec = nullptr;
	std::string module_name = "siesta_bindings";
	std::string ns = "api";
	const std::vector<Endpoint>* endpoints = nullptr;
};

class ICodeGenerator {
public:
	virtual ~ICodeGenerator() = default;
	virtual void operator()(const CodegenArgs& args, const std::filesystem::path& output_dir) = 0;
};

} // namespace codegen
