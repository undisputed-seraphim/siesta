#include "Driver/Driver.hpp"

#include "Frontend/IFrontend.hpp"
#include "IR/CodegenArgs.hpp"
#include "IR/DefsGenerator.hpp"
#include "IR/DependencyGraph.hpp"
#include "Backend/Beast/BeastClientGen.hpp"
#include "Backend/Beast/BeastServerGen.hpp"
#include "Backend/Beast/BeastPythonGen.hpp"
#include "Backend/Beast/BeastServerPythonGen.hpp"

#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

namespace driver {

bool generate(const fs::path& input_path,
              const fs::path& output_path,
              GenMode mode,
              bool python,
              const std::string& backend,
              const std::string& ns_override) {

	if (backend != "beast") {
		std::cerr << "Unsupported backend '" << backend << "'. Only 'beast' is available.\n";
		return false;
	}

	auto frontend = codegen::IFrontend::create(input_path);
	if (!frontend) {
		std::cerr << "Error: could not detect or parse " << input_path << "\n";
		return false;
	}

	auto& ast       = frontend->ast();
	auto& endpoints = frontend->endpoints();

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
				if (i + 1 < cycle.size()) std::cerr << " -> ";
			}
			std::cerr << "\n";
		}
		return false;
	}

	if (!order.ordered_types.empty()) {
		std::cout << "Topological order: ";
		for (size_t i = 0; i < order.ordered_types.size(); ++i) {
			std::cout << order.ordered_types[i];
			if (i + 1 < order.ordered_types.size()) std::cout << " -> ";
		}
		std::cout << "\n";
	}

	std::cout << "Phase 4: Generating C++ code...\n";

	std::string module_name = frontend->module_name();
	std::string ns = ns_override.empty() ? module_name : ns_override;
	std::string server_module = module_name + "_server";

	bool gen_client = (mode == GenMode::client || mode == GenMode::both);
	bool gen_server = (mode == GenMode::server || mode == GenMode::both);

	codegen::CodegenArgs args{ast, order, module_name, std::move(ns), &endpoints};

	codegen::DefsGenerator{}(args, output_path);

	if (gen_client)
		codegen::BeastClientGenerator{}(args, output_path);

	if (python && gen_client)
		codegen::BeastPythonGenerator{}(args, output_path);

	if (gen_server)
		codegen::BeastServerGenerator{}(args, output_path);

	if (python && gen_server) {
		codegen::CodegenArgs server_args{ast, order, server_module, args.ns, &endpoints};
		codegen::BeastServerPythonGenerator{}(server_args, output_path);
	}

	{
		auto cmake_path = output_path / "siesta_info.cmake";
		std::ofstream cmf(cmake_path);
		if (cmf) {
			if (python && gen_client)
				cmf << "set(SIESTA_CLIENT_MODULE \"" << module_name << "\")\n";
			if (python && gen_server)
				cmf << "set(SIESTA_SERVER_MODULE \"" << server_module << "\")\n";
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
		if (sorted_set.find(name) == sorted_set.end()) missing++;
	}
	if (missing > 0) {
		std::cerr << "  *** WARNING: " << missing << " of " << ast.getTypes().size()
		          << " AST types are NOT in topological order ***\n";
	} else {
		std::cout << "  - All " << ast.getTypes().size() << " AST types present in output\n";
	}

	return true;
}

} // namespace driver
