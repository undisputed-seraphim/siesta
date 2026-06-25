// SPDX-License-Identifier: Apache-2.0

#include "Driver.hpp"

#include "Frontend/AST.hpp"
#include "Frontend/SchemaParser.hpp"
#include "Frontend/OpenRPC/openrpc.hpp"
#include "Frontend/Proto/Parser.hpp"
#include "Frontend/Proto/ProtoBridge.hpp"
#include "IR/DependencyGraph.hpp"
#include "IR/RPCIR.hpp"
#include "Support/Utils.hpp"
#include "Backend/Mock/MockBackend.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

using codegen::sanitize;
using codegen::escapeCppString;
using codegen::component_path;

namespace fs = std::filesystem;

namespace driver {

// ══════════════════════════════════════════════════════════════════════
//  Shared phases: deps + sort + mock dump
// ══════════════════════════════════════════════════════════════════════

static void runDebugPipeline(
	schema::NormalizedAST& ast,
	const std::vector<codegen::rpc::RPCMethod>& methods) {

	auto dep_graph = analysis::DependencyGraph::buildFromAST(ast);
	auto order = analysis::sortTypes(ast);
	if (order.has_cycles) {
		std::cerr << "Error: Circular dependencies detected\n";
		for (auto& cycle : order.cycles) {
			std::cerr << "  Cycle:";
			for (auto& t : cycle) std::cerr << " " << t;
			std::cerr << "\n";
		}
	}

	int notifications = 0;
	for (auto& m : methods) if (m.is_notification) notifications++;

	std::cout << "  Methods: " << methods.size()
	          << " (" << notifications << " notifications)\n";
	std::cout << "Phase 4: Mock backend debug output.\n";
	backend::mock::debug_print(ast, methods);
}

// ══════════════════════════════════════════════════════════════════════
//  OpenRPC path (existing)
// ══════════════════════════════════════════════════════════════════════

static void parseComponentSchemas(
	const std::vector<std::pair<std::string, openrpc::JsonSchema>>& schemas,
	schema::NormalizedAST& ast) {
	std::unordered_set<std::string> added_types;
	int struct_count = 0;

	for (const auto& [name, schema_obj] : schemas) {
		std::string safe_name = sanitize(std::string_view(name));
		std::cout << "  Parsing schema: " << name << " -> " << safe_name << "\n";

		try {
			auto type = schema::SchemaParser::parseSchema(
				schema_obj, safe_name, ast, added_types);
			ast.addType(safe_name, std::move(type));
			struct_count++;
		} catch (const std::exception& e) {
			std::cerr << "    Warning: Failed to parse " << name << ": "
			          << e.what() << "\n";
		}
	}

	std::cout << "  Component schemas: " << struct_count << " types\n";
}

static std::vector<codegen::rpc::RPCMethod> materialiseMethods(
	const openrpc::Methods& methods) {

	using codegen::rpc::RPCMethod;
	using codegen::rpc::RPCParam;
	using codegen::rpc::RPCError;

	std::vector<RPCMethod> result;

	const simdjson::dom::array& arr = methods;
	for (auto method_elem : arr) {
		simdjson::dom::object method_obj(method_elem);
		RPCMethod rm;
		rm.is_notification = true;

		for (auto [key, val] : method_obj) {
			if (key == "name")
				rm.name = std::string(std::string_view(val));
			else if (key == "summary")
				rm.summary = std::string(std::string_view(val));
			else if (key == "description")
				rm.description = std::string(std::string_view(val));
			else if (key == "paramStructure")
				rm.param_structure = std::string(std::string_view(val));
			else if (key == "deprecated")
				rm.deprecated = bool(val);
			else if (key == "params") {
				for (auto p_elem : simdjson::dom::array(val)) {
					openrpc::ContentDescriptor p(p_elem);
					RPCParam rp;
					rp.name        = std::string(p.name());
					rp.description = std::string(p.description());
					rp.required    = p.required();
					rp.deprecated  = p.deprecated();
					auto schema = p.schema();
					if (schema.IsRef()) {
						auto ref = component_path(schema.ref());
						rp.cpp_type = sanitize(std::string_view(ref));
						rp.schema_ref.name = sanitize(std::string_view(ref));
						rp.schema_ref.is_inline = false;
					} else {
						rp.cpp_type = "std::string";
						rp.schema_ref.name = rp.cpp_type;
						rp.schema_ref.is_inline = true;
					}
					rm.params.push_back(std::move(rp));
				}
			} else if (key == "result") {
				rm.is_notification = false;
				simdjson::dom::object rs_obj(val);
				auto ref = rs_obj.at_key("$ref");
				if (openapi::__detail::simdjson_noerror(ref)) {
					auto ref_str = std::string(std::string_view(ref.value_unsafe()));
					auto cpath = component_path(std::string_view(ref_str));
					if (cpath.starts_with("contentDescriptors/"))
						cpath = cpath.substr(sizeof("contentDescriptors/") - 1);
					rm.result_type.name = cpath;
				} else {
					openrpc::ContentDescriptor rs(val);
					auto schema = rs.schema();
					if (schema.IsRef())
						rm.result_type.name = sanitize(component_path(schema.ref()));
				}
			} else if (key == "errors") {
				for (auto e_elem : simdjson::dom::array(val)) {
					openrpc::ErrorDef ed(e_elem);
					RPCError re;
					re.code    = ed.code();
					re.message = std::string(ed.message());
					rm.errors.push_back(re);
				}
			}
		}

		result.push_back(std::move(rm));
	}

	return result;
}

bool generateFromOpenRPC(const fs::path& input_path,
                          const fs::path& /*output_path*/) {
	openrpc::OpenRPC spec;
	if (!spec.Load(input_path.string())) {
		std::cerr << "Failed to load " << input_path << "\n";
		return false;
	}

	std::cout << "Phase 0: Materialising simdjson...\n";

	auto methods_raw = spec.methods();
	auto methods_vec = materialiseMethods(methods_raw);

	auto components  = spec.components();
	std::vector<std::pair<std::string, openrpc::JsonSchema>> schemas_vec;
	for (const auto& [name, schema] : components.schemas())
		schemas_vec.emplace_back(std::string(name), schema);

	std::cout << "  Schemas: " << schemas_vec.size()
	          << ", Methods: " << methods_vec.size() << "\n";

	schema::NormalizedAST ast;
	std::cout << "Phase 1: Parsing component schemas...\n";
	parseComponentSchemas(schemas_vec, ast);

	std::cout << "Phase 2: Building dependency graph...\n";
	std::cout << "Phase 3: Topological sort...\n";

	runDebugPipeline(ast, methods_vec);
	return true;
}

// ══════════════════════════════════════════════════════════════════════
//  Proto path (new)
// ══════════════════════════════════════════════════════════════════════

bool generateFromProto(const fs::path& input_path,
                        const fs::path& /*output_path*/) {
	std::ifstream in(input_path);
	if (!in) {
		std::cerr << "Failed to open " << input_path << "\n";
		return false;
	}

	std::ostringstream oss;
	oss << in.rdbuf();
	auto source = oss.str();

	std::cout << "Phase 0: Parsing proto3 source...\n";
	auto result = siesta::protobuf::parse_proto(source);
	if (!result.ok) {
		std::cerr << result.error << "\n";
		return false;
	}

	auto& pf = result.file;
	std::cout << "  Package: " << pf.package << "\n";
	std::cout << "  Messages: " << pf.messages.size()
	          << ", Enums: " << pf.enums.size()
	          << ", Services: " << pf.services.size() << "\n";

	std::cout << "Phase 1: Converting to AST + RPC IR...\n";
	schema::NormalizedAST ast;
	std::vector<codegen::rpc::RPCMethod> methods;
	proto::convertFile(pf, ast, methods);
	std::cout << "  AST types: " << ast.getTypes().size()
	          << ", RPC methods: " << methods.size() << "\n";

	std::cout << "Phase 2: Building dependency graph...\n";
	std::cout << "Phase 3: Topological sort...\n";

	runDebugPipeline(ast, methods);
	return true;
}

} // namespace driver
