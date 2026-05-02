// SPDX-License-Identifier: Apache-2.0
#include "openapi3_codegen.hpp"
#include "codegen_defs.hpp"
#include "dependency_graph.hpp"
#include "openapi.hpp"
#include "openapi3.hpp"
#include "schema_ast.hpp"
#include "schema_parser.hpp"
#include "util.hpp"
#include <boost/json/serialize.hpp>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;
namespace openapi::v3::codegen {

/**
 * Build normalized AST from OpenAPI v3 spec
 */
static schema::NormalizedAST buildAST(const openapi::v3::OpenAPIv3& spec) {
	schema::NormalizedAST ast;
	std::unordered_set<std::string> added_types;

	// Process components/schemas
	for (const auto& [name_sv, schema_obj] : spec.components().schemas()) {
		std::string name(name_sv);
		std::string safe_name = sanitize(std::string_view(name));
		std::cout << "  Parsing schema: " << name << " -> " << safe_name << "\n";

		try {
			auto type = schema::SchemaParser::parseSchema(schema_obj, safe_name, ast, added_types);
			ast.addType(safe_name, std::move(type));
		} catch (const std::exception& e) {
			std::cerr << "    Warning: Failed to parse " << name << ": " << e.what() << "\n";
		}
	}

	return ast;
}

bool generateFromOpenAPI(const fs::path& input_path, const fs::path& output_path) {
	// Load OpenAPI spec using the base loader
	openapi::OpenAPI file;
	if (!file.Load(input_path.string())) {
		std::cerr << "Failed to load " << input_path << "\n";
		return false;
	}

	// Check version and cast appropriately
	if (file.MajorVersion() != 3) {
		std::cerr << "Only OpenAPI v3 is supported\n";
		return false;
	}

	const auto& spec = static_cast<const openapi::v3::OpenAPIv3&>(file);

	std::cout << "Phase 1: Building normalized AST...\n";
	auto ast = buildAST(spec);

	// Validate AST
	std::cout << "Validating AST...\n";
	auto errors = ast.validate();
	if (!errors.empty()) {
		std::cerr << "AST validation errors:\n";
		for (const auto& err : errors) {
			std::cerr << "  - " << err << "\n";
		}
		return false;
	}

	std::cout << "Phase 2: Building dependency graph...\n";
	auto dep_graph = analysis::DependencyGraph::buildFromAST(ast);

	std::cout << "Phase 3: Detecting cycles and sorting...\n";
	auto order = analysis::sortTypes(ast);

	if (order.has_cycles) {
		std::cerr << "Error: Circular dependencies detected:\n";
		for (const auto& cycle : order.cycles) {
			std::cerr << "  Cycle: ";
			for (size_t i = 0; i < cycle.size(); ++i) {
				std::cerr << cycle[i];
				if (i + 1 < cycle.size())
					std::cerr << " -> ";
			}
			std::cerr << "\n";
		}
		return false;
	}

	if (!order.ordered_types.empty()) {
		std::cout << "Topological order: ";
		for (size_t i = 0; i < order.ordered_types.size(); ++i) {
			std::cout << order.ordered_types[i];
			if (i + 1 < order.ordered_types.size())
				std::cout << " -> ";
		}
		std::cout << "\n";
	}

	std::cout << "Phase 4: Generating C++ code...\n";

	// Create output directory
	fs::create_directories(output_path);

	// Generate defs.hpp and defs.cpp
	::codegen::DefsGenerator gen(order, ast);

	{
		fs::path defs_hpp = output_path / "defs.hpp";
		std::ofstream out(defs_hpp);
		if (!out) {
			std::cerr << "Failed to open " << defs_hpp << "\n";
			return false;
		}
		std::cout << "  Generating " << defs_hpp << "\n";
		gen.generateDefsHpp(out);
	}

	{
		fs::path defs_cpp = output_path / "defs.cpp";
		std::ofstream out(defs_cpp);
		if (!out) {
			std::cerr << "Failed to open " << defs_cpp << "\n";
			return false;
		}
		std::cout << "  Generating " << defs_cpp << "\n";
		gen.generateDefsCpp(out);
	}

	std::cout << "Code generation complete!\n";
	std::cout << "  - AST built with " << ast.getTypes().size() << " types\n";
	std::cout << "  - Dependency graph has " << dep_graph.getDependencies().size() << " edges\n";
	std::cout << "  - Topological sort: " << (order.isValid() ? "SUCCESS" : "FAILED") << "\n";

	return true;
}

} // namespace openapi::v3::codegen
