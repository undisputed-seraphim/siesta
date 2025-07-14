// SPDX-License-Identifier: Apache-2.0
#include "openapi3.hpp"
#include "util.hpp"
#include <fstream>

namespace fs = std::filesystem;

namespace openapi::v3 {

using Type = JsonSchema::Type;

class StructPrinter {
public:
	StructPrinter(const OpenAPIv3& file_, const std::filesystem::path& input_, const std::filesystem::path& output_)
		: file(file_)
		, input(input_)
		, output(output_)
		, hpp_out(output / (input.stem().string() + "_defs.hpp"))
		, cpp_out(output / (input.stem().string() + "_defs.cpp")) {}

	bool operator()();

private:
	const OpenAPIv3& file;
	fs::path input, output;
	std::ofstream hpp_out, cpp_out;
	std::string indent;

	// Top level prints, used in operator()
	void PrintComponents();
	void PrintPaths();

	void PrintSchema(std::string_view name, const JsonSchema& schema);
	void PrintParameter(const Parameter&);
	void PrintParameter(std::string_view name, const Parameter&);
	void PrintParameterStruct(const Parameter&);

	// Detailed print impls
	Type PrintSchemaDecl(std::string_view name, const JsonSchema& schema);
	Type PrintSchemaDecl(std::string_view name, bool instantiate, const JsonSchema& schema);
	Type PrintResponseDecl(std::string_view name, const JsonSchema& schema);

	Type PrintJSONValueFromTagDecl(std::string_view name, const JsonSchema& schema);
	Type PrintJSONValueFromTagImpl(std::string_view name, const JsonSchema& schema);
	Type PrintJSONValueToTagDecl(std::string_view name, const JsonSchema& schema);
	Type PrintJSONValueToTagImpl(std::string_view name, const JsonSchema& schema);

	// Expects the full, untruncated ref path
	std::pair<std::string_view, std::optional<openapi::v3::JsonSchema>>
	getComponentByRef(std::string_view ref) const;
};

// gets the final component of a def path
std::string_view component_path(std::string_view path) noexcept {
	size_t pos = path.find_last_of('/');
	return path.substr(pos + 1);
}

struct RecursiveSchemaVisitor final {
	std::ostream& _os;
	std::string_view _name;
	std::string& _indent;

	static void PrintPrimitive(std::ostream& out, std::string_view name, const JsonSchema& schema, const std::string& indent) {
		const auto sanitized_name = sanitize(name);
		const auto typestr = JsonTypeToCppType(schema.type(), schema.format());
		write_multiline_comment(out, schema.description(), indent);
		out << indent << typestr << ' ' << sanitized_name << ";\n";
	}

	static void PrintPrimitive(std::ostream& out, const JsonSchema& schema, const std::string& indent) {
		PrintPrimitive(out, schema.name(), schema, indent);
	}

	static void PrintStruct(std::ostream& out, std::string_view name, const Object& schema, std::string& indent) {
		const auto sanitized_name = sanitize(name);
		write_multiline_comment(out, schema.description(), indent);
		out << indent << "struct " << sanitized_name << " {\n";
		indent.push_back('\t');
		for (const auto& [propname, prop] : schema.properties()) {
			const auto sanitized_propname = sanitize(propname);
			if (prop.IsRef()) {
				out << indent << "using " << sanitized_propname << " = " << prop.
			} else {
				prop.Visit(RecursiveSchemaVisitor{out, sanitized_name, indent});
			}
		}
		indent.pop_back();
		out << indent << "};\n";
	}

	static void PrintStruct(std::ostream& out, std::string_view name, const JsonSchema& schema, std::string& indent) {
		PrintStruct(out, schema.name(), schema, indent);
	}

	static void PrintArray(std::ostream& out, std::string_view name, const Array& schema, std::string& indent) {
		const auto sanitized_name = sanitize(name);
		write_multiline_comment(out, schema.description(), indent);

		if (schema.items().IsRef()) {
			const auto refname = component_path(schema.items().ref());
			out << indent << "using " << sanitized_name << " = std::vector<" << refname << ">;\n";
			return;
		}
		if (schema.items().IsPrimitive()) {

		}
	}

	static void PrintRefDecl(std::ostream& out, std::string_view name, const JsonSchema& schema, std::string& indent) {
		const auto refname = component_path(schema.ref());
		
	}
	static void PrintRefUsing(std::ostream& out, std::string_view name, const JsonSchema& schema, std::string& indent) {
		const auto refname = component_path(schema.ref());
		out << indent << "using " << name << " = " << refname << ";\n";
	}

