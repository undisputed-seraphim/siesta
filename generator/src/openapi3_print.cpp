// SPDX-License-Identifier: Apache-2.0
#include "openapi3.hpp"
#include "util.hpp"
#include <fstream>

namespace fs = std::filesystem;

namespace openapi::v3 {

using Type = JsonSchema::Type;

class StructPrinter {
public:
	StructPrinter(const OpenAPIv3& file_, const fs::path& input_, const fs::path& output_)
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

	// Top level prints, used in operator()
	void PrintComponents();
	void PrintPaths();

	void PrintSchema(std::string_view name, const JsonSchema& schema);
	void PrintParameter(std::string_view name, const Parameter&);
	void PrintParameterStruct(const Parameter&);

	// Detailed print impls
	void PrintSchemaDecl(std::string_view parent_name, std::string_view name, const JsonSchema& schema);
	void PrintResponseDecl(std::string_view parent_name, std::string_view name, const JsonSchema& schema);

	void PrintJSONValueFromTagDecl(std::string_view name, const JsonSchema& schema);
	void PrintJSONValueFromTagImpl(std::string_view parent_name, std::string_view name, const JsonSchema& schema);
	void PrintJSONValueToTagDecl(std::string_view name, const JsonSchema& schema);
	void PrintJSONValueToTagImpl(std::string_view parent_name, std::string_view name, const JsonSchema& schema);
};

// gets the final component of a def path
static std::string_view component_path(std::string_view path) noexcept {
	size_t pos = path.find_last_of('/');
	return path.substr(pos + 1);
}

// Generate flat struct name from parent and child names
static std::string make_flat_name(std::string_view parent, std::string_view child) {
	if (parent.empty()) {
		return std::string(child);
	}
	return std::string(parent) + "_" + std::string(child);
}

struct RecursiveSchemaVisitor final {
	std::ostream& _os;
	std::string_view _name;
	std::string _parent_name;

	static void
	PrintPrimitive(std::ostream& out, std::string_view name, const JsonSchema& schema, const std::string& indent) {
		const auto sanitized_name = sanitize(name);
		const auto typestr = JsonTypeToCppType(schema.type(), schema.format());
		write_multiline_comment(out, schema.description(), indent);
		out << indent << typestr << ' ' << sanitized_name << ";\n";
	}

	static void PrintPrimitive(std::ostream& out, const JsonSchema& schema, const std::string& indent) {
		PrintPrimitive(out, schema.name(), schema, indent);
	}

	static void PrintStruct(std::ostream& out, std::string_view parent, std::string_view name, const Object& schema) {
		const auto flat_name = make_flat_name(parent, name);

		// Output struct definition at namespace level (no indentation)
		write_multiline_comment(out, schema.description(), "");
		out << "struct " << flat_name << " {\n";

		std::string indent = "\t";
		for (const auto& [propname, prop] : schema.properties()) {
			const auto sanitized_propname = sanitize(propname);
			if (prop.IsRef()) {
				// Skip ref handling here - will be handled by the visitor
			} else {
				prop.Visit(RecursiveSchemaVisitor{out, sanitized_propname, flat_name});
			}
		}

		out << "};\n";

		// If this is a nested struct, also output member declaration in parent
		if (!parent.empty()) {
			out << "\t" << flat_name << ' ' << sanitize(name) << "_;\n";
		}
	}

	static void PrintArray(std::ostream& out, std::string_view parent, std::string_view name, const Array& schema) {
		const auto flat_name = make_flat_name(parent, name);
		write_multiline_comment(out, schema.description(), "");

		if (schema.items().IsRef()) {
			const auto refname = component_path(schema.items().ref());
			out << "using " << flat_name << " = std::vector<" << refname << ">;\n";
			return;
		}
		if (schema.items().IsPrimitive()) {
			const auto typestr = JsonTypeToCppType(schema.items().type(), schema.items().format());
			out << "using " << flat_name << " = std::vector<" << typestr << ">;\n";
			return;
		}
		// For complex array items, the item visitor will handle it
	}

