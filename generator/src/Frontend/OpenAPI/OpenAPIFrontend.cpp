#include "Frontend/OpenAPI/OpenAPIFrontend.hpp"

#include "Frontend/openapi.hpp"
#include "Frontend/openapi3.hpp"
#include "Frontend/AST.hpp"
#include "Frontend/SchemaParser.hpp"
#include "IR/EndpointIR.hpp"
#include "Support/Utils.hpp"
#include <iostream>

using codegen::sanitize;

namespace codegen {

bool OpenAPIFrontend::parse(const std::filesystem::path& input) {
	openapi::OpenAPI file;
	if (!file.Load(input.string())) return false;

	if (file.MajorVersion() != 3) return false;

	const auto& spec = static_cast<const openapi::v3::OpenAPIv3&>(file);

	std::unordered_set<std::string> added_types;
	for (const auto& [name_sv, schema_obj] : spec.components().schemas()) {
		std::string name(name_sv);
		std::string safe_name = sanitize(std::string_view(name));
		try {
			auto type = schema::SchemaParser::parseSchema(
				schema_obj, safe_name, ast_, added_types);
			ast_.addType(safe_name, std::move(type));
		} catch (const std::exception& e) {
			std::cerr << "  Warning: Failed to parse " << name
			          << ": " << e.what() << "\n";
		}
	}

	endpoints_ = ::codegen::parseEndpoints(spec);

	std::string title = std::string(spec.info().title());
	module_name_ = sanitize(std::string_view(title));
	if (module_name_.empty()) module_name_ = "siesta_bindings";

	return true;
}

} // namespace codegen
