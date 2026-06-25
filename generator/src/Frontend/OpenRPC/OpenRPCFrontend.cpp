#include "Frontend/OpenRPC/OpenRPCFrontend.hpp"

#include "Frontend/OpenRPC/openrpc.hpp"
#include "Frontend/AST.hpp"
#include "Frontend/SchemaParser.hpp"
#include "IR/RPCEndpointBridge.hpp"
#include "Support/Utils.hpp"
#include <iostream>

using codegen::sanitize;
using codegen::component_path;

namespace codegen {

bool OpenRPCFrontend::parse(const std::filesystem::path& input) {
	openrpc::OpenRPC spec;
	if (!spec.Load(input.string())) return false;

	auto methods_raw = spec.methods();

	// Materialise methods from simdjson DOM → RPCMethod IR
	std::vector<rpc::RPCMethod> rpc_methods;

	const simdjson::dom::array& arr = methods_raw;
	for (auto method_elem : arr) {
		simdjson::dom::object method_obj(method_elem);
		rpc::RPCMethod rm;
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
					rpc::RPCParam rp;
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
					rpc::RPCError re;
					re.code    = ed.code();
					re.message = std::string(ed.message());
					rm.errors.push_back(re);
				}
			}
		}

		rpc_methods.push_back(std::move(rm));
	}

	// Parse component schemas
	auto components = spec.components();
	std::unordered_set<std::string> added_types;
	for (const auto& [name, schema] : components.schemas()) {
		std::string safe_name = sanitize(std::string_view(std::string(name)));
		try {
			auto type = schema::SchemaParser::parseSchema(
				schema, safe_name, ast_, added_types);
			ast_.addType(safe_name, std::move(type));
		} catch (const std::exception& e) {
			std::cerr << "  Warning: Failed to parse " << std::string(name)
			          << ": " << e.what() << "\n";
		}
	}

	// Bridge to endpoints
	rpcToEndpoints(rpc_methods, endpoints_);

	std::string title = std::string(spec.info().title());
	module_name_ = sanitize(std::string_view(title));
	if (module_name_.empty()) module_name_ = "siesta_bindings";

	return true;
}

} // namespace codegen