	void operator()(const String& schema) const { PrintPrimitive(_os, _name, schema, ""); }
	void operator()(const Number& schema) const { PrintPrimitive(_os, _name, schema, ""); }
	void operator()(const Integer& schema) const { PrintPrimitive(_os, _name, schema, ""); }
	void operator()(const Boolean& schema) const { PrintPrimitive(_os, _name, schema, ""); }
	void operator()(const Object& schema) const { PrintStruct(_os, _parent_name, _name, schema); }
	void operator()(const Array& schema) const { PrintArray(_os, _parent_name, _name, schema); }
};

bool StructPrinter::operator()() {
	hpp_out << "// Automatically generated from " << input.string() << ". Do not modify this file.\n"
			<< "#pragma once\n"
			<< '\n'
			<< "#include <string>\n"
			<< "#include <vector>\n"
			<< "#include <any>\n"
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
	hpp_out << "// Schemas\n";
	for (const auto& [name, schema] : file.components().schemas()) {
		PrintSchema(name, schema);
		hpp_out << std::endl;
		cpp_out << std::endl;
	}
	hpp_out << "// Parameters\n";
	for (const auto& [name, parameter] : file.components().parameters()) {
		PrintParameter(name, parameter);
	}
}

void StructPrinter::PrintSchema(std::string_view name, const JsonSchema& schema) {
	PrintSchemaDecl("", name, schema);
	PrintJSONValueFromTagDecl(name, schema);
	PrintJSONValueToTagDecl(name, schema);
	cpp_out << std::endl;
	PrintJSONValueFromTagImpl("", name, schema);
	cpp_out << std::endl;
	PrintJSONValueToTagImpl("", name, schema);
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
			_this.PrintSchemaDecl("", schema.name(), schema);
		}
		void operator()(const Array& schema) const { std::cout << "Got array " << _name << std::endl; }
	};
	parameter.schema().Visit(SchemaVisitor{*this, hpp_out, name});
}

void StructPrinter::PrintParameterStruct(const Parameter& parameter) {
	if (parameter.IsRef()) {
		const auto ref = parameter.ref();
		write_multiline_comment(hpp_out, ref, "");
		if (const auto [name, param] = file.components().GetParameterByRef(parameter.ref()); param) {
			PrintSchemaDecl("", param.value().name(), param.value().schema());
		} else {
			std::cout << "WARN: " << ref << " not found\n";
		}
	} else {
		PrintSchemaDecl("", parameter.name(), parameter.schema());
	}
}

void StructPrinter::PrintPaths() {
	hpp_out << "// Paths\n";
	std::string name; // Buffer to hold names constructed during this function
	for (const auto& [pathstr, path] : file.paths()) {
		const std::string pathstr_name = sanitize(pathstr);
		for (const auto& [opstr, op] : path.operations()) {
			write_multiline_comment(hpp_out, pathstr, "");
			hpp_out << "struct " << pathstr_name << '_' << opstr << " {\n";

			hpp_out << "\tstruct parameters {\n";
			for (const auto& parameter : op.parameters()) {
				PrintParameterStruct(parameter);
			}
			hpp_out << "\t}; // parameters\n";

			const std::string path_op_name = pathstr_name + '_' + std::string(opstr);
			for (const auto& [responsecode, response] : op.responses()) {
				write_multiline_comment(hpp_out, response.description(), "\t");
				for (const auto& [mediatypestr, mediatype] : response.content()) {
					PrintResponseDecl(path_op_name, std::string("_").append(responsecode), mediatype.schema());
				}
			}
			if (op.requestBody()) {
				for (const auto& [mediatypestr, mediatype] : op.requestBody().content()) {
					PrintSchemaDecl(path_op_name, mediatypestr, mediatype.schema());
				}
			}
			hpp_out << "};\n";
		}
		hpp_out << std::endl;
	}
}

