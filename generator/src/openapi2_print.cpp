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
	Type PrintJSONValueToTagImpl(std::string_view name, const JsonSchema& schema);
};

std::string_view get_reference_name(std::string_view ref) {
	static constexpr std::string_view def_refstr = "#/definitions/";
	if (ref.starts_with(def_refstr)) {
		ref.remove_prefix(def_refstr.size());
		return ref;
	}
	return std::string_view();
}

bool StructPrinter::operator()() {
	hpp_out << "// Automatically generated from " << input.string() << ". Do not modify this file.\n"
			<< "#pragma once\n"
			<< '\n'
			<< "#include <string>\n"
			<< "#include <vector>\n"
			<< "#include <boost/json.hpp>\n"
			<< '\n'
			<< "namespace swagger {\n"
			<< std::endl;

	fs::path definitions_hpp = output / (input.stem().string() + "_defs.hpp");
	cpp_out << "// Automatically generated from " << input.string() << ". Do not modify this file.\n"
			<< "#include \"" << definitions_hpp.filename().string() << "\"\n"
			<< "namespace js = ::boost::json;\n"
			<< '\n'
			<< "namespace swagger {\n"
			<< std::endl;

	for (const auto& [defname, def] : file.def2()) {
		PrintSchema(defname, def);
	}

	for (const auto& [pathstr, path] : file.paths()) {
		for (const auto& [opstr, opt] : path.operations()) {
			for (const auto& param : opt.parameters()) {
				// PrintSchema(param.name(), param.schema());
			}
		}
	}

	hpp_out << "} // namespace swagger\n";
	cpp_out << "} // namespace swagger\n";
	return true;
}

void StructPrinter::PrintSchema(std::string_view name, const JsonSchema& schema) {
	PrintSchemaDecl(name, schema);
	PrintJSONValueFromTagDecl(name, schema);
	PrintJSONValueFromTagImpl(name, schema);
	PrintJSONValueToTagDecl(name, schema);
	PrintJSONValueToTagImpl(name, schema);
}

