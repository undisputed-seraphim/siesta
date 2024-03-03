// SPDX-License-Identifier: Apache-2.0
#include "openapi2.hpp"
#include "util.hpp"

using namespace std::literals;

namespace openapi::v2 {

// Object
std::string_view Object::title() const { return _GetValueIfExist<std::string_view>("title"); }
std::string_view Object::description() const { return _GetValueIfExist<std::string_view>("description"); }
Object::Enum Object::enum_() const { return _GetObjectIfExist<Object::Enum>("enum"); }

// Array
std::string_view Array::collectionFormat() const { return _GetValueIfExist<std::string_view>("collectionFormat"); }

// Item
std::string_view Item::type() const { return _GetValueIfExist<std::string_view>("type"); }
std::string_view Item::format() const { return _GetValueIfExist<std::string_view>("format"); }
std::string_view Item::collectionFormat() const { return _GetValueIfExist<std::string_view>("collectionFormat"); }
Item Item::items() const { return _GetObjectIfExist<Item>("items"); }
double Item::maximum() const { return _GetValueIfExist<double>("maximum"); }
bool Item::exclusiveMaximum() const { return _GetValueIfExist<bool>("exclusiveMaximum"); }
double Item::minimum() const { return _GetValueIfExist<double>("minimum"); }
bool Item::exclusiveMinimum() const { return _GetValueIfExist<bool>("exclusiveMinimum"); }
int64_t Item::maxLength() const { return _GetValueIfExist<int64_t>("maxLength"); }
int64_t Item::minLength() const { return _GetValueIfExist<int64_t>("minLength"); }
std::string_view Item::pattern() const { return _GetValueIfExist<std::string_view>("pattern"); }
int64_t Item::maxItems() const { return _GetValueIfExist<int64_t>("maxItems"); }
int64_t Item::minItems() const { return _GetValueIfExist<int64_t>("minItems"); }
bool Item::uniqueItems() const { return _GetValueIfExist<bool>("uniqueItems"); }
double Item::multipleOf() const { return _GetValueIfExist<double>("multipleOf"); }

bool Item::IsReference() const noexcept { return !_GetValueIfExist<std::string_view>("$ref").empty(); }
std::string_view Item::reference() const {
	constexpr auto def_refstr = "#/definitions/"sv;
	auto ref = _GetValueIfExist<std::string_view>("$ref");
	if (ref.empty()) {
		return ref;
	}
	if (ref.starts_with(def_refstr)) {
		ref.remove_prefix(def_refstr.size());
	}
	return ref;
}

// Schema
std::string_view Schema::title() const { return _GetValueIfExist<std::string_view>("title"); }
std::string_view Schema::description() const { return _GetValueIfExist<std::string_view>("description"); }
int64_t Schema::maxProperties() const { return _GetValueIfExist<int64_t>("maxProperties"); }
int64_t Schema::minProperties() const { return _GetValueIfExist<int64_t>("minProperties"); }
bool Schema::required() const { return _GetValueIfExist<bool>("required"); }
Schema::Enum Schema::enum_() const { return _GetObjectIfExist<Schema::Enum>("enum"); }
Schema::NestedSchema Schema::properties() const { return _GetObjectIfExist<Schema::NestedSchema>("properties"); }

// Parameter
std::string_view Parameter::name() const { return _GetValueIfExist<std::string_view>("name"); }
std::string_view Parameter::in() const { return _GetValueIfExist<std::string_view>("in"); }
std::string_view Parameter::description() const { return _GetValueIfExist<std::string_view>("description"); }
bool Parameter::required() const { return _GetValueIfExist<bool>("required"); }
Schema Parameter::schema() const { return _GetObjectIfExist<Schema>("schema"); }

Schema BodyParameter::schema() const { return _GetObjectIfExist<Schema>("schema"); }

Parameter::Location Parameter::In() const noexcept {
	const auto sv = in();
	if (sv == "path") {
		return Location::path;
	}
	if (sv == "query") {
		return Location::query;
	}
	if (sv == "header") {
		return Location::header;
	}
	if (sv == "body") {
		return Location::body;
	}
	if (sv == "form") {
		return Location::form;
	}
	return Location::unknown;
}

// Header
std::string_view Header::description() const { return _GetValueIfExist<std::string_view>("description"); }

// Response
std::string_view Response::description() const { return _GetValueIfExist<std::string_view>("description"); }
Schema Response::schema() const { return _GetObjectIfExist<Schema>("schema"); }
Response::Headers Response::headers() const { return _GetObjectIfExist<Response::Headers>("headers"); }

// Operation
Operation::Parameters Operation::parameters() const { return _GetObjectIfExist<Operation::Parameters>("parameters"); }
Operation::Responses Operation::responses() const { return _GetObjectIfExist<Operation::Responses>("responses"); }
Operation::Consumes Operation::consumes() const { return _GetObjectIfExist<Operation::Tags>("consumes"); }
Operation::Produces Operation::produces() const { return _GetObjectIfExist<Operation::Tags>("produces"); }

// Root

std::string_view OpenAPIv2::swagger() const { return _GetObjectIfExist<std::string_view>("swagger"); }
Info OpenAPIv2::info() const { return _GetObjectIfExist<Info>("info"); }
std::string_view OpenAPIv2::host() const { return _GetObjectIfExist<std::string_view>("host"); }
std::string_view OpenAPIv2::basePath() const { return _GetObjectIfExist<std::string_view>("basePath"); }
OpenAPIv2::Paths OpenAPIv2::paths() const { return _GetObjectIfExist<OpenAPIv2::Paths>("paths"); }
OpenAPIv2::Definitions OpenAPIv2::definitions() const {
	return _GetObjectIfExist<OpenAPIv2::Definitions>("definitions");
}
OpenAPIv2::Parameters OpenAPIv2::parameters() const { return _GetObjectIfExist<OpenAPIv2::Parameters>("parameters"); }
Tags OpenAPIv2::tags() const { return _GetObjectIfExist<Tags>("tags"); }
ExternalDocumentation OpenAPIv2::externalDocs() const {
	return _GetObjectIfExist<ExternalDocumentation>("externalDocs");
}

Schemas OpenAPIv2::def2() const { return _GetObjectIfExist<Schemas>("definitions"); }

Schema OpenAPIv2::GetDefinedSchemaByReference(std::string_view reference) {
	for (const auto& [schemaname, schema] : definitions()) {
		if (schemaname == reference) {
			return schema;
		}
	}
	return Schema();
}

} // namespace openapi::v2
