#include "openapi2.hpp"
#include "util.hpp"

using namespace std::literals;

namespace openapi {

// ExternalDocumentation
std::string_view ExternalDocumentation::description() const { return _GetValueIfExist<std::string_view>("description"); }
std::string_view ExternalDocumentation::url() const { return _GetValueIfExist<std::string_view>("url"); }

// Tag
std::string_view Tag::name() const { return _GetValueIfExist<std::string_view>("url"); }
std::string_view Tag::description() const { return _GetValueIfExist<std::string_view>("description"); }
ExternalDocumentation Tag::externalDocs() const { return _GetObjectIfExist<ExternalDocumentation>("externalDocs"); }

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
	ref.remove_prefix(def_refstr.size());
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

// Header
std::string_view Header::description() const { return _GetValueIfExist<std::string_view>("description"); }

// Response
std::string_view Response::description() const { return _GetValueIfExist<std::string_view>("description"); }
Schema Response::schema() const { return _GetObjectIfExist<Schema>("schema"); }
Response::Headers Response::headers() const { return _GetObjectIfExist<Response::Headers>("headers"); }

// Operation
Operation::Tags Operation::tags() const { return _GetObjectIfExist<Operation::Tags>("tags"); }
std::string_view Operation::summary() const { return _GetValueIfExist<std::string_view>("summary"); }
std::string_view Operation::description() const { return _GetValueIfExist<std::string_view>("description"); }
ExternalDocumentation Operation::externalDocs() const { return _GetObjectIfExist<ExternalDocumentation>("externalDocs"); }
std::string_view Operation::operationId() const { return _GetValueIfExist<std::string_view>("operationId"); }
Operation::Parameters Operation::parameters() const { return _GetObjectIfExist<Operation::Parameters>("parameters"); }
Operation::Responses Operation::responses() const { return _GetObjectIfExist<Operation::Responses>("responses"); }
bool Operation::deprecated() const { return _GetValueIfExist<bool>("deprecated"); }

// Info
std::string_view Info::title() const { return _GetValueIfExist<std::string_view>("title"); }
std::string_view Info::description() const { return _GetValueIfExist<std::string_view>("description"); }
std::string_view Info::terms_of_service() const { return _GetValueIfExist<std::string_view>("terms_of_service"); }
std::string_view Info::version() const { return _GetValueIfExist<std::string_view>("version"); }

// Root
OpenAPI2::OpenAPI2(OpenAPI2&& other) noexcept
	: OpenAPIObject(std::move(other))
	, _parser(std::move(other._parser)) {}

std::string_view OpenAPI2::swagger() const { return _GetObjectIfExist<std::string_view>("swagger"); }
Info OpenAPI2::info() const { return _GetObjectIfExist<Info>("info"); }
std::string_view OpenAPI2::host() const { return _GetObjectIfExist<std::string_view>("host"); }
std::string_view OpenAPI2::basePath() const { return _GetObjectIfExist<std::string_view>("basePath"); }
OpenAPI2::Paths OpenAPI2::paths() const { return _GetObjectIfExist<OpenAPI2::Paths>("paths"); }
OpenAPI2::Definitions OpenAPI2::definitions() const { return _GetObjectIfExist<OpenAPI2::Definitions>("definitions"); }
OpenAPI2::Parameters OpenAPI2::parameters() const { return _GetObjectIfExist<OpenAPI2::Parameters>("parameters"); }
OpenAPI2::Tags OpenAPI2::tags() const { return _GetObjectIfExist<OpenAPI2::Tags>("tags"); }
ExternalDocumentation OpenAPI2::externalDocs() const { return _GetObjectIfExist<ExternalDocumentation>("externalDocs"); }

// Should return false if JSON parsing fails or if file is not an OpenAPI swagger file.
bool OpenAPI2::Load(const std::string& path) {
	_json = _parser.load(path);
	return true;
}

Schema OpenAPI2::GetDefinedSchemaByReference(std::string_view reference) {
	for (const auto& [schemaname, schema] : definitions()) {
		if (schemaname == reference) {
			return schema;
		}
	}
	return Schema();
}

std::string SynthesizeFunctionName(std::string_view pathstr, RequestMethod verb) {
	auto name = sanitize(pathstr);
	name = std::string(RequestMethodToString(verb)) + '_' + name;
	return name;
}

} // namespace openapi
