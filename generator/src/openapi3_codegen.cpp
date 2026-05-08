// SPDX-License-Identifier: Apache-2.0
#include "openapi3_codegen.hpp"
#include "codegen_base.hpp"
#include "beast/codegen_client.hpp"
#include "codegen_defs.hpp"
#include "beast/codegen_python.hpp"
#include "beast/codegen_server.hpp"
#include "beast/codegen_server_python.hpp"
#include "endpoint_ir.hpp"

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

static schema::NormalizedAST buildAST(const openapi::v3::OpenAPIv3& spec) {
	schema::NormalizedAST ast;

	std::cout << "Phase 1: Building normalized AST...\n";
	parseSchemas(spec, ast);

	auto endpoint_count = parsePaths(spec, ast);
	std::cout << "  Path endpoints: " << endpoint_count << "\n";

	return ast;
}

bool generateFromOpenAPI(const fs::path& input_path, const fs::path& output_path, GenMode mode, bool python, const std::string& backend) {
	if (backend != "beast") {
		std::cerr << "Unsupported backend '" << backend << "'. Only 'beast' is available.\n";
		return false;
	}
	openapi::OpenAPI file;
	if (!file.Load(input_path.string())) {
		std::cerr << "Failed to load " << input_path << "\n";
		return false;
	}

	if (file.MajorVersion() != 3) {
		std::cerr << "Only OpenAPI v3 is supported\n";
		return false;
	}

	const auto& spec = static_cast<const openapi::v3::OpenAPIv3&>(file);

	auto ast = buildAST(spec);

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

	// Parse endpoints once — shared by all backends
	bool gen_client = (mode == GenMode::client || mode == GenMode::both);
	bool gen_server = (mode == GenMode::server || mode == GenMode::both);
	auto endpoints = gen_client || gen_server
	                     ? ::codegen::parseEndpoints(spec)
	                     : std::vector<::codegen::Endpoint>{};

	std::cout << "Phase 4: Generating C++ code...\n";

	std::string title = std::string(spec.info().title());
	std::string module_name = sanitize(std::string_view(title));
	if (module_name.empty()) module_name = "siesta_bindings";
	std::string server_module = module_name + "_server";
	std::string client_mod = module_name;
	std::string server_mod = server_module;

	::codegen::CodegenArgs args{ast, order, &spec, std::move(module_name), &endpoints};

	::codegen::DefsGenerator{}(args, output_path);

	if (gen_client)
		::codegen::BeastClientGenerator{}(args, output_path);

	if (python && gen_client)
		::codegen::BeastPythonGenerator{}(args, output_path);

	if (gen_server)
		::codegen::BeastServerGenerator{}(args, output_path);

	if (python && gen_server) {
		::codegen::CodegenArgs server_args{ast, order, &spec, std::move(server_module), &endpoints};
		::codegen::BeastServerPythonGenerator{}(server_args, output_path);
	}

	// Write CMake info fragment for consumers
	{
		auto cmake_path = output_path / "siesta_info.cmake";
		std::ofstream cmf(cmake_path);
		if (cmf) {
			if (python && gen_client)
				cmf << "set(SIESTA_CLIENT_MODULE \"" << client_mod << "\")\n";
			if (python && gen_server)
				cmf << "set(SIESTA_SERVER_MODULE \"" << server_mod << "\")\n";
		}
	}

	std::cout << "Code generation complete!\n";
	std::cout << "  - AST built with " << ast.getTypes().size() << " types\n";
	std::cout << "  - Dependency graph has " << dep_graph.getDependencies().size() << " edges\n";
	std::cout << "  - Topological sort: " << (order.isValid() ? "SUCCESS" : "FAILED") << "\n";
	std::cout << "  - Ordered types: " << order.ordered_types.size() << "\n";

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
