// SPDX-License-Identifier: Apache-2.0
#include "openapi2.hpp"
#include "util.hpp"
#include <fstream>

namespace fs = std::filesystem;

namespace openapi::v2 {

using namespace json_schema;
using Type = JsonSchema::Type;

class StructPrinter {
public:
	StructPrinter(const OpenAPIv2& file_, const std::filesystem::path& input_, const std::filesystem::path& output_)
		: file(file_)
		, input(input_)
		, output(output_)
		, hpp_out(output / (input.stem().string() + "_defs.hpp"))
		, cpp_out(output / (input.stem().string() + "_defs.cpp")) {}

	bool operator()();

private:
	const OpenAPIv2& file;
	fs::path input, output;
	std::ofstream hpp_out, cpp_out;
	std::string indent;

	// Top level prints, used in operator()
	void PrintComponentSchemas();
	void PrintComponentParameters();
	void PrintComponentResponses();
	void PrintPathSchemas();

	void PrintSchema(std::string_view name, const JsonSchema& schema);
	// void PrintParameter(const Parameter&);
	// void PrintParameter(std::string_view name, const Parameter&);

	// Detailed print impls
	Type PrintSchemaDecl(std::string_view name, const JsonSchema& schema);
	Type PrintJSONValueFromTagDecl(std::string_view name, const JsonSchema& schema);
	Type PrintJSONValueFromTagImpl(std::string_view name, const JsonSchema& schema);
	Type PrintJSONValueToTagDecl(std::string_view name, const JsonSchema& schema);
	// Type PrintJSONValueToTagImpl(std::string_view name, const JsonSchema& schema);
};

bool StructPrinter::operator()() {
	hpp_out << "// Automatically generated from " << input.string() << ". Do not modify this file.\n"
			<< "#pragma once\n"
			<< '\n'
			<< "#include <string>\n"
			<< "#include <vector>\n"
			<< "#include <boost/json.hpp>\n"
			<< '\n'
			<< "namespace openapi {\n"
			<< std::endl;

	fs::path definitions_hpp = output / (input.stem().string() + "_defs.hpp");
	cpp_out << "// Automatically generated from " << input.string() << ". Do not modify this file.\n"
			<< "#include \"" << definitions_hpp.filename().string() << "\"\n"
			<< "namespace js = ::boost::json;\n"
			<< '\n'
			<< "namespace openapi {\n"
			<< std::endl;

	for (const auto& [defname, def] : file.def2()) {
		PrintSchema(defname, def);
	}

	hpp_out << "} // namespace openapi\n";
	cpp_out << "} // namespace openapi\n";
	return true;
}

void StructPrinter::PrintSchema(std::string_view name, const JsonSchema& schema) {
	PrintSchemaDecl(name, schema);
	PrintJSONValueFromTagDecl(name, schema);
	PrintJSONValueFromTagImpl(name, schema);
	PrintJSONValueToTagDecl(name, schema);
	// PrintJSONValueToTagImpl(name, schema);
}

Type StructPrinter::PrintSchemaDecl(std::string_view name, const JsonSchema& schema) {
	struct Visitor {
		std::ostream& _os;
		std::string_view _name;
		std::string& _indent;
		bool _should_instantiate = true;

		void print_primitive(const JsonSchema& schema) const {
			write_multiline_comment(_os, schema.description(), _indent);
			_os << _indent << JsonTypeToCppType(schema.type(), schema.format()) << ' ' << _name << ";\n";
		}
		void operator()(const String& schema) const { print_primitive(schema); }
		void operator()(const Number& schema) const { print_primitive(schema); }
		void operator()(const Integer& schema) const { print_primitive(schema); }
		void operator()(const Boolean& schema) const { print_primitive(schema); }
		void operator()(const v2::Object& schema) const {
			write_multiline_comment(_os, schema.description(), _indent);
			const auto sanitized_name = sanitize(_name);
			_os << _indent << "struct " << sanitized_name << " {\n";
			_indent.push_back('\t');

			for (const auto& [propname, prop] : schema.properties()) {
				const auto sanitized_propname = sanitize(propname);
				prop.Visit(Visitor{_os, sanitized_propname, _indent});
			}

			_indent.pop_back();

			if (_indent.empty()) {
				_os << "};\n";
			} else {
				_os << _indent << '}';
				if (_should_instantiate) {
					_os << " " << sanitized_name << "_;\n";
				} else {
					_os << ";\n";
				}
			}
		}
		void operator()(const json_schema::Object& schema) const {
			return (*this)(static_cast<const v2::Object&>(schema));
		}
		void operator()(const v2::Array& schema) const {
			struct ArrayItemVisitor {
				std::ostream& _os;
				std::string_view _name;
				std::string& _indent;

				void print_primitive(const JsonSchema& schema) const {
					write_multiline_comment(_os, schema.description(), _indent);
					_os << _indent << "std::vector<" << JsonTypeToCppType(schema.type(), schema.format()) << "> "
						<< _name << ";\n";
				}
				void operator()(const String& schema) const { print_primitive(schema); }
				void operator()(const Number& schema) const { print_primitive(schema); }
				void operator()(const Integer& schema) const { print_primitive(schema); }
				void operator()(const Boolean& schema) const { print_primitive(schema); }
				void operator()(const v2::Object& schema) const {
					const auto entry_name = std::string(_name) + "_entry";
					Type visited = schema.Visit(Visitor{_os, entry_name, _indent, false});
				}
				void operator()(const json_schema::Object& schema) const {
					return (*this)(static_cast<const v2::Object&>(schema));
				}
				void operator()(const v2::Array& schema) const {
					write_multiline_comment(_os, schema.description(), _indent);
					const auto entry_name = std::string(_name) + "_entry";
					Type visited = schema.items().Visit(ArrayItemVisitor{_os, entry_name, _indent});
				}
				void operator()(const json_schema::Array& schema) const {
					return (*this)(static_cast<const v2::Array&>(schema));
				}
			};

			const auto itemschema = schema.items();
			Type type = itemschema.Visit(ArrayItemVisitor{_os, _name, _indent});
			write_multiline_comment(_os, schema.description(), _indent);
			switch (type) {
			case Type::object: {
				const auto entry_name = std::string(_name) + "_entry";
				if (_indent.empty()) {
					_os << "using " << _name << " = std::vector<" << entry_name << ">;\n";
				} else {
					_os << _indent << "std::vector<" << entry_name << "> " << _name << ";\n";
				}
				break;
			}
			case Type::array: {

				break;
			}
			default:
				break;
			}
		}
		void operator()(const json_schema::Array& schema) const {
			return (*this)(static_cast<const v2::Array&>(schema));
		}
	};
	return schema.Visit(Visitor{hpp_out, name, indent});
}

Type StructPrinter::PrintJSONValueFromTagDecl(std::string_view name, const JsonSchema& schema) {
	struct Visitor final {
		std::ostream& _os;
		std::string_view _name;

		void operator()(const v2::Object& schema) const {
			_os << "void tag_invoke(boost::json::value_from_tag, boost::json::value& jv, const " << _name << "& v);\n";
		}
		void operator()(const json_schema::Object& schema) const {
			return (*this)(static_cast<const v2::Object&>(schema));
		}
	};
	return schema.Visit(Visitor{hpp_out, sanitize(name)});
}

Type StructPrinter::PrintJSONValueFromTagImpl(std::string_view name, const JsonSchema& schema) {
	struct Visitor final {
		std::ostream& _os;
		std::string_view _parent_name;
		std::string_view _name;

		void operator()(const v2::Object& schema) const {
			const auto full_name =
				(_parent_name.empty()) ? std::string(_name) : (std::string(_parent_name) + "::" + std::string(_name));
			for (const auto& [propname, prop] : schema.properties()) {
				prop.Visit(Visitor{_os, full_name, sanitize(propname)});
			}

			_os << "void tag_invoke(boost::json::value_from_tag, boost::json::value& jv, const " << full_name
				<< "& v) {\n";
			_os << "\tjv = {\n";
			for (const auto& [propname, prop] : schema.properties()) {
				const auto sanitized_propname = sanitize(propname);
				if (prop.Type_() == Type::object) {
					_os << "\t\t{ \"" << propname << "\", js::value_from(v." << sanitized_propname
						<< "_, jv.storage()) },\n";
				} else {
					_os << "\t\t{ \"" << propname << "\", v." << sanitized_propname << " },\n";
				}
			}
			_os << "\t};\n";
			_os << "}\n";
		}
		void operator()(const json_schema::Object& schema) const {
			return (*this)(static_cast<const v2::Object&>(schema));
		}
		void operator()(const v2::Array& schema) const {
			const auto& itemschema = schema.items();
			itemschema.Visit(Visitor{_os, _parent_name, std::string(_name) + "_entry"});
		}
		void operator()(const json_schema::Array& schema) const {
			return (*this)(static_cast<const v2::Array&>(schema));
		}
	};
	return schema.Visit(Visitor{cpp_out, "", sanitize(name)});
}

Type StructPrinter::PrintJSONValueToTagDecl(std::string_view name, const JsonSchema& schema) {
	struct Visitor final {
		std::ostream& _os;
		std::string_view _name;

		void operator()(const v2::Object& schema) const {
			_os << _name << " tag_invoke(boost::json::value_to_tag<" << _name << ">, const boost::json::value& jv);\n";
		}
		void operator()(const json_schema::Object& schema) const {
			return (*this)(static_cast<const v2::Object&>(schema));
		}
	};

	return schema.Visit(Visitor{hpp_out, sanitize(name)});
}

/// OLD

void PrintReferenceSchema(
	std::ostream& os,
	std::string_view name,
	const openapi::v2::Schema& schema,
	std::string& indent);
void PrintObjectSchema(std::ostream& os, std::string_view name, const openapi::v2::Schema& schema, std::string& indent);
void PrintArraySchema(std::ostream& os, std::string_view name, const openapi::v2::Schema& schema, std::string& indent);

void PrintSchema(std::ostream& os, std::string_view name, const openapi::v2::Schema& schema, std::string& indent) {
	write_multiline_comment(os, schema.description(), indent);
	if (schema.IsReference()) {
		PrintReferenceSchema(os, name, schema, indent);
	} else if (schema.type() == "object") {
		PrintObjectSchema(os, name, schema, indent);
	} else if (schema.type() == "array") {
		PrintArraySchema(os, name, schema, indent);
	} else if (schema.type().empty()) {
		if (const auto item = schema.items(); item) {
			const auto& subschema = static_cast<const openapi::v2::Schema&>(item);
			PrintSchema(os, name, subschema, indent);
		} else {
			PrintObjectSchema(os, name, schema, indent);
		}
	} else {
		os << indent << openapi::JsonTypeToCppType(schema.type()) << ' ' << name << ";\n";
	}
}

void PrintReferenceSchema(
	std::ostream& os,
	std::string_view name,
	const openapi::v2::Schema& schema,
	std::string& indent) {
	if (indent.empty()) {
		// Top-level reference: Use it as a typedef synonym
		os << "using " << name << " = " << sanitize(schema.reference()) << ";\n";
	} else {
		// Not a top-level reference, then it is nested and needs a member declaration.
		os << indent << schema.reference() << ' ' << name << ";\n";
	}
}

void PrintObjectSchema(
	std::ostream& os,
	std::string_view name,
	const openapi::v2::Schema& schema,
	std::string& indent) {
	os << indent << "struct " << name << " {\n";
	indent.push_back('\t');
	if (const auto properties = schema.properties(); !properties.empty()) {
		for (const auto& [propname, prop] : properties) {
			PrintSchema(os, sanitize(propname), prop, indent);
		}
	} else {
		// Object with no properties, save as raw data
		os << indent << "std::string data;\n";
	}
	indent.pop_back();
	os << indent << "};\n";
	// If this isn't a top-level struct, then it is nested and needs a member declaration.
	if (!indent.empty()) {
		os << indent << name << ' ' << name << "_;\n";
	}
	os << std::endl;
}

void PrintArraySchema(std::ostream& os, std::string_view name, const openapi::v2::Schema& schema, std::string& indent) {
	const auto items = schema.items();
	const auto& subschema = static_cast<const openapi::v2::Schema&>(items);
	write_multiline_comment(os, subschema.description(), indent);
	if (const auto properties = subschema.properties(); !properties.empty()) {
		// Schema declared inline
		os << indent << "struct " << name << "_entry {\n";
		indent.push_back('\t');
		for (const auto& [propname, prop] : subschema.properties()) {
			PrintSchema(os, sanitize(propname), prop, indent);
		}
		indent.pop_back();
		os << indent << "};\n";
		if (indent.empty()) {
			// Top-level array declaration: Use it as a typedef
			os << "using " << name << " = std::vector<" << name << "_entry>;\n";
		} else {
			// Not a top-level array, then it nested and needs a member declaration.
			os << indent << "std::vector<" << name << "_entry> " << name << ";\n";
		}
	} else if (subschema.IsReference()) {
		if (indent.empty()) {
			// Top-level array of schema
			os << "using " << name << " = std::vector<" << sanitize(subschema.reference()) << ">;\n";
		} else {
			// Nested array of schema
			os << indent << "std::vector<" << sanitize(subschema.reference()) << "> " << name << ";\n";
		}
	} else {
		os << indent << "std::vector<" << openapi::JsonTypeToCppType(subschema.type()) << "> " << name << ";\n";
	}
	os << std::endl;
}

void PrintJSONValueFromTag(std::ostream& out, std::string_view name, const openapi::v2::Schema& schema) {
	if (schema.IsReference()) {
		return;
	}
	// We only care about objects here
	if (schema.type() != "object" && schema.type() != "array" && schema.properties().empty()) {
		return;
	}

	// Stash nested objects, print their implementations at the end.
	std::unordered_map<std::string, openapi::v2::Schema> nested_objects;

	out << "void tag_invoke(js::value_from_tag, js::value& jv, const " << name << "& v) {\n";
	out << "\tjv = {\n";

	for (const auto& [propname, prop] : schema.properties()) {
		const auto sanitized_propname = sanitize(propname);

		if (prop.type() == "object") {
			out << "\t\t{ \"" << propname << "\", js::value_from(v." << sanitized_propname << "_, jv.storage()) },\n";
			nested_objects.emplace(sanitized_propname, prop);
		} else if (prop.type() == "array") {
			const auto item = prop.items();
			const auto& subschema = static_cast<const openapi::v2::Schema&>(item);
			nested_objects.emplace(sanitized_propname + "_entry", subschema);
			out << "\t\t{ \"" << propname << "\", js::value_from(v." << sanitized_propname << ", jv.storage()) },\n";
		} else {
			out << "\t\t{ \"" << propname << "\", v." << sanitized_propname << " },\n";
		}
	}

	out << "\t};\n";
	out << "}\n" << std::endl;

	for (const auto& [propname, prop] : nested_objects) {
		PrintJSONValueFromTag(out, std::string(name) + "::" + propname, prop);
	}
}

void PrintJSONValueToTag(std::ostream& out, std::string_view name, const openapi::v2::Schema& schema) {
	if (schema.IsReference()) {
		return;
	}
	// We only care about objects here
	if (schema.type() != "object" && schema.type() != "array" && schema.properties().empty()) {
		return;
	}

	// Stash nested objects, print their implementations at the end.
	std::unordered_map<std::string, openapi::v2::Schema> nested_objects;

	out << name << " tag_invoke(js::value_to_tag<" << name << ">, const js::value& jv) {\n";
	out << "\tconst auto& obj = jv.as_object();\n";
	out << "\t" << name << " ret;\n";

	// For objects
	for (const auto& [propname, prop] : schema.properties()) {
		const auto sanitized_propname = sanitize(propname);
		if (prop.IsReference()) {
			out << "\tret." << sanitized_propname << " = js::value_to<" << prop.reference() << ">(obj.at(\"" << propname
				<< "\"));\n";
		} else if (prop.type() == "object") {
			nested_objects.emplace(sanitized_propname, prop);
			out << "\tret." << sanitized_propname << "_ = js::value_to<decltype(ret." << sanitized_propname
				<< "_)>(obj.at(\"" << propname << "\"));\n";
		} else if (prop.type() == "array") {
			const auto item = prop.items();
			const auto& subschema = static_cast<const openapi::v2::Schema&>(item);
			if (subschema.IsReference()) {
				out << "\tret." << sanitized_propname << " = js::value_to<std::vector<"
					<< sanitize(subschema.reference()) << ">>(obj.at(\"" << propname << "\"));\n";
			} else if (subschema.type() == "object" || !subschema.properties().empty()) {
				nested_objects.emplace(sanitized_propname + "_entry", subschema);
			} else {
				out << "\tret." << sanitized_propname << " = js::value_to<std::vector<"
					<< openapi::JsonTypeToCppType(subschema.type()) << ">>(obj.at(\"" << propname << "\"));\n";
			}
		} else if (prop.type().empty()) {
			out << "\tret." << sanitized_propname << " = js::value_to<std::string>(obj.at(\"" << propname << "\"));\n";
		} else {
			out << "\tret." << sanitized_propname << " = js::value_to<" << openapi::JsonTypeToCppType(prop.type())
				<< ">(obj.at(\"" << propname << "\"));\n";
		}
	}
	// If there were no properties for this object, default to 'data'.
	if (schema.properties().empty() && schema.type() != "array") {
		out << "\tret.data = js::value_to<std::string>(obj.at(\"data\"));\n";
	}

	out << "\treturn ret;\n";
	out << "}\n" << std::endl;

	for (const auto& [propname, prop] : nested_objects) {
		PrintJSONValueToTag(out, std::string(name) + "::" + propname, prop);
	}

	// For arrays
	if (const auto& item = schema.items(); item) {
		PrintJSONValueToTag(out, std::string(name) + "_entry", static_cast<const openapi::v2::Schema&>(item));
	}
}

// Write the struct definitions file.
void PrintStructDefinitions(const OpenAPIv2& file, const fs::path& input, const fs::path& output) {
#if 1
	[[maybe_unused]] bool b = StructPrinter(file, input, output)();
#else
	fs::path definitions_hpp = output / (input.stem().string() + "_defs.hpp");
	auto out_hpp = std::ofstream(definitions_hpp);
	out_hpp << "// Automatically generated from " << input.string() << ". Do not modify this file.\n"
			<< "#pragma once\n"
			<< '\n'
			<< "#include <any>\n"
			<< "#include <string>\n"
			<< "#include <vector>\n"
			<< "#include <boost/json.hpp>\n"
			<< '\n'
			<< "using namespace std::literals;\n"
			<< '\n'
			<< "namespace swagger {\n"
			<< std::endl;

	fs::path definitions_cpp = output / (input.stem().string() + "_defs.cpp");
	auto out_cpp = std::ofstream(definitions_cpp);
	out_cpp << "// Automatically generated from " << input.string() << ". Do not modify this file.\n"
			<< "#include \"" << definitions_hpp.filename().string() << "\"\n"
			<< "namespace js = ::boost::json;\n"
			<< '\n'
			<< "namespace swagger {\n"
			<< std::endl;

	std::string indent;
	// Look in definitions first
	for (const auto& [defname, def] : file.definitions()) {
		const auto name = sanitize(defname);
		PrintSchema(out_hpp, name, def, indent);
		PrintJSONValueFromTag(out_cpp, name, def);
		PrintJSONValueToTag(out_cpp, name, def);
	}

	// Look in paths
	for (const auto& [pathname, path] : file.paths()) {
		for (const auto& [opname, op] : path.operations()) {
			auto verb = openapi::RequestMethodFromString(opname);
			// Look in the parameters for body schemas
			for (const auto& param : op.parameters()) {
				if (param.in() == "body") {
					if (const auto& schema = param.schema(); schema) {
						const auto name = openapi::SynthesizeFunctionName(pathname, verb) + std::string(param.name());
						sanitize(name);
						PrintSchema(out_hpp, name, schema, indent);
						PrintJSONValueFromTag(out_cpp, name, schema);
					}
				}
			}

			// Each response status code may contain a schema
			for (const auto& [respcode, resp] : op.responses()) {
				if (const auto& schema = resp.schema(); schema) {
					auto name = openapi::SynthesizeFunctionName(pathname, verb) + '_' + std::string(respcode) + "_body";
					sanitize(name);
					write_multiline_comment(out_hpp, resp.description(), "");
					PrintSchema(out_hpp, name, schema, indent);
				}
			}
		}
	}

	out_hpp << "} // namespace swagger\n";
	out_cpp << "} // namespace swagger\n";
#endif
}

} // namespace openapi::v2
