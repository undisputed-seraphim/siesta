// SPDX-License-Identifier: Apache-2.0
/// RPC generator driver — materialises simdjson data then processes.
/// simdjson dom iterators are single-pass; all extraction must happen
/// in one sweep.  Materialise methods + schemas into plain C++ structs,
/// then parse types, build deps, and sort.

#include "Driver.hpp"

#include "Frontend/AST.hpp"
#include "Frontend/SchemaParser.hpp"
#include "Frontend/OpenRPC/openrpc.hpp"
#include "IR/DependencyGraph.hpp"
#include "IR/RPCIR.hpp"
#include "Support/Utils.hpp"
#include "Backend/Mock/MockBackend.hpp"

#include <filesystem>
#include <iostream>

using codegen::sanitize;
using codegen::escapeCppString;
using codegen::component_path;

namespace fs = std::filesystem;

namespace driver {

// ── Phase 1: Parse component schemas into NormalizedAST ─────────

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

// ── Phase 2: Materialise methods from simdjson into plain IR ────
// All simdjson DOM access happens here, in one sweep.  The result
// is plain C++ structs with no simdjson references.

static std::vector<codegen::rpc::RPCMethod> materialiseMethods(
	const openrpc::Methods& methods) {

	using codegen::rpc::RPCMethod;
	using codegen::rpc::RPCParam;
	using codegen::rpc::RPCError;

	std::vector<RPCMethod> result;

	// simdjson dom objects + arrays are single-pass.  Iterate each
	// method's keys in document order via raw dom::object, sub-arrays
	// via raw dom::array — never reuse a wrapper that has been consumed.
	const simdjson::dom::array& arr = methods;  // ListAdaptor IS a dom::array
	for (auto method_elem : arr) {
		simdjson::dom::object method_obj(method_elem);
		RPCMethod rm;
		rm.is_notification = true;   // default: absent result

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
				// result can be a ContentDescriptor or a Reference ($ref)
				simdjson::dom::object rs_obj(val);
				auto ref = rs_obj.at_key("$ref");
				if (openapi::__detail::simdjson_noerror(ref)) {
					auto ref_str = std::string(std::string_view(ref.value_unsafe()));
					auto cpath = component_path(std::string_view(ref_str));
					if (cpath.starts_with("contentDescriptors/"))
						cpath = cpath.substr(sizeof("contentDescriptors/") - 1);
					rm.result_type.name = cpath;
				} else {
					// Inline ContentDescriptor
					openrpc::ContentDescriptor rs(val);
					auto schema = rs.schema();
					if (schema.IsRef()) {
						rm.result_type.name = sanitize(component_path(schema.ref()));
					}
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

// ── Main entry point ────────────────────────────────────────────

bool generateFromOpenRPC(const fs::path& input_path,
                          const fs::path& output_path) {
	openrpc::OpenRPC spec;
	if (!spec.Load(input_path.string())) {
		std::cerr << "Failed to load " << input_path << "\n";
		return false;
	}

	// Phase 0: Materialise simdjson DOM → plain C++ containers.
	std::cout << "Phase 0: Materialising simdjson...\n";

	auto methods_raw = spec.methods();
	auto methods_vec = materialiseMethods(methods_raw);

	auto components  = spec.components();
	std::vector<std::pair<std::string, openrpc::JsonSchema>> schemas_vec;
	for (const auto& [name, schema] : components.schemas())
		schemas_vec.emplace_back(std::string(name), schema);

	std::cout << "  Schemas: " << schemas_vec.size()
	          << ", Methods: " << methods_vec.size() << "\n";

	// Phase 1: Parse component schemas into NormalizedAST
	schema::NormalizedAST ast;
	std::cout << "Phase 1: Parsing component schemas...\n";
	parseComponentSchemas(schemas_vec, ast);

	// Phase 2: Dependency graph
	std::cout << "Phase 2: Building dependency graph...\n";
	auto dep_graph = analysis::DependencyGraph::buildFromAST(ast);

	// Phase 3: Topological sort
	std::cout << "Phase 3: Topological sort...\n";
	auto order = analysis::sortTypes(ast);
	if (order.has_cycles) {
		std::cerr << "Error: Circular dependencies detected\n";
		return false;
	}

	// Phase 4: Mock backend debug output
	int notifications = 0;
	for (const auto& m : methods_vec)
		if (m.is_notification) notifications++;

	std::cout << "  Methods: " << methods_vec.size()
	          << " (" << notifications << " notifications)\n";
	std::cout << "Phase 4: Mock backend debug output.\n";
	backend::mock::debug_print(ast, methods_vec);
	return true;
}

} // namespace driver