void StructPrinter::PrintSchemaDecl(std::string_view parent_name, std::string_view name, const JsonSchema& schema) {
	static std::set<std::string> emitted_decls; // Track emitted tag_invoke declarations

	// Two-pass approach: first collect all definitions (structs and array typedefs), then emit them
	struct DefCollector {
		std::vector<std::tuple<std::string, std::string>> defs; // (name, definition)

		void collect_object(std::string_view pname, std::string_view nme, const Object& schema) {
			const auto flat_name = make_flat_name(pname, nme);

			std::string def = "struct " + std::string(flat_name) + " {\n";

			for (const auto& [propname, prop] : schema.properties()) {
				const auto sanitized_propname = sanitize(propname);

				if (prop.IsRef()) {
					const auto ref_type = sanitize(component_path(prop.ref()));
					def += "\t" + std::string(ref_type) + " " + std::string(sanitized_propname) + ";\n";
				} else if (prop.Type_() == Type::object) {
					const auto nested_flat = make_flat_name(flat_name, sanitized_propname);
					def += "\t" + std::string(nested_flat) + " " + std::string(sanitized_propname) + "_;\n";
					collect_object(flat_name, sanitized_propname, static_cast<const Object&>(prop));
				} else if (prop.Type_() == Type::array) {
					const auto& arr = static_cast<const Array&>(prop);
					const auto items = arr.items();
					if (items.IsRef()) {
						const auto ref_type = sanitize(component_path(items.ref()));
						def +=
							"\tstd::vector<" + std::string(ref_type) + "> " + std::string(sanitized_propname) + ";\n";
					} else if (items.Type_() == Type::object) {
						const auto entry_flat = make_flat_name(flat_name, std::string(sanitized_propname) + "_entry");
						def +=
							"\tstd::vector<" + std::string(entry_flat) + "> " + std::string(sanitized_propname) + ";\n";
						collect_object(
							flat_name, std::string(sanitized_propname) + "_entry", static_cast<const Object&>(items));
					} else {
						const auto typestr = JsonTypeToCppType(items.type(), items.format());
						def += "\tstd::vector<" + std::string(typestr) + "> " + std::string(sanitized_propname) + ";\n";
					}
				} else {
					const auto typestr = JsonTypeToCppType(prop.type(), prop.format());
					def += "\t" + std::string(typestr) + " " + std::string(sanitized_propname) + ";\n";
				}
			}
			def += "};\n";

			defs.emplace_back(std::string(flat_name), std::move(def));
		}

		void collect_array(std::string_view pname, std::string_view nme, const Array& schema) {
			const auto flat_name = make_flat_name(pname, nme);
			const auto items = schema.items();

			if (items.IsRef()) {
				const auto ref_type = sanitize(component_path(items.ref()));
				std::string def =
					"using " + std::string(flat_name) + " = std::vector<" + std::string(ref_type) + ">;\n";
				defs.emplace_back(std::string(flat_name), std::move(def));
			} else if (items.Type_() == Type::object) {
				// Array of objects - collect the entry struct first
				const auto entry_flat = make_flat_name(pname, std::string(nme) + "_entry");
				std::string def = "struct " + entry_flat + " {\n";

				for (const auto& [propname, prop] : static_cast<const Object&>(items).properties()) {
					const auto sanitized_propname = sanitize(propname);
					const auto typestr = JsonTypeToCppType(prop.type(), prop.format());
					def += "\t" + std::string(typestr) + " " + std::string(sanitized_propname) + ";\n";
				}
				def += "};\n";

				defs.emplace_back(entry_flat, std::move(def));

				// Then the typedef
				std::string array_def = "using " + std::string(flat_name) + " = std::vector<" + entry_flat + ">;\n";
				defs.emplace_back(std::string(flat_name), std::move(array_def));
			} else if (items.Type_() == Type::array) {
				// Array of arrays
				const auto& inner_arr = static_cast<const Array&>(items);
				if (inner_arr.items().IsRef()) {
					const auto ref_type = sanitize(component_path(inner_arr.items().ref()));
					std::string def = "using " + std::string(flat_name) + " = std::vector<std::vector<" +
									  std::string(ref_type) + ">>;\n";
					defs.emplace_back(std::string(flat_name), std::move(def));
				} else {
					const auto typestr = JsonTypeToCppType(inner_arr.items().type(), inner_arr.items().format());
					std::string def = "using " + std::string(flat_name) + " = std::vector<std::vector<" +
									  std::string(typestr) + ">>;\n";
					defs.emplace_back(std::string(flat_name), std::move(def));
				}
			} else {
				const auto typestr = JsonTypeToCppType(items.type(), items.format());
				std::string def = "using " + std::string(flat_name) + " = std::vector<" + std::string(typestr) + ">;\n";
				defs.emplace_back(std::string(flat_name), std::move(def));
			}
		}
	};

	DefCollector collector;

	// Collect all definitions based on schema type
	if (schema.Type_() == Type::object) {
		schema.Visit([&collector, name](const Object& obj) { collector.collect_object("", name, obj); });
	} else if (schema.Type_() == Type::array) {
		collector.collect_array("", name, static_cast<const Array&>(schema));
	}

	// Emit all definitions at namespace level (in collection order - parents before children)
	for (auto& [def_name, def] : collector.defs) {
		hpp_out << def;
	}
}

