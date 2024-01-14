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
	void PrintComponentSchemas();
	void PrintComponentParameters();
	void PrintComponentResponses();
	void PrintPathSchemas();

	void PrintSchema(std::string_view name, const JsonSchema& schema);
	void PrintParameter(const Parameter&);
	void PrintParameter(std::string_view name, const Parameter&);

	// Detailed print impls
	Type PrintSchemaDecl(std::string_view name, const JsonSchema& schema);
	Type PrintJSONValueFromTagDecl(std::string_view name, const JsonSchema& schema);
	Type PrintJSONValueFromTagImpl(std::string_view name, const JsonSchema& schema);
	Type PrintJSONValueToTagDecl(std::string_view name, const JsonSchema& schema);
	Type PrintJSONValueToTagImpl(std::string_view name, const JsonSchema& schema);
};

// gets the final component of a def path
std::string_view component_path(std::string_view path) noexcept {
	size_t pos = path.find_last_of('/');
	return path.substr(pos + 1);
}

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

	PrintComponentSchemas();
	PrintComponentParameters();
	PrintComponentResponses();
	PrintPathSchemas();

	hpp_out << "} // namespace openapi\n";
	cpp_out << "} // namespace openapi\n";
	return true;
}

void StructPrinter::PrintComponentSchemas() {
	for (const auto& [name, schema] : file.components().schemas()) {
		PrintSchema(name, schema);
		hpp_out << std::endl;
		cpp_out << std::endl;
	}
}

void StructPrinter::PrintSchema(std::string_view name, const JsonSchema& schema) {
	PrintSchemaDecl(name, schema);
	PrintJSONValueFromTagDecl(name, schema);
	PrintJSONValueFromTagImpl(name, schema);
	PrintJSONValueToTagDecl(name, schema);
	PrintJSONValueToTagImpl(name, schema);
}

void StructPrinter::PrintComponentParameters() {
	for (const auto& [name, parameter] : file.components().parameters()) {
		PrintParameter(name, parameter);
	}
}

void StructPrinter::PrintParameter(const Parameter& parameter) {
	if (parameter.IsRef()) {

	} else {
		write_multiline_comment(hpp_out, parameter.description(), indent);
		PrintSchema(parameter.name(), parameter.schema());
	}
}

void StructPrinter::PrintParameter(std::string_view name, const Parameter& parameter) {
	using Location = Parameter::Location;
	write_multiline_comment(hpp_out, parameter.description(), indent);
	hpp_out << "struct " << name << " {\n";
	indent.push_back('\t');

	// PrintSchema(os, name, parameter.schema(), indent);

	indent.pop_back();
	hpp_out << "};\n";
}

void StructPrinter::PrintComponentResponses() {
	for (const auto& [name, response] : file.components().responses()) {
		for (const auto& [mediatypestr, mediatype] : response.content()) {
			PrintSchema(mediatypestr, mediatype.schema());
		}
	}
}

void StructPrinter::PrintPathSchemas() {
	std::string name; // Buffer to hold names constructed during this function
	for (const auto& [pathstr, path] : file.paths()) {
		const std::string pathstr_name = sanitize(pathstr);
		for (const auto& [opstr, op] : path.operations()) {
			for (const auto& parameter : op.parameters()) {
				PrintParameter(parameter);
			}
			for (const auto& [responsecode, response] : op.responses()) {
				write_multiline_comment(hpp_out, response.description(), indent);
				for (const auto& [mediatypestr, mediatype] : response.content()) {
					name = pathstr_name + '_' + std::string(opstr) + '_' + std::string(responsecode);
					PrintSchema(name, mediatype.schema());
				}
			}
			if (op.requestBody()) {
				for (const auto& [mediatypestr, mediatype] : op.requestBody().content()) {
					PrintSchema(mediatypestr, mediatype.schema());
				}
			}
		}
		for (const auto& parameter : path.parameters()) {
			PrintParameter(parameter);
		}
		hpp_out << std::endl;
	}
}

