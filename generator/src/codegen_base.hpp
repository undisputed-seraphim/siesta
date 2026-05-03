// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "dependency_graph.hpp"
#include "schema_ast.hpp"
#include <filesystem>

namespace openapi::v3 {
class OpenAPIv3;
}

namespace codegen {

struct CodegenArgs {
	const schema::NormalizedAST& ast;
	const analysis::TopologicalOrder& order;
	const openapi::v3::OpenAPIv3* spec = nullptr;
};

class ICodeGenerator {
public:
	virtual ~ICodeGenerator() = default;
	virtual void operator()(const CodegenArgs& args, const std::filesystem::path& output_dir) = 0;
};

} // namespace codegen
