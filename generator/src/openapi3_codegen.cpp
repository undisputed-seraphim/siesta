// SPDX-License-Identifier: Apache-2.0
#include "openapi3_codegen.hpp"
#include "codegen_base.hpp"
#include "codegen_client.hpp"
#include "codegen_defs.hpp"
#include "codegen_python.hpp"
#include "codegen_server.hpp"
#include "codegen_server_python.hpp"

using codegen::sanitize;
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
 * Parse components/schemas into the AST, counting each type.
 */
static void parseSchemas(const openapi::v3::OpenAPIv3& spec, schema::NormalizedAST& ast) {
	std::unordered_set<std::string> added_types;

	int struct_count = 0, variant_count = 0, array_count = 0, map_count = 0, enum_count = 0, prim_count = 0;

	for (const auto& [name_sv, schema_obj] : spec.components().schemas()) {
		std::string name(name_sv);
		std::string safe_name = sanitize(std::string_view(name));
		std::cout << "  Parsing schema: " << name << " -> " << safe_name << "\n";

		try {
			auto type = schema::SchemaParser::parseSchema(schema_obj, safe_name, ast, added_types);
			ast.addType(safe_name, std::move(type));

			std::visit(
				[&](const auto& t) {
					using T = std::decay_t<decltype(t)>;
					if constexpr (std::is_same_v<T, schema::StructType>)
						struct_count++;
					else if constexpr (std::is_same_v<T, schema::VariantType>)
						variant_count++;
					else if constexpr (std::is_same_v<T, schema::ArrayType>)
						array_count++;
					else if constexpr (std::is_same_v<T, schema::MapType>)
						map_count++;
					else if constexpr (std::is_same_v<T, schema::EnumType>)
						enum_count++;
					else if constexpr (std::is_same_v<T, schema::PrimitiveType>)
						prim_count++;
				},
				type);
		} catch (const std::exception& e) {
			std::cerr << "    Warning: Failed to parse " << name << ": " << e.what() << "\n";
		}
	}

	std::cout << "  AST summary: " << ast.getTypes().size() << " types (" << struct_count << " structs, "
			  << variant_count << " variants, " << array_count << " arrays, " << map_count << " maps, " << enum_count
			  << " enums, " << prim_count << " primitives)\n";
}

/**
 * Collect path/operation metadata into the AST.
 */
static int parsePaths(const openapi::v3::OpenAPIv3& spec, schema::NormalizedAST& ast) {
	int endpoint_count = 0;

	for (const auto& [path_sv, path_obj] : spec.paths()) {
		std::string path(path_sv);
		auto path_ops = path_obj.operations();

		for (const auto& [method_sv, op_obj] : path_ops) {
			std::string method(method_sv);
			std::transform(method.begin(), method.end(), method.begin(), ::tolower);

			std::string summary = std::string(op_obj.summary());
			std::string op_id;
			try {
				op_id = std::string(op_obj.operationId());
			} catch (...) {
				op_id = "";
			}

			schema::PathItem path_item;
			path_item.path = path;
			path_item.operations[method] = op_id.empty() ? summary : op_id;
			try {
				path_item.description = std::string(op_obj.description());
			} catch (...) {
				path_item.description = "";
			}

			ast.addPath(path, std::move(path_item));
			endpoint_count++;
		}
	}

	return endpoint_count;
}

/**
 * Build normalized AST from OpenAPI v3 spec
 */
static schema::NormalizedAST buildAST(const openapi::v3::OpenAPIv3& spec) {
	schema::NormalizedAST ast;

	std::cout << "Phase 1: Building normalized AST...\n";
	parseSchemas(spec, ast);

	auto endpoint_count = parsePaths(spec, ast);
	std::cout << "  Path endpoints: " << endpoint_count << "\n";

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

	::codegen::CodegenArgs args{ast, order, &spec};

	{
		::codegen::DefsGenerator gen;
		std::cout << "  Generating openapi_defs.hpp/cpp\n";
		gen(args, output_path);
	}

	{
		::codegen::ClientGenerator gen;
		std::cout << "  Generating client.hpp\n";
		gen(args, output_path);
	}

	{
		::codegen::PythonGenerator gen("siesta_bindings");
		std::cout << "  Generating py_module.cpp\n";
		gen(args, output_path);
	}

	{
		::codegen::ServerGenerator gen;
		std::cout << "  Generating server.hpp/cpp\n";
		gen(args, output_path);
	}

	{
		::codegen::ServerPythonGenerator gen("siesta_bindings");
		std::cout << "  Generating server_py.cpp\n";
		gen(args, output_path);
	}

	std::cout << "Code generation complete!\n";
	std::cout << "  - AST built with " << ast.getTypes().size() << " types\n";
	std::cout << "  - Dependency graph has " << dep_graph.getDependencies().size() << " edges\n";
	std::cout << "  - Topological sort: " << (order.isValid() ? "SUCCESS" : "FAILED") << "\n";
	std::cout << "  - Ordered types: " << order.ordered_types.size() << "\n";

	// Cross-check: how many AST types were actually emitted?
	std::unordered_set<std::string> sorted_set(order.ordered_types.begin(), order.ordered_types.end());
	int missing = 0;
	for (const auto& [name, _] : ast.getTypes()) {
		if (sorted_set.find(name) == sorted_set.end()) {
			missing++;
		}
	}
	if (missing > 0) {
		std::cerr << "  *** WARNING: " << missing << " of " << ast.getTypes().size()
				  << " AST types are NOT in topological order (will be missing from output) ***\n";
	} else {
		std::cout << "  - All " << ast.getTypes().size() << " AST types present in output\n";
	}

	return true;
}

} // namespace openapi::v3::codegen