Type StructPrinter::PrintSchemaDecl(std::string_view name, const JsonSchema& schema) {
	struct SchemaVisitor final {
		std::ostream& _os;
		std::string_view _name;
		std::string& _indent;

		// void operator()(const String& schema) const { _os << _indent << "std::string " << _name << ";\n"; }
		// void operator()(const Number& schema) const { _os << _indent << "double " << _name << ";\n"; }
		// void operator()(const Integer& schema) const { _os << _indent << "int64_t " << _name << ";\n"; }
		// void operator()(const Boolean& schema) const { _os << _indent << "bool " << _name << ";\n"; }
		void operator()(const Object& schema) const {
			const auto sanitized_name = sanitize(_name);
			_os << _indent << "struct " << sanitized_name << " {\n";
			_indent.push_back('\t');
			for (const auto& [propname, prop] : schema.properties()) {
				const auto sanitized_propname = sanitize(propname);
				const auto type = prop.Type_();
				switch (type) {
				case Type::string:
				case Type::number:
				case Type::integer:
				case Type::boolean: {
					_os << _indent << JsonTypeToCppType(prop.type(), prop.format()) << ' ' << sanitized_propname
						<< ";\n";
					break;
				}
				case Type::object:
				case Type::array: {
					prop.Visit(SchemaVisitor{_os, sanitized_propname, _indent});
					break;
				}
				default:
					break;
				}
			}
			_indent.pop_back();

			if (_indent.empty()) {
				_os << _indent << "};\n";
			} else {
				_os << _indent << "} " << sanitized_name << "_;\n";
			}
		}
		void operator()(const Array& schema) const {
			struct ArrayItemVisitor {
				std::ostream& _os;
				std::string_view _name;
				std::string& _indent;

				void operator()(const String& schema) const {
					_os << _indent << "std::vector<std::string> " << _name << ";\n";
				}
				void operator()(const Number& schema) const {
					_os << _indent << "std::vector<double> " << _name << ";\n";
				}
				void operator()(const Integer& schema) const {
					_os << _indent << "std::vector<int64_t> " << _name << ";\n";
				}
				void operator()(const Boolean& schema) const {
					_os << _indent << "std::vector<bool> " << _name << ";\n";
				}
				void operator()(const Object& schema) const {
					if (schema.IsRef()) {
						_os << _indent << "using " << _name << " = "
							<< "std::vector<" << schema.name() << ">;\n";
					} else {
						const auto entry_name = std::string(_name) + "_entry";
						schema.Visit(SchemaVisitor{_os, entry_name, _indent});
						_os << _indent << "std::vector<" << entry_name << "> " << _name << ";\n";
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
			schema.items().Visit(ArrayItemVisitor{_os, _name, _indent});
		}
		void operator()(std::string_view ref) const {
			_os << _indent << "using " << _name << " = " << component_path(ref) << ";\n";
		}
	};
	return schema.Visit(SchemaVisitor{hpp_out, name, indent});
}

Type StructPrinter::PrintJSONValueFromTagDecl(std::string_view name, const JsonSchema& schema) {
	struct SchemaVisitor final {
		std::ostream& _os;
		std::string_view _name;

		void operator()(const Object& schema) const {
			_os << "void tag_invoke(boost::json::value_from_tag, boost::json::value& jv, const " << _name << "& v);\n";
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
			_os << _indent << "void tag_invoke(boost::json::value_from_tag, boost::json::value& jv, const " << _name
				<< "& v) {\n";
			_indent.push_back('\t');
			_os << _indent << "jv = {\n";
			_indent.push_back('\t');
			for (const auto& [propname, prop] : schema.properties()) {
				if (prop.Type_() == Type::object) {
					_os << _indent << "{ \"" << propname << "\", v." << propname << "_ },\n";
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
			_os << _name << " tag_invoke(boost::json::value_to_tag<" << _name << ">, const boost::json::value& jv);\n";
		}
	};

	return schema.Visit(SchemaVisitor{hpp_out, name});
}

Type StructPrinter::PrintJSONValueToTagImpl(std::string_view name, const JsonSchema& schema) {
	struct SchemaVisitor final {
		std::ostream& _os;
		std::string_view _name;
		std::string& _indent;

		void operator()(const Object& schema) const {
			_os << _name << " tag_invoke(boost::json::value_to_tag<" << _name << ">, const boost::json::value& jv) {\n";
			_indent.push_back('\t');
			// Write usings first for simplicity of implementation
			for (const auto& [propname, prop] : schema.properties()) {
				if (prop.Type_() == Type::object) {
					_os << _indent << "using " << propname << " = " << _name << "::" << propname << ";\n";
				}
			}

			_os << _indent << "const auto& obj = jv.as_object();\n";
			_os << _indent << _name << " ret;\n";
			for (const auto& [propname, prop] : schema.properties()) {
				if (prop.Type_() == Type::object) {
					_os << _indent << "ret." << propname << "_ = js::value_to<" << propname << ">(obj.at(\"" << propname
						<< "\"));\n";
				} else if (prop.Type_() == Type::array) {
					_os << _indent << "ret." << propname << " = js::value_to<decltype(ret." << propname
						<< ")>(obj.at(\"" << propname << "\"));\n";
				} else {
					_os << _indent << "ret." << propname << " = js::value_to<"
						<< JsonTypeToCppType(prop.type(), prop.format()) << ">(obj.at(\"" << propname << "\"));\n";
				}
			}
			_os << _indent << "return ret;\n";
			_indent.pop_back();
			_os << _indent << "}\n";
		}
	};

	return schema.Visit(SchemaVisitor{cpp_out, name, indent});
}

// Public interface
void PrintStructDefinitions(
	const OpenAPIv3& file,
	const std::filesystem::path& input,
	const std::filesystem::path& output) {
	[[maybe_unused]] bool b = StructPrinter(file, input, output)();
}

} // namespace openapi::v3