Type StructPrinter::PrintSchemaDecl(std::string_view name, const JsonSchema& schema) {
	struct Visitor {
		std::ostream& _os;
		std::string_view _name;
		std::string& _indent;
		bool _should_instantiate = true;

		void print_primitive(const JsonSchema& schema) const {
			write_multiline_comment(_os, schema.description(), _indent);
			if (_indent.empty()) {
				_os << "using " << _name << " = " << JsonTypeToCppType(schema.type(), schema.format()) << ";\n";
			} else {
				_os << _indent << JsonTypeToCppType(schema.type(), schema.format()) << ' ' << _name << ";\n";
			}
		}
		void operator()(const String& schema) const { print_primitive(schema); }
		void operator()(const Number& schema) const { print_primitive(schema); }
		void operator()(const Integer& schema) const { print_primitive(schema); }
		void operator()(const Boolean& schema) const { print_primitive(schema); }
		void operator()(const v2::Object& schema) const {
			write_multiline_comment(_os, schema.description(), _indent);

			if (schema.properties().empty()) {
				_os << "// WARNING: " << _name << " has no properties, assuming it's a string." << std::endl;
				if (_indent.empty()) {
					_os << "using " << _name << " = std::string;\n";
				} else {
					_os << _indent << "std::string " << _name << ";\n";
				}
				return;
			}

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
					if (_indent.empty()) {
						_os << "using " << _name << " = std::vector<"
							<< JsonTypeToCppType(schema.type(), schema.format()) << ">;\n";
					} else {
						_os << _indent << "std::vector<" << JsonTypeToCppType(schema.type(), schema.format()) << "> "
							<< _name << ";\n";
					}
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
				void operator()(std::string_view ref) const {
					auto refname = get_reference_name(ref);
					if (_indent.empty()) {
						_os << "using " << _name << " = std::vector<" << refname << ">;\n";
					} else {
						_os << _indent << "std::vector<" << refname << "> " << _name << ";\n";
					}
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
				_os << "// Encountered array " << _name << std::endl;
				break;
			}
			default:
				break;
			}
		}
		void operator()(const json_schema::Array& schema) const {
			return (*this)(static_cast<const v2::Array&>(schema));
		}
		void operator()(const json_schema::JsonSchema& schema) const {
			if (schema.HasKey("properties")) {
				return (*this)(static_cast<const v2::Object&>(schema));
			} else if (schema.IsRef()) {
				constexpr std::string_view def_refstr = "#/definitions/";
				if (auto ref = schema.ref(); ref.starts_with(def_refstr)) {
					ref.remove_prefix(def_refstr.size());
					if (_indent.empty()) {
						_os << "// Warning: Malformed reference: " << ref << '\n';
					} else {
						_os << _indent << ref << ' ' << _name << "_;\n";
					}
				} else {
					_os << _indent << "// Warning: Malformed reference: " << ref << '\n';
				}
			} else {
				write_multiline_comment(_os, schema.description(), _indent);
				_os << _indent << "// Warning: " << _name << " did not have a type, assuming it's string\n";
				_os << _indent << "std::string " << _name << ";\n";
			}
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
		void operator()(const json_schema::Array& schema) const {
			// No-op
		}
		void operator()(const json_schema::JsonSchema& schema) const {
			// TODO: Figure out how to write json from tag for primitives.
			_os << "// WARNING: " << _name << ' ' << schema.type() << '\n';
			// return (*this)(static_cast<const v2::Object&>(schema));
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
			if (schema.properties().empty()) {
				_os << "// WARNING: object " << full_name << " has no properties, treating as string.\n";
				return;
			}
			for (const auto& [propname, prop] : schema.properties()) {
				prop.Visit(Visitor{_os, full_name, sanitize(propname)});
			}

			_os << "void tag_invoke(boost::json::value_from_tag, boost::json::value& jv, const " << full_name
				<< "& v) {\n";
			_os << "\tjv = {\n";
			for (const auto& [propname, prop] : schema.properties()) {
				const auto sanitized_propname = sanitize(propname);
				if (prop.Type_() == Type::object || prop.IsRef()) {
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
		void operator()(const json_schema::Array& schema) const {
			// No-op
		}
		void operator()(const json_schema::JsonSchema& schema) const {
			return (*this)(static_cast<const v2::Object&>(schema));
		}
	};

	return schema.Visit(Visitor{hpp_out, sanitize(name)});
}

Type StructPrinter::PrintJSONValueToTagImpl(std::string_view name, const JsonSchema& schema) {
	struct Visitor final {
		std::ostream& _os;
		std::string_view _parent_name;
		std::string_view _name;

		void operator()(const v2::Object& schema) const {
			_os << _name << " tag_invoke(boost::json::value_to_tag<" << _name << ">, const boost::json::value& jv) {\n";
			_os << "\tconst auto& obj = jv.as_object();\n";
			_os << "\t" << _name << " ret;\n";
			for (const auto& [propname, prop] : schema.properties()) {
				const auto sanitized_propname = sanitize(propname);
				if (prop.IsRef()) {
					const auto refname = get_reference_name(prop.ref());
					_os << "\tret." << sanitized_propname << "_ = js::value_to<" << refname << ">(obj.at(\""
					<< propname << "\"));\n";
				} else if (prop.Type_() != Type::object) {
					_os << "\tret." << sanitized_propname << " = js::value_to<" << JsonTypeToCppType(prop.type()) << ">(obj.at(\""
					<< propname << "\"));\n";
				}
			}
			_os << "\treturn ret;\n";
			_os << "}\n";
		}
		void operator()(const json_schema::Object& schema) const {
			return (*this)(static_cast<const v2::Object&>(schema));
		}
		void operator()(const json_schema::Array& schema) const {
			// No-op
		}
		void operator()(const json_schema::JsonSchema& schema) const {
			return (*this)(static_cast<const v2::Object&>(schema));
		}
	};
	return schema.Visit(Visitor{cpp_out, "", sanitize(name)});
}

// Write the struct definitions file.
void PrintStructDefinitions(const OpenAPIv2& file, const fs::path& input, const fs::path& output) {
	[[maybe_unused]] bool b = StructPrinter(file, input, output)();
}

} // namespace openapi::v2