void StructPrinter::PrintResponseDecl(std::string_view parent_name, std::string_view name, const JsonSchema& schema) {
	if (schema.IsRef()) {
		if (const auto [schemaname, optparam] = file.components().GetSchemaByRef(schema.ref()); optparam) {
			hpp_out << "using " << name << " = " << schemaname << ";\n";
		} else {
			std::cout << "WARN: " << name << " not found at " << schema.ref() << '\n';
		}
		return;
	}
	if (auto oneofs = schema.oneOf(); !oneofs.empty()) {
		hpp_out << "using " << name << " = std::variant<";
		for (const auto& type : oneofs) {
			if (type.IsRef()) {
				if (const auto [optschemaname, optschema] = file.components().GetSchemaByRef(type.ref()); optschema) {
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
		return;
	}
	switch (schema.Type_()) {
	case Type::boolean:
	case Type::number:
	case Type::integer:
	case Type::string: {
		const auto sanitized_name = sanitize(name);
		const auto typestr = JsonTypeToCppType(schema.type(), schema.format());
		write_multiline_comment(hpp_out, schema.description(), "");
		if (parent_name.empty()) {
			hpp_out << "using " << sanitized_name << " = " << typestr << ";\n";
		} else {
			hpp_out << "\t" << typestr << ' ' << sanitized_name << ";\n";
		}
		break;
	}
	case Type::object:
	case Type::array: {
		PrintSchemaDecl(parent_name, name, schema);
		break;
	}
	}
	return;
}

// JSON value to and from tag printers

void StructPrinter::PrintJSONValueFromTagDecl(std::string_view name, const JsonSchema& schema) {
	struct DeclCollector {
		std::ostream& _os;

		void collect(const JsonSchema& schema, std::string_view parent_name, std::string_view nme) {
			if (schema.Type_() != Type::object)
				return;

			const auto flat_name = make_flat_name(parent_name, nme);
			const auto sanitized_name = sanitize(flat_name);
			_os << "void tag_invoke(boost::json::value_from_tag, boost::json::value& jv, const " << sanitized_name
				<< "& v);\n";

			auto& obj = static_cast<const Object&>(schema);
			for (const auto& [propname, prop] : obj.properties()) {
				const auto sanitized_propname = sanitize(propname);
				if (!prop.IsRef() && prop.Type_() == Type::object) {
					collect(prop, flat_name, sanitized_propname);
				} else if (prop.Type_() == Type::array) {
					const auto& arr = static_cast<const Array&>(prop);
					const auto items = arr.items();
					if (!items.IsRef() && items.Type_() == Type::object) {
						collect(items, flat_name, std::string(sanitized_propname) + "_entry");
					} else if (items.Type_() == Type::array) {
						const auto& inner = static_cast<const Array&>(items);
						if (!inner.items().IsRef() && inner.items().Type_() == Type::object) {
							collect(inner.items(), flat_name, std::string(sanitized_propname) + "_entry");
						}
					}
				}
			}
		}
	};
	DeclCollector{hpp_out}.collect(schema, "", name);
}

void StructPrinter::PrintJSONValueFromTagImpl(
	std::string_view parent_name,
	std::string_view name,
	const JsonSchema& schema) {
	struct SchemaVisitor final {
		std::ostream& _os;
		std::string _parent_name;
		std::string _name;

		void operator()(const Object& schema) const {
			const auto sanitized_name = sanitize(_name);
			const std::string full_name = _parent_name.empty() ? sanitized_name : (_parent_name + "_" + sanitized_name);

			_os << "void tag_invoke(boost::json::value_from_tag, boost::json::value& jv, const " << full_name
				<< "& v) {\n";
			_os << "\tjv = {\n";
			for (const auto& [propname, prop] : schema.properties()) {
				const auto sanitized_propname = sanitize(propname);
				if (prop.IsRef()) {
					_os << "\t{ \"" << propname << "\", js::value_from(v." << sanitized_propname
						<< ", jv.storage()) },\n";
				} else if (prop.Type_() == Type::object) {
					const auto nested_flat = make_flat_name(full_name, sanitized_propname);
					_os << "\t{ \"" << propname << "\", js::value_from(v." << sanitized_propname
						<< "_, jv.storage()) },\n";
				} else if (prop.Type_() == Type::array) {
					const auto& arr = static_cast<const Array&>(prop);
					const auto items = arr.items();
					if (!items.IsRef() && items.Type_() == Type::object) {
						_os << "\t{ \"" << propname << "\", js::value_from(v." << sanitized_propname
							<< ", jv.storage()) },\n";
					} else {
						_os << "\t{ \"" << propname << "\", js::value_from(v." << sanitized_propname
							<< ", jv.storage()) },\n";
					}
				} else {
					_os << "\t{ \"" << propname << "\", v." << sanitized_propname << " },\n";
				}
			}
			_os << "\t};\n";
			_os << "}\n\n";

			for (const auto& [propname, prop] : schema.properties()) {
				const auto sanitized_propname = sanitize(propname);
				if (!prop.IsRef() && prop.Type_() == Type::object) {
					prop.Visit(SchemaVisitor{_os, full_name, std::string(sanitized_propname)});
				} else if (prop.Type_() == Type::array) {
					const auto& arr = static_cast<const Array&>(prop);
					const auto items = arr.items();
					if (!items.IsRef() && items.Type_() == Type::object) {
						items.Visit(SchemaVisitor{_os, full_name, std::string(sanitized_propname) + "_entry"});
					}
				}
			}
		}
		void operator()(const Array& schema) const {
			const auto items = schema.items();
			if (!items.IsRef() && items.Type_() == Type::object) {
				static_cast<const Object&>(items).Visit(SchemaVisitor{_os, _parent_name, _name + "_entry"});
			} else if (items.Type_() == Type::array) {
				items.Visit(SchemaVisitor{_os, _parent_name, _name + "_entry"});
			}
		}
	};

	const auto sanitized_name = sanitize(name);
	const std::string full_name =
		parent_name.empty() ? sanitized_name : (std::string(parent_name) + "_" + sanitized_name);
	schema.Visit(SchemaVisitor{cpp_out, std::string(parent_name), sanitized_name});
}

void StructPrinter::PrintJSONValueToTagDecl(std::string_view name, const JsonSchema& schema) {
	struct DeclCollector {
		std::ostream& _os;

		void collect(const JsonSchema& schema, std::string_view parent_name, std::string_view nme) {
			if (schema.Type_() != Type::object)
				return;

			const auto flat_name = make_flat_name(parent_name, nme);
			const auto sanitized_name = sanitize(flat_name);
			_os << sanitized_name << " tag_invoke(boost::json::value_to_tag<" << sanitized_name
				<< ">, const boost::json::value& jv);\n";

			auto& obj = static_cast<const Object&>(schema);
			for (const auto& [propname, prop] : obj.properties()) {
				const auto sanitized_propname = sanitize(propname);
				if (!prop.IsRef() && prop.Type_() == Type::object) {
					collect(prop, flat_name, sanitized_propname);
				} else if (prop.Type_() == Type::array) {
					const auto& arr = static_cast<const Array&>(prop);
					const auto items = arr.items();
					if (!items.IsRef() && items.Type_() == Type::object) {
						collect(items, flat_name, std::string(sanitized_propname) + "_entry");
					} else if (items.Type_() == Type::array) {
						const auto& inner = static_cast<const Array&>(items);
						if (!inner.items().IsRef() && inner.items().Type_() == Type::object) {
							collect(inner.items(), flat_name, std::string(sanitized_propname) + "_entry");
						}
					}
				}
			}
		}
	};

	DeclCollector{hpp_out}.collect(schema, "", name);
}

void StructPrinter::PrintJSONValueToTagImpl(
	std::string_view parent_name,
	std::string_view name,
	const JsonSchema& schema) {
	struct SchemaVisitor final {
		std::ostream& _os;
		std::string _parent_name;
		std::string _name;

		void operator()(const Object& schema) const {
			const auto sanitized_name = sanitize(_name);
			const std::string full_name = _parent_name.empty() ? sanitized_name : (_parent_name + "_" + sanitized_name);

			for (const auto& [propname, prop] : schema.properties()) {
				if (!prop.IsRef() && prop.Type_() == Type::object) {
					const auto nested_sanitized = sanitize(propname);
					prop.Visit(SchemaVisitor{_os, full_name, nested_sanitized});
				}
			}

			_os << full_name << " tag_invoke(boost::json::value_to_tag<" << full_name
				<< ">, const boost::json::value& jv) {\n";
			_os << "\tconst auto& obj = jv.as_object();\n";
			_os << "\t" << full_name << " ret;\n";
			for (const auto& [propname, prop] : schema.properties()) {
				const auto sanitized_propname = sanitize(propname);
				if (!prop.IsRef() && prop.Type_() == Type::object) {
					const auto nested_flat = make_flat_name(full_name, sanitized_propname);
					_os << "\tret." << sanitized_propname << "_ = js::value_to<" << nested_flat << ">(obj.at(\""
						<< propname << "\"));\n";
				} else if (prop.Type_() == Type::array) {
					const auto& arr = static_cast<const Array&>(prop);
					const auto items = arr.items();
					if (!items.IsRef() && items.Type_() == Type::object) {
						const auto entry_flat = make_flat_name(full_name, std::string(sanitized_propname) + "_entry");
						_os << "\tret." << sanitized_propname << " = js::value_to<std::vector<" << entry_flat
							<< ">>(obj.at(\"" << propname << "\"));\n";
					} else {
						_os << "\tret." << sanitized_propname << " = js::value_to<decltype(ret." << sanitized_propname
							<< ")>(obj.at(\"" << propname << "\"));\n";
					}
				} else if (prop.IsRef()) {
					const auto ref_type = sanitize(component_path(prop.ref()));
					_os << "\tret." << sanitized_propname << " = js::value_to<" << ref_type << ">(obj.at(\"" << propname
						<< "\"));\n";
				} else {
					_os << "\tret." << sanitized_propname << " = js::value_to<"
						<< JsonTypeToCppType(prop.type(), prop.format()) << ">(obj.at(\"" << propname << "\"));\n";
				}
			}
			_os << "\treturn ret;\n";
			_os << "}\n\n";
		}
		void operator()(const Array& schema) const {
			const auto items = schema.items();
			if (!items.IsRef() && items.Type_() == Type::object) {
				static_cast<const Object&>(items).Visit(SchemaVisitor{_os, _parent_name, _name + "_entry"});
			} else if (items.Type_() == Type::array) {
				items.Visit(SchemaVisitor{_os, _parent_name, _name + "_entry"});
			}
		}
	};

	const auto sanitized_name = sanitize(name);
	schema.Visit(SchemaVisitor{cpp_out, std::string(parent_name), sanitized_name});
}

// Public interface
void PrintStructDefinitions(
	const OpenAPIv3& file,
	const std::filesystem::path& input,
	const std::filesystem::path& output) {
	[[maybe_unused]] bool b = StructPrinter(file, input, output)();
}

} // namespace openapi::v3
