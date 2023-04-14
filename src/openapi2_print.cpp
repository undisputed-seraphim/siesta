#include "openapi2.hpp"
#include "util.hpp"

// Contains all the definitions for Print() functions

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
		if (format == "double") {
			return "double";
		}
		return "float";
	}
	if (type == "boolean") {
		return "bool";
	}
	if (type == "integer") {
		if (format == "int64") {
			return "int64_t";
		}
		return "int32_t";
	}
	return "std::any"; // Unknown type (possibly 'object')
};

void ExternalDocumentation::Print(std::ostream& os, std::string_view name, std::string& indent) const {}

void Tag::Print(std::ostream& os, std::string_view name, std::string& indent) const {}

void Item::Print(std::ostream& os, std::string_view name_, std::string& indent) const {
	std::string name = sanitize(name_);
	if (IsReference()) {
		os << indent << reference() << ' ' << name << "_;\n";
	} else {
		os << indent << JsonTypeToCppType(type()) << ' ' << name << ";\n";
	}
}

void Schema::Print(std::ostream& os, std::string_view name_, std::string& indent) const {
	write_multiline_comment(os, description());
	std::string name = sanitize(name_);
	if (type() == "object") {
		os << indent << "struct " << name << " {\n";
		indent.push_back('\t');
		if (IsReference()) {
			os << indent << reference() << '\n';
		} else {
			for (const auto& [propname, property] : properties()) {
				write_multiline_comment(os, property.description(), indent);
				if (property.type() == "object") {
					property.Print(os, propname, indent);
					os << indent << propname << ' ' << propname << "_;\n";
				} else {
					static_cast<Item>(property).Print(os, propname, indent);
				}
			}
		}
		indent.pop_back();
		os << indent << "};\n";
	} else if (type() == "array") {
		auto item = items();
		if (item.IsReference()) {
			os << indent << "using " << name << " = std::vector<" << item.reference() << ">;\n";
		} else {
			os << indent << "using " << name << " = std::vector<" << JsonTypeToCppType(item.type()) << ">;\n";
		}
	}
}

void Parameter::Print(std::ostream& os, std::string_view name, std::string& indent) const {
    Item::Print(os, name, indent);
}

void Header::Print(std::ostream& os, std::string_view name, std::string& indent) const {}

void Response::Print(std::ostream& os, std::string_view name, std::string& indent) const {}

void Operation::Print(std::ostream& os, std::string_view name, std::string& indent) const {}

void Path::Print(std::ostream& os, std::string_view name, std::string& indent) const {}

void Info::Print(std::ostream& os, std::string_view name, std::string& indent) const {}

void OpenAPI2::Print(std::ostream& os, std::string_view /*ignored*/, std::string& indent) const {
	// First, print all definitions (structs)
	for (const auto& [name, property] : definitions()) {
		property.Print(os, name, indent);
		os << '\n';
	}
}

} // namespace openapi