	void operator()(const String& schema) const { PrintPrimitive(_os, _name, schema, _indent); }
	void operator()(const Number& schema) const { PrintPrimitive(_os, _name, schema, _indent); }
	void operator()(const Integer& schema) const { PrintPrimitive(_os, _name, schema, _indent); }
	void operator()(const Boolean& schema) const { PrintPrimitive(_os, _name, schema, _indent); }
	void operator()(const Object& schema) const { PrintStruct(_os, _name, schema, _indent); }
	void operator()(const Array& schema) const { PrintArray(_os, _name, schema, _indent); }
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

	PrintComponents();
	PrintPaths();

	hpp_out << "} // namespace openapi\n";
	cpp_out << "} // namespace openapi\n";
	return true;
}

void StructPrinter::PrintComponents() {
	for (const auto& [name, schema] : file.components().schemas()) {
		PrintSchema(name, schema);
		hpp_out << std::endl;
		cpp_out << std::endl;
	}
	for (const auto& [name, parameter] : file.components().parameters()) {
		PrintParameter(name, parameter);
	}
}

void StructPrinter::PrintSchema(std::string_view name, const JsonSchema& schema) {
	PrintSchemaDecl(name, schema);
	PrintJSONValueFromTagDecl(name, schema);
	PrintJSONValueFromTagImpl(name, schema);
	PrintJSONValueToTagDecl(name, schema);
	PrintJSONValueToTagImpl(name, schema);
}

void StructPrinter::PrintParameter(const Parameter& parameter) {
	if (parameter.IsRef()) {
		// TODO
		write_multiline_comment(hpp_out, parameter.ref(), indent);
	} else {
		write_multiline_comment(hpp_out, parameter.description(), indent);
		write_multiline_comment(hpp_out, parameter.name(), indent);
		PrintParameter(parameter.name(), parameter);
	}
}

void StructPrinter::PrintParameter(std::string_view name, const Parameter& parameter) {
	struct SchemaVisitor final {
		StructPrinter& _this;
		std::ostream& _os;
		std::string_view _name;

		void print_primitive(std::string_view type, std::string_view format) const {
			// NOTE: There is no need to supply a using typedef for primitives.
			// Instead we will embed it directly into the parameter struct.
		}
		void operator()(const String& schema) const { print_primitive(schema.type(), schema.format()); }
		void operator()(const Number& schema) const { print_primitive(schema.type(), schema.format()); }
		void operator()(const Integer& schema) const { print_primitive(schema.type(), schema.format()); }
		void operator()(const Boolean& schema) const { print_primitive(schema.type(), schema.format()); }
		void operator()(const Object& schema) const {
			write_multiline_comment(_os, schema.description(), "");
			_this.PrintSchemaDecl(schema.name(), schema);
		}
		void operator()(const Array& schema) const {
			std::cout << "Got array " << _name << std::endl;
		}
	};
	parameter.schema().Visit(SchemaVisitor{*this, hpp_out, name});
}

void StructPrinter::PrintParameterStruct(const Parameter& parameter) {
	if (parameter.IsRef()) {
		write_multiline_comment(hpp_out, parameter.ref(), indent);
		if (auto [name, optparam] = getComponentByRef(parameter.ref()); optparam) {
			PrintSchemaDecl(name, optparam.value());
		} else {
			std::cout << "WARN: " << name << " not found at " << parameter.ref() << '\n';
		}
	} else {
		PrintSchemaDecl(parameter.name(), parameter.schema());
	}
}

void StructPrinter::PrintPaths() {
	std::string name; // Buffer to hold names constructed during this function
	for (const auto& [pathstr, path] : file.paths()) {
		const std::string pathstr_name = sanitize(pathstr);
		for (const auto& [opstr, op] : path.operations()) {
			write_multiline_comment(hpp_out, pathstr, indent);
			indent.push_back('\t');
			hpp_out << "struct " << pathstr_name << '_' << opstr << " {\n";

			hpp_out << indent << "struct parameters {\n";
			indent.push_back('\t');
			for (const auto& parameter : op.parameters()) {
				PrintParameterStruct(parameter);
			}
			indent.pop_back();
			hpp_out << indent << "}; // parameters\n";

			for (const auto& [responsecode, response] : op.responses()) {
				write_multiline_comment(hpp_out, response.description(), indent);
				for (const auto& [mediatypestr, mediatype] : response.content()) {
					PrintResponseDecl(std::string("_").append(responsecode), mediatype.schema());
				}
			}
			if (op.requestBody()) {
				for (const auto& [mediatypestr, mediatype] : op.requestBody().content()) {
					PrintSchema(mediatypestr, mediatype.schema());
				}
			}
			indent.pop_back();
			hpp_out << "};\n";
		}
		hpp_out << std::endl;
	}
}

Type StructPrinter::PrintSchemaDecl(std::string_view name, const JsonSchema& schema) {
	return PrintSchemaDecl(name, false, schema);
}

Type StructPrinter::PrintSchemaDecl(std::string_view name, bool instantiate, const JsonSchema& schema) {
	struct SchemaVisitor final {
		std::ostream& _os;
		std::string_view _name;
		std::string& _indent;
		bool _instantiate = true;

		void print_primitive(const JsonSchema& schema) const {
			RecursiveSchemaVisitor::PrintPrimitive(_os, _name, schema, _indent);
		}
		void operator()(const String& schema) const { print_primitive(schema); }
		void operator()(const Number& schema) const { print_primitive(schema); }
		void operator()(const Integer& schema) const { print_primitive(schema); }
		void operator()(const Boolean& schema) const { print_primitive(schema); }
		void operator()(const Object& schema) const {
			const auto sanitized_name = sanitize(_name);
			write_multiline_comment(_os, schema.description(), _indent);
			_os << _indent << "struct " << sanitized_name << " {\n";

			_indent.push_back('\t');
			for (const auto& [propname, prop] : schema.properties()) {
				const auto sanitized_propname = sanitize(propname);
				prop.Visit(SchemaVisitor{_os, sanitized_propname, _indent});
			}
			_indent.pop_back();

			if (_indent.empty()) {
				_os << _indent << "};\n";
			} else {
				if (_instantiate) {
					_os << _indent << "} " << sanitized_name << "_;\n";
				} else {
					_os << _indent << "};\n";
				}
			}
		}
		void operator()(const Array& schema) const {
			struct ArrayItemVisitor final {
				std::ostream& _os;
				std::string_view _name;
				std::string& _indent;

				void print_primitive(const JsonSchema& schema) const {
					const auto sanitized_name = sanitize(_name);
					const auto typestr = JsonTypeToCppType(schema.type(), schema.format());
					write_multiline_comment(_os, schema.description(), _indent);
					if (_indent.empty()) {
						_os << "using " << sanitized_name << " = std::vector<" << typestr << ">;\n";
					} else {
						_os << _indent << "std::vector<" << typestr << "> " << sanitized_name << ";\n";
					}
				}

				void operator()(const String& schema) const { print_primitive(schema); }
				void operator()(const Number& schema) const { print_primitive(schema); }
				void operator()(const Integer& schema) const { print_primitive(schema); }
				void operator()(const Boolean& schema) const { print_primitive(schema); }
				void operator()(const Object& schema) const {
					if (schema.IsRef()) {
						_os << _indent << "using " << _name << " = "
							<< "std::vector<" << schema.name() << ">;\n";
					} else {
						const auto sanitized_name = sanitize(_name);
						const auto entry_name = sanitized_name + "_entry";
						schema.Visit(SchemaVisitor{_os, entry_name, _indent, false});
						_os << _indent << "std::vector<" << entry_name << "> " << sanitized_name << ";\n";
					}
				}
				void operator()(const Array& schema) const {
					const auto entry_name = std::string(_name) + "_entry";
					const auto item = schema.items();
					switch (item.Type_()) {
					case Type::string:
					case Type::number:
					case Type::integer:
					case Type::boolean: {
						_os << _indent << "std::vector<std::vector<" << JsonTypeToCppType(item.type(), item.format())
							<< ">> " << _name << ";\n";
						break;
					}
					case Type::object: {
						item.Visit(SchemaVisitor{_os, entry_name, _indent});
					}
					case Type::array: {
					}
					case Type::unknown: {
						// TODO
						_os << _indent << "// UNKNOWN: " << entry_name << '\n';
						break;
					}
					}
				}
			};
			if (schema.items().IsRef()) {
				_os << _indent << "using " << _name << " = std::vector<" << component_path(schema.items().ref()) << ">;\n";
			} else {
				schema.items().Visit(ArrayItemVisitor{_os, _name, _indent});
			}
		}
		void operator()(const JsonSchema& schema) const {
			if (!schema.oneOf().empty()) {
				printf("OneOf: %s -- ", _name.data());
				for (const auto& oneOfSchema : schema.oneOf()) {
					if (oneOfSchema.IsRef()) {
						printf("%s ", oneOfSchema.ref().data());
					} else {
						printf("%s ", oneOfSchema.type().data());
					}
				}
				printf("\n");
			}
			else if (!schema.anyOf().empty()) {
				printf("AnyOf: %s -- ", _name.data());
				bool hasAnyOfNull = false;
				for (const auto& anyOfSchema : schema.anyOf()) {
					if (anyOfSchema.IsRef()) {
						printf("%s ", anyOfSchema.ref().data());
					} else if (anyOfSchema.Type_() == Type::null) {
						hasAnyOfNull = true;
					} else {
						printf("%s ", anyOfSchema.type().data());
					}
				}
				printf("\n");
			}
			else {
				printf("JsonSchema Other: %s -- %s\n", _name.data(), schema.name().data());
			}
		}
		void operator()(std::string_view ref) const {
			const auto sanitized_name = sanitize(_name);
			const auto sanitized_ref = sanitize(component_path(ref));
			// If this is a nested ref, need to instantiate it
			// If not, then make is a typedef ('using').
			std::cout << "Doing " << _name << '\n';
			if (_indent.empty()) {
				_os << "using " << sanitized_name << " = " << sanitized_ref << ";\n";
			} else {
				_os << _indent << sanitized_ref << ' ' << sanitized_name << ";\n";
			}
		}
	};
	Type visitedType = schema.Visit(SchemaVisitor{hpp_out, name, indent, instantiate});
	if (visitedType == Type::unknown) {
		//printf("UNKNOWN: %s\n", name.data());
	}
	return visitedType;
}

Type StructPrinter::PrintResponseDecl(std::string_view name, const JsonSchema& schema) {
	if (schema.IsRef()) {
		if (auto [schemaname, optparam] = getComponentByRef(schema.ref()); optparam) {
			hpp_out << indent << "using " << name << " = " << schemaname << ";\n";
		} else {
			std::cout << "WARN: " << name << " not found at " << schema.ref() << '\n';
		}
		return Type::unknown;
	}
	if (auto oneofs = schema.oneOf(); !oneofs.empty()) {
		hpp_out << indent << "using " << name << " = std::variant<";
		for (const auto& type : oneofs) {
			if (type.IsRef()) {
				if (const auto [optschemaname, optschema] = getComponentByRef(type.ref()); optschema) {
					hpp_out << optschemaname << ',';
				} else {
					std::cout << "WARN: " << name << " not found at " << type.ref() << '\n';
				}
			} else {
				hpp_out << type.type() << ',';
			}
		}
		hpp_out.seekp(-1, std::ios::cur);
		hpp_out << ">;\n";
		return Type::unknown;
	}
	switch (schema.Type_()) {
	case Type::boolean:
	case Type::number:
	case Type::integer:
	case Type::string: {
		RecursiveSchemaVisitor::PrintPrimitive(hpp_out, schema, indent);
		break;
	}
	case Type::object:
	case Type::array: {
		PrintSchemaDecl(name, false, schema);
		break;
	}
	}
	return Type::unknown;
}


// JSON value to and from tag printers


Type StructPrinter::PrintJSONValueFromTagDecl(std::string_view name, const JsonSchema& schema) {
	struct SchemaVisitor final {
		std::ostream& _os;
		std::string_view _name;

		void operator()(const Object& schema) const {
			const auto sanitized_name = sanitize(_name);
			_os << "void tag_invoke(boost::json::value_from_tag, boost::json::value& jv, const " << sanitized_name
				<< "& v);\n";
		}
	};
	return schema.Visit(SchemaVisitor{hpp_out, name});
}

Type StructPrinter::PrintJSONValueFromTagImpl(std::string_view name, const JsonSchema& schema) {
	struct SchemaVisitor final {
		std::ostream& _os;
		std::string_view _name;
		std::string& _indent;

		void operator()(const Object& schema) const {
			_os << _indent << "void tag_invoke(boost::json::value_from_tag, boost::json::value& jv, const " << _name << "& v) {\n";
			_indent.push_back('\t');
			_os << _indent << "jv = {\n";
			_indent.push_back('\t');
			for (const auto& [propname, prop] : schema.properties()) {
				if (prop.Type_() == Type::object) {
					_os << _indent << "{ \"" << propname << "\", js::value_from(v." << propname << "_, jv.storage()) },\n";
				} else {
					_os << _indent << "{ \"" << propname << "\", v." << propname << " },\n";
				}
			}
			_indent.pop_back();
			_os << _indent << "};\n";
			_indent.pop_back();
			_os << _indent << "}\n";
		}
	};

	return schema.Visit(SchemaVisitor{cpp_out, name, indent});
}

Type StructPrinter::PrintJSONValueToTagDecl(std::string_view name, const JsonSchema& schema) {
	struct SchemaVisitor final {
		std::ostream& _os;
		std::string_view _name;

		void operator()(const Object& schema) const {
			const auto sanitized_name = sanitize(_name);
			_os << sanitized_name << " tag_invoke(boost::json::value_to_tag<" << sanitized_name
				<< ">, const boost::json::value& jv);\n";
		}
	};

	return schema.Visit(SchemaVisitor{hpp_out, name});
}

Type StructPrinter::PrintJSONValueToTagImpl(std::string_view name, const JsonSchema& schema) {
	struct SchemaVisitor final {
		std::ostream& _os;
		std::string_view _parent_name;
		std::string _name;
		std::string& _indent;

		void operator()(const Object& schema) const {
			const std::string full_name =
				(_parent_name.empty()) ? _name : (std::string(_parent_name) + "::" + _name);

			// First recurse to the leaf nodes of the tree to print nested structs
			for (const auto& [propname, prop] : schema.properties()) {
				Type nestedtype = prop.Visit(SchemaVisitor{_os, full_name, sanitize(propname), _indent});
			}

			_os << full_name << " tag_invoke(boost::json::value_to_tag<" << full_name
				<< ">, const boost::json::value& jv) {\n";
			_indent.push_back('\t');

			// Write usings first for simplicity of implementation
			for (const auto& [propname, prop] : schema.properties()) {
				if (prop.Type_() == Type::object) {
					const auto sanitized_propname = sanitize(propname);
					_os << _indent << "using " << sanitized_propname << " = " << full_name << "::" << sanitized_propname
						<< ";\n";
				}
			}

			_os << _indent << "const auto& obj = jv.as_object();\n";
			_os << _indent << full_name << " ret;\n";
			for (const auto& [propname, prop] : schema.properties()) {
				const auto sanitized_propname = sanitize(propname);
				if (prop.Type_() == Type::object) {
					_os << _indent << "ret." << sanitized_propname << "_ = js::value_to<" << sanitized_propname
						<< ">(obj.at(\"" << propname << "\"));\n";
				} else if (prop.Type_() == Type::array) {
					_os << _indent << "ret." << sanitized_propname << " = js::value_to<decltype(ret."
						<< sanitized_propname << ")>(obj.at(\"" << propname << "\"));\n";
				} else {
					_os << _indent << "ret." << sanitized_propname << " = js::value_to<"
						<< JsonTypeToCppType(prop.type(), prop.format()) << ">(obj.at(\"" << propname << "\"));\n";
				}
			}
			_os << _indent << "return ret;\n";
			_indent.pop_back();
			_os << _indent << "}\n";
		}
		void operator()(const Array& schema) const {
			schema.items().Visit(SchemaVisitor{_os, _parent_name, _name + "_entry", _indent});
		}
	};

	return schema.Visit(SchemaVisitor{cpp_out, "", sanitize(name), indent});
}

// Helpers for finding things by ref

std::pair<std::string_view, std::optional<openapi::v3::JsonSchema>>
StructPrinter::getComponentByRef(std::string_view ref) const {
	size_t pos = ref.find_first_of('/');
	ref.remove_prefix(pos + 1);
	pos = ref.find_first_of('/');
	if (ref.substr(0, pos) == "components") {
		ref.remove_prefix(pos + 1);
		pos = ref.find_first_of('/');
		const auto component = ref.substr(0, pos);
		if (component == "parameters") {
			ref.remove_prefix(pos + 1);
			for (const auto& [paramname, param] : file.components().parameters()) {
				if (paramname == ref) {
					return std::pair{ref, std::optional{param.schema()}};
				}
			}
		} else if (component == "schemas") {
			ref.remove_prefix(pos + 1);
			for (const auto& [schemaname, schema] : file.components().schemas()) {
				if (schemaname == ref) {
					return std::pair{ref, std::optional{schema}};
				}
			}
		} /*else if (component == "responses")*/
	}
	return std::pair{ref, std::nullopt};
}

// Public interface
void PrintStructDefinitions(
	const OpenAPIv3& file,
	const std::filesystem::path& input,
	const std::filesystem::path& output) {
	[[maybe_unused]] bool b = StructPrinter(file, input, output)();
}

} // namespace openapi::v3