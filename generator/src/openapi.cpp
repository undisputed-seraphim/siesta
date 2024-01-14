#include "openapi.hpp"
#include "util.hpp"

namespace openapi {

RequestMethod RequestMethodFromString(std::string_view key) {
	if (key == "post" || key == "POST") {
		return RequestMethod::POST;
	}
	if (key == "put" || key == "PUT") {
		return RequestMethod::PUT;
	}
	if (key == "get" || key == "GET") {
		return RequestMethod::GET;
	}
	if (key == "delete" || key == "DELETE") {
		return RequestMethod::DELETE;
	}
	if (key == "patch" || key == "PATCH") {
		return RequestMethod::PATCH;
	}
	if (key == "head" || key == "HEAD") {
		return RequestMethod::HEAD;
	}
	if (key == "connect" || key == "CONNECT") {
		return RequestMethod::CONNECT;
	}
	if (key == "options" || key == "OPTIONS") {
		return RequestMethod::OPTIONS;
	}
	if (key == "trace" || key == "TRACE") {
		return RequestMethod::TRACE;
	}
	return RequestMethod::UNKNOWN;
}

std::string_view RequestMethodToString(RequestMethod rm) {
	switch (rm) {
	case RequestMethod::CONNECT:
		return "connect";
	case RequestMethod::DELETE:
		return "delete";
	case RequestMethod::GET:
		return "get";
	case RequestMethod::HEAD:
		return "head";
	case RequestMethod::OPTIONS:
		return "options";
	case RequestMethod::PATCH:
		return "patch";
	case RequestMethod::POST:
		return "post";
	case RequestMethod::PUT:
		return "put";
	case RequestMethod::TRACE:
		return "trace";
	default:
		break;
	}
	return "unknown";
}

// Only for simple types.
std::string_view JsonTypeToCppType(std::string_view type, std::string_view format) {
	if (type == "string") {
		return "std::string";
	}
	if (type == "number") {
		return (format == "double") ? "double" : "float";
	}
	if (type == "boolean") {
		return "bool";
	}
	if (type == "integer") {
		return (format == "int64") ? "int64_t" : "int32_t";
	}
	return "std::any"; // Unknown type (possibly 'object')
};

std::string SynthesizeFunctionName(std::string_view pathstr, RequestMethod verb) {
	auto name = sanitize(pathstr);
	std::replace_if(
		name.begin(), name.end(), [](char c) { return c == '{' || c == '}'; }, '_');
	return (std::string(RequestMethodToString(verb)) + '_' + name);
}

namespace json_schema {

JsonSchema::Type JsonSchema::Type_() const noexcept {
	const auto sv = type();
	if (sv == "string") {
		return Type::string;
	}
	if (sv == "number") {
		return Type::number;
	}
	if (sv == "integer") {
		return Type::integer;
	}
	if (sv == "boolean") {
		return Type::boolean;
	}
	if (sv == "object") {
		return Type::object;
	}
	if (sv == "array") {
		return Type::array;
	}
	return Type::unknown;
}

// JsonSchema
std::string_view JsonSchema::type() const { return _GetValueIfExist<std::string_view>("type"); }
std::string_view JsonSchema::name() const { return _GetValueIfExist<std::string_view>("name"); }
std::string_view JsonSchema::format() const { return _GetValueIfExist<std::string_view>("format"); }
std::string_view JsonSchema::example() const { return _GetValueIfExist<std::string_view>("example"); }

bool JsonSchema::IsPrimitive(Type type) noexcept {
	switch (type) {
	case Type::string:
	case Type::number:
	case Type::integer:
	case Type::boolean:
		return true;
	default:
		break;
	}
	return false;
}
bool JsonSchema::IsPrimitive(const JsonSchema& schema) noexcept { return IsPrimitive(schema.Type_()); }
JsonSchema::SchemaList JsonSchema::anyOf() const { return _GetObjectIfExist<JsonSchema::SchemaList>("anyOf"); }
JsonSchema::SchemaList JsonSchema::oneOf() const { return _GetObjectIfExist<JsonSchema::SchemaList>("oneOf"); }

// String
uint64_t String::minLength() const { return _GetValueIfExist<uint64_t>("minLength"); }
uint64_t String::maxLength() const { return _GetValueIfExist<uint64_t>("maxLength"); }
std::string_view String::pattern() const { return _GetValueIfExist<std::string_view>("pattern"); }

// Number
int64_t Number::maximum() const { return _GetValueIfExist<int64_t>("maximum"); }
bool Number::exclusiveMaximum() const { return _GetValueIfExist<bool>("exclusiveMaximum"); }
int64_t Number::minimum() const { return _GetValueIfExist<int64_t>("minimum"); }
bool Number::exclusiveMinimum() const { return _GetValueIfExist<bool>("exclusiveMinimum"); }
int64_t Number::multipleOf() const { return _GetValueIfExist<int64_t>("multipleOf"); }

// Object
uint64_t Object::minProperties() const { return _GetValueIfExist<uint64_t>("minProperties"); }
uint64_t Object::maxProperties() const { return _GetValueIfExist<uint64_t>("maxProperties"); }
bool Object::required() const { return _GetValueIfExist<bool>("required"); }
Object::Properties Object::properties() const { return _GetObjectIfExist<Object::Properties>("properties"); }

// Array
Array::Items Array::items() const { return _GetObjectIfExist<Array::Items>("items"); }
uint64_t Array::minItems() const { return _GetValueIfExist<uint64_t>("minItems"); }
uint64_t Array::maxItems() const { return _GetValueIfExist<uint64_t>("maxItems"); }
uint64_t Array::minContains() const { return _GetValueIfExist<uint64_t>("minContains"); }
uint64_t Array::maxContains() const { return _GetValueIfExist<uint64_t>("maxContains"); }
bool Array::uniqueItems() const { return _GetValueIfExist<bool>("uniqueItems"); }

} // namespace json_schema

namespace common {

// ExternalDocumentation
std::string_view ExternalDocumentation::description() const {
	return _GetValueIfExist<std::string_view>("description");
}
std::string_view ExternalDocumentation::url() const { return _GetValueIfExist<std::string_view>("url"); }

// Contact
std::string_view Contact::name() const { return _GetValueIfExist<std::string_view>("name"); }
std::string_view Contact::url() const { return _GetValueIfExist<std::string_view>("url"); }
std::string_view Contact::email() const { return _GetValueIfExist<std::string_view>("email"); }

// License
std::string_view License::name() const { return _GetValueIfExist<std::string_view>("name"); }
std::string_view License::identifier() const { return _GetValueIfExist<std::string_view>("identifier"); }
std::string_view License::url() const { return _GetValueIfExist<std::string_view>("url"); }

// Info
std::string_view Info::title() const { return _GetValueIfExist<std::string_view>("title"); }
std::string_view Info::description() const { return _GetValueIfExist<std::string_view>("description"); }
std::string_view Info::termsOfService() const { return _GetValueIfExist<std::string_view>("termsOfService"); }
Contact Info::contact() const { return _GetObjectIfExist<Contact>("contact"); }
License Info::license() const { return _GetObjectIfExist<License>("license"); }
std::string_view Info::version() const { return _GetValueIfExist<std::string_view>("version"); }

// SecuritySchema
std::string_view SecuritySchema::type() const { return _GetValueIfExist<std::string_view>("type"); }
std::string_view SecuritySchema::description() const { return _GetValueIfExist<std::string_view>("description"); }
std::string_view SecuritySchema::name() const { return _GetValueIfExist<std::string_view>("name"); }
std::string_view SecuritySchema::in() const { return _GetValueIfExist<std::string_view>("in"); }

// Tag
std::string_view Tag::name() const { return _GetValueIfExist<std::string_view>("name"); }
std::string_view Tag::description() const { return _GetValueIfExist<std::string_view>("description"); }
ExternalDocumentation Tag::externalDocs() const { return _GetObjectIfExist<ExternalDocumentation>("externalDocs"); }

// Parameter
std::string_view Parameter::name() const { return _GetValueIfExist<std::string_view>("name"); }
std::string_view Parameter::in() const { return _GetValueIfExist<std::string_view>("in"); }
std::string_view Parameter::description() const { return _GetValueIfExist<std::string_view>("description"); }
bool Parameter::required() const { return _GetValueIfExist<bool>("required"); }

// Operation
Operation::Tags Operation::tags() const { return _GetObjectIfExist<Operation::Tags>("tags"); }
std::string_view Operation::summary() const { return _GetValueIfExist<std::string_view>("summary"); }
std::string_view Operation::description() const { return _GetValueIfExist<std::string_view>("description"); }
ExternalDocumentation Operation::externalDocs() const {
	return _GetObjectIfExist<ExternalDocumentation>("externalDocs");
}
std::string_view Operation::operationId() const { return _GetValueIfExist<std::string_view>("operationId"); }
bool Operation::deprecated() const { return _GetValueIfExist<bool>("deprecated"); }

} // namespace common

OpenAPI::OpenAPI() noexcept {}

OpenAPI::OpenAPI(OpenAPI&& other) noexcept
	: Object(std::move(other))
	, _parser(std::move(other._parser)) {}

// Should return false if JSON parsing fails or if file is not an OpenAPI swagger file.
bool OpenAPI::Load(const std::string& path) noexcept {
	_json = _parser.load(path).get_object();
	return true;
}

int OpenAPI::MajorVersion() const noexcept {
	auto version = _GetObjectIfExist<std::string_view>("swagger");
	if (!version.empty()) {
		return std::atoi(&version[0]);
	}
	version = _GetObjectIfExist<std::string_view>("openapi");
	if (!version.empty()) {
		return std::atoi(&version[0]);
	}
	return -1;
}

} // namespace openapi