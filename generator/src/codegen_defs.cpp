// SPDX-License-Identifier: Apache-2.0
#include "codegen_defs.hpp"
#include "util.hpp"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace codegen {

// Helper to resolve a type name to its actual C++ type (follows typedef chains recursively)
static std::string resolveTypeName(
	const schema::NormalizedAST& ast,
	const std::unordered_map<std::string, std::string>& typedef_chain,
	const std::string& name) {
	// Follow typedef chain recursively
	std::string current = name;
	std::unordered_set<std::string> visited;
	while (true) {
		auto chain_it = typedef_chain.find(current);
		if (chain_it == typedef_chain.end()) {
			break;
		}
		if (visited.count(current)) {
			// Circular chain - stop
			break;
		}
		visited.insert(current);
		std::string next = chain_it->second;
		if (next == current) {
			break;
		}
		current = next;
	}

	const auto* type = ast.getType(current);
	if (!type) {
		return current;
	}

	std::string result = std::visit(
		[&](const auto& t) -> std::string {
			using T = std::decay_t<decltype(t)>;

			if constexpr (std::is_same_v<T, schema::PrimitiveType>) {
				// If it has enum_values, it's an enum class - return name as-is
				if (!t.enum_values.empty()) {
					return current;
				}
				return "std::string"; // default fallback
			} else if constexpr (std::is_same_v<T, schema::VariantType>) {
				// If variant has exactly one alternative and is not nullable, resolve to that type
				if (t.alternatives.size() == 1 && !t.is_nullable) {
					const auto& alt = t.alternatives[0];
					if (!alt.is_inline) {
						return resolveTypeName(ast, typedef_chain, alt.name);
					}
					return alt.name;
				}
				// Otherwise return name as-is
				return current;
			} else {
				// Struct, Enum, Array, Map - return name as-is
				return current;
			}
		},
		*type);

	return result;
}

// Get a canonical signature for variant alternatives (for deduplication)
static std::string getVariantSignature(
	const schema::NormalizedAST& ast,
	const std::unordered_map<std::string, std::string>& typedef_chain,
	const std::vector<schema::TypeRef>& alternatives) {
	std::ostringstream sig;
	sig << "variant<";
	for (size_t i = 0; i < alternatives.size(); ++i) {
		if (i > 0)
			sig << ",";
		const auto& alt = alternatives[i];
		if (alt.is_inline) {
			sig << alt.name;
		} else {
			sig << resolveTypeName(ast, typedef_chain, alt.name);
		}
	}
	sig << ">";
	return sig.str();
}

// Process variant alternatives: deduplicate and check for single-alternative collapse
static std::pair<std::vector<schema::TypeRef>, bool> processVariantAlternatives(
	const schema::NormalizedAST& ast,
	const std::unordered_map<std::string, std::string>& typedef_chain,
	const schema::VariantType& v) {
	std::vector<schema::TypeRef> processed;

	// Resolve and deduplicate alternatives
	std::unordered_map<std::string, schema::TypeRef> seen; // resolved type -> first occurrence
	std::unordered_set<std::string> duplicates;
	for (const auto& alt : v.alternatives) {
		std::string resolved;
		if (alt.is_inline) {
			resolved = alt.name;
		} else {
			resolved = resolveTypeName(ast, typedef_chain, alt.name);
		}

		auto [it, inserted] = seen.emplace(resolved, alt);
		if (!inserted) {
			duplicates.insert(resolved);
		}
	}

	// Collect unique alternatives - create inline TypeRefs with resolved names
	for (const auto& [resolved_type, original_alt] : seen) {
		schema::TypeRef ref;
		ref.name = resolved_type;
		ref.is_inline = true;
		processed.push_back(ref);
	}

	// Check if should collapse to typedef (single alternative, not nullable)
	bool collapse = (processed.size() == 1 && !v.is_nullable);

	return {processed, collapse};
}

void DefsGenerator::operator()(const CodegenArgs& args, const std::filesystem::path& output_dir) {
	const auto& ast = args.ast;
	const auto& order = args.order;

	std::filesystem::create_directories(output_dir);

	{
		auto path = output_dir / "openapi_defs.hpp";
		std::ofstream out(path);
		if (out) {
			generateDefsHpp(out, order, ast);
		}
	}

	{
		auto path = output_dir / "openapi_defs.cpp";
		std::ofstream out(path);
		if (out) {
			generateDefsCpp(out, order, ast);
		}
	}
}

void DefsGenerator::generateDefsHpp(std::ostream& out,
                                     const analysis::TopologicalOrder& order,
                                     const schema::NormalizedAST& ast) {
	// Header guard
	out << "#pragma once\n\n";

	// Includes — emit only those actually referenced by the schema
	out << "#include <string>\n";

	{
		bool need_vector = false, need_map = false, need_variant = false, need_optional = false, need_cstdint = false;
		for (const auto& name : order.ordered_types) {
			const auto* type = ast.getType(name);
			if (!type) continue;
			std::visit(
				[&](const auto& t) {
					using T = std::decay_t<decltype(t)>;
					if constexpr (std::is_same_v<T, schema::ArrayType>) {
						need_vector = true;
					} else if constexpr (std::is_same_v<T, schema::MapType>) {
						need_map = true;
					} else if constexpr (std::is_same_v<T, schema::VariantType>) {
						need_variant = true;
						if (t.is_nullable) need_optional = true;
					} else if constexpr (std::is_same_v<T, schema::StructType>) {
						for (const auto& f : t.fields) {
							std::string tn = cppTypeName(f.type);
							if (tn.find("vector<") != std::string::npos)    need_vector   = true;
							if (tn.find("map<") != std::string::npos)       need_map      = true;
							if (tn.find("variant<") != std::string::npos)   need_variant  = true;
							if (tn.find("optional<") != std::string::npos)  need_optional = true;
							if (tn.find("int") != std::string::npos)        need_cstdint  = true;
						}
					} else if constexpr (std::is_same_v<T, schema::PrimitiveType>) {
						if (cppTypeName(t).find("int") != std::string::npos) need_cstdint = true;
					}
				},
				*type);
		}
		if (need_vector)   out << "#include <vector>\n";
		if (need_map)      out << "#include <map>\n";
		if (need_variant)  out << "#include <variant>\n";
		if (need_optional) out << "#include <optional>\n";
		if (need_cstdint)  out << "#include <cstdint>\n";
	}

	out << "#include <boost/json.hpp>\n\n";

	// Namespace
	out << "namespace api {\n\n";

	// Forward declarations for all types
	out << "// Forward declarations\n";
	for (const auto& name : order.ordered_types) {
		const auto* type = ast.getType(name);
		if (type) {
			std::visit(
				[&](const auto& t) {
					using T = std::decay_t<decltype(t)>;

					if constexpr (std::is_same_v<T, schema::StructType>) {
						out << "struct " << name << ";\n";
					} else if constexpr (std::is_same_v<T, schema::EnumType>) {
						out << "enum class " << name << " : int;\n";
					}
					// Skip variant forward declarations - emit full typedef in definitions section
					// to ensure proper ordering with primitive typedefs
				},
				*type);
		}
	}
	out << "\n";

	// Full definitions in topological order
	LOG_EMIT("generateDefsHpp: emitting %zu types from topological order", order.ordered_types.size());
	int emitted_structs = 0, emitted_variants = 0, emitted_enums = 0, emitted_typedefs = 0, emitted_aliases = 0;
	for (const auto& name : order.ordered_types) {
		const auto* type = ast.getType(name);
		if (!type) {
			continue;
		}

		std::visit(
			[&](const auto& t) {
				using T = std::decay_t<decltype(t)>;

				if constexpr (std::is_same_v<T, schema::StructType>) {
					emitStruct(out, t);
					emitted_structs++;
				} else if constexpr (std::is_same_v<T, schema::VariantType>) {
					emitVariant(out, t, ast);
					emitted_variants++;
				} else if constexpr (std::is_same_v<T, schema::EnumType>) {
					emitEnum(out, t);
					emitted_enums++;
				} else if constexpr (std::is_same_v<T, schema::PrimitiveType>) {
					// Named primitive with enum values - emit as enum class for type safety
					if (!t.enum_values.empty()) {
						emitEnumFromPrimitive(out, name, t);
					} else {
						// Plain named primitive - emit as typedef
						emitPrimitiveTypedef(out, name, t);
					}
					emitted_typedefs++;
				} else if constexpr (std::is_same_v<T, schema::ArrayType>) {
					// Top-level array schemas - emit as type alias
					emitArrayAlias(out, name, t);
					emitted_aliases++;
				} else if constexpr (std::is_same_v<T, schema::MapType>) {
					// Top-level map schemas - emit as type alias
					emitMapAlias(out, name, t);
					emitted_aliases++;
				}
			},
			*type);
		out << "\n";
	}
	LOG_EMIT(
		"generateDefsHpp: emitted %d structs, %d variants, %d enums, %d typedefs, %d aliases",
		emitted_structs,
		emitted_variants,
		emitted_enums,
		emitted_typedefs,
		emitted_aliases);

	// Serialization declarations
	out << "// JSON serialization\n";
	for (const auto& name : order.ordered_types) {
		const auto* type = ast.getType(name);
		if (!type)
			continue;

		std::visit(
			[&](const auto& t) {
				using T = std::decay_t<decltype(t)>;

				if constexpr (std::is_same_v<T, schema::StructType> || std::is_same_v<T, schema::VariantType>) {
					out << "void tag_invoke(boost::json::value_from_tag, boost::json::value& jv, const " << name
						<< "& v);\n";
					out << name << " tag_invoke(boost::json::value_to_tag<" << name
						<< ">, const boost::json::value& jv);\n";
				} else if constexpr (std::is_same_v<T, schema::EnumType>) {
					out << "void tag_invoke(boost::json::value_from_tag, boost::json::value& jv, " << name << " v);\n";
					out << name << " tag_invoke(boost::json::value_to_tag<" << name
						<< ">, const boost::json::value& jv);\n";
				} else if constexpr (std::is_same_v<T, schema::PrimitiveType>) {
					if (!t.enum_values.empty()) {
						out << "void tag_invoke(boost::json::value_from_tag, boost::json::value& jv, " << name
							<< " v);\n";
						out << name << " tag_invoke(boost::json::value_to_tag<" << name
							<< ">, const boost::json::value& jv);\n";
					}
				}
			},
			*type);
	}

	out << "\n} // namespace api\n";
}

void DefsGenerator::generateDefsCpp(std::ostream& out,
                                     const analysis::TopologicalOrder& order,
                                     const schema::NormalizedAST& ast) {
	out << "#include \"openapi_defs.hpp\"\n";
	out << "namespace api {\n\n";

	int cpp_structs = 0, cpp_variants = 0, cpp_enums = 0;
	for (const auto& name : order.ordered_types) {
		const auto* type = ast.getType(name);
		if (!type)
			continue;

		std::visit(
			[&](const auto& t) {
				using T = std::decay_t<decltype(t)>;

				if constexpr (std::is_same_v<T, schema::StructType>) {
					emitStructSerialization(out, t);
					cpp_structs++;
				} else if constexpr (std::is_same_v<T, schema::VariantType>) {
					emitVariantSerialization(out, t, ast);
					cpp_variants++;
				} else if constexpr (std::is_same_v<T, schema::EnumType>) {
					emitEnumSerialization(out, t);
					cpp_enums++;
				} else if constexpr (std::is_same_v<T, schema::PrimitiveType>) {
					if (!t.enum_values.empty()) {
						emitEnumFromPrimitiveSerialization(out, name, t);
					}
				}
			},
			*type);
	}

	LOG_EMIT(
		"generateDefsCpp: emitted %d struct, %d variant, %d enum serializations", cpp_structs, cpp_variants, cpp_enums);
	out << "} // namespace api\n";
}

void DefsGenerator::emitStruct(std::ostream& out, const schema::StructType& s) {
	// Documentation
	if (!s.description.empty()) {
		out << "/**\n * " << escapeCppString(s.description) << "\n */\n";
	}

	// Struct definition with inheritance
	out << "struct " << s.name;
	if (!s.allOf_bases.empty()) {
		out << " : ";
		for (size_t i = 0; i < s.allOf_bases.size(); ++i) {
			if (i > 0)
				out << ", ";
			out << cppTypeName(s.allOf_bases[i]);
		}
	}
	out << " {\n";

	// Fields
	for (const auto& field : s.fields) {
		// Documentation
		if (!field.description.empty()) {
			out << "    /** " << escapeCppString(field.description) << " */\n";
		}

		std::string type_name = cppTypeName(field.type);
		out << "    " << type_name << " " << field.name;

		// Default value
		if (field.default_value) {
			out << " = " << *field.default_value;
		}
		out << ";\n";
	}

	out << "};\n";
}

void DefsGenerator::emitVariant(std::ostream& out, const schema::VariantType& v, const schema::NormalizedAST& ast) {
	// Documentation
	if (!v.description.empty()) {
		out << "/**\n * " << escapeCppString(v.description) << "\n */\n";
	}

	// Handle empty variant (fallback to std::monostate)
	if (v.alternatives.empty() && !v.is_nullable) {
		LOG_EMIT("emitVariant '%s': EMPTY -> std::monostate", v.name.c_str());
		out << "using " << v.name << " = std::monostate;\n";
		return;
	}

	// Process alternatives: deduplicate and check for collapse
	auto [processed_alternatives, should_collapse] = processVariantAlternatives(ast, typedef_chain_, v);

	// Check if this is a duplicate variant
	std::string sig = getVariantSignature(ast, typedef_chain_, processed_alternatives);
	auto [it, inserted] = emitted_variant_signatures_.emplace(sig, v.name);
	if (!inserted) {
		// Duplicate! Emit as alias and track typedef chain
		std::string target = it->second;
		LOG_EMIT("emitVariant '%s': DUPLICATE sig='%s' -> alias to '%s'", v.name.c_str(), sig.c_str(), target.c_str());
		out << "using " << v.name << " = " << target << ";\n";
		typedef_chain_[v.name] = target;
		return;
	}

	// If collapsed to single alternative, emit as typedef and track chain
	if (should_collapse) {
		std::string target = cppTypeName(processed_alternatives[0]);
		LOG_EMIT("emitVariant '%s': COLLAPSED to single alt '%s'", v.name.c_str(), target.c_str());
		out << "using " << v.name << " = " << target << ";\n";
		typedef_chain_[v.name] = target;
		return;
	}

	// Normal variant typedef
	LOG_EMIT(
		"emitVariant '%s': %zu alternatives (nullable=%d) -> std::variant<%s>",
		v.name.c_str(),
		processed_alternatives.size(),
		v.is_nullable,
		sig.c_str());
	out << "using " << v.name << " = std::variant<";
	for (size_t i = 0; i < processed_alternatives.size(); ++i) {
		if (i > 0)
			out << ", ";
		out << cppTypeName(processed_alternatives[i]);
	}
	if (v.is_nullable) {
		if (!processed_alternatives.empty())
			out << ", ";
		out << "std::nullptr_t";
	}
	out << ">;\n";
}

void DefsGenerator::emitEnum(std::ostream& out, const schema::EnumType& e) {
	// Documentation
	if (!e.description.empty()) {
		out << "/**\n * " << escapeCppString(e.description) << "\n */\n";
	}

	out << "enum class " << e.name << " : int {\n";
	for (size_t i = 0; i < e.values.size(); ++i) {
		const auto& val = e.values[i];
		std::string safe_name = sanitize_enum_identifier(val.name);
		out << "    " << safe_name;
		if (i + 1 < e.values.size())
			out << ",";
		out << "\n";
	}
	out << "};\n";
}

void DefsGenerator::emitPrimitiveTypedef(std::ostream& out, const std::string& name, const schema::PrimitiveType& p) {
	// Documentation with enum values
	if (!p.description.empty()) {
		out << "/**\n * " << escapeCppString(p.description) << "\n";
		if (!p.enum_values.empty()) {
			out << " * Allowed values: ";
			for (size_t i = 0; i < p.enum_values.size(); ++i) {
				if (i > 0)
					out << ", ";
				out << "`" << escapeCppString(p.enum_values[i]) << "`";
			}
			out << "\n";
		}
		out << " */\n";
	}

	out << "using " << name << " = " << cppTypeName(p) << ";\n";
}

void DefsGenerator::emitEnumFromPrimitive(std::ostream& out, const std::string& name, const schema::PrimitiveType& p) {
	// Determine base type for enum (string or int)
	std::string base_type = "int"; // Default to int for enums

	// Documentation with enum values
	if (!p.description.empty()) {
		out << "/**\n * " << escapeCppString(p.description) << "\n";
		if (!p.enum_values.empty()) {
			out << " * Allowed values: ";
			for (size_t i = 0; i < p.enum_values.size(); ++i) {
				if (i > 0)
					out << ", ";
				out << "`" << escapeCppString(p.enum_values[i]) << "`";
			}
			out << "\n";
		}
		out << " */\n";
	}

	// Emit enum class
	out << "enum class " << name << " : " << base_type << " {\n";
	for (size_t i = 0; i < p.enum_values.size(); ++i) {
		const auto& enum_val = p.enum_values[i];
		// Use sanitize_enum_identifier() to convert enum value to valid C++ identifier
		std::string enum_name = sanitize_enum_identifier(enum_val);
		out << "    " << enum_name;
		if (i + 1 < p.enum_values.size())
			out << ",";
		out << "\n";
	}
	out << "};\n";
}

void DefsGenerator::emitEnumFromPrimitiveSerialization(
	std::ostream& out,
	const std::string& name,
	const schema::PrimitiveType& p) {
	// to_json (value_from) - convert enum to string
	out << "void tag_invoke(boost::json::value_from_tag, boost::json::value& jv, " << name << " val) {\n";
	out << "    switch (val) {\n";
	for (size_t i = 0; i < p.enum_values.size(); ++i) {
		const auto& enum_val = p.enum_values[i];
		// Use sanitize_enum_identifier() to convert enum value to valid C++ identifier
		std::string enum_name = sanitize_enum_identifier(enum_val);
		out << "        case " << name << "::" << enum_name << ": jv = \"" << escapeCppString(enum_val)
			<< "\"; break;\n";
	}
	out << "        default: jv = \"\"; break;\n";
	out << "    }\n";
	out << "}\n\n";

	// from_json (value_to) - convert string to enum
	out << name << " tag_invoke(boost::json::value_to_tag<" << name << ">, const boost::json::value& jv) {\n";
	out << "    auto str = jv.as_string();\n";
	std::string default_enum = (!p.enum_values.empty()) ? sanitize_enum_identifier(p.enum_values[0]) : "0";
	out << "    if (str.empty()) return " << name << "::" << default_enum << ";\n";
	out << "\n";
	for (const auto& enum_val : p.enum_values) {
		out << "    if (str == \"" << escapeCppString(enum_val) << "\") {\n";
		// Use sanitize_enum_identifier() to convert enum value to valid C++ identifier
		std::string enum_name = sanitize_enum_identifier(enum_val);
		out << "        return " << name << "::" << enum_name << ";\n";
		out << "    }\n";
	}
	out << "    return " << name << "::" << default_enum << ";\n";
	out << "}\n\n";
}

void DefsGenerator::emitArrayAlias(std::ostream& out, const std::string& name, const schema::ArrayType& arr) {
	out << "using " << name << " = std::vector<" << cppTypeName(arr.element_type) << ">;\n";
}

void DefsGenerator::emitMapAlias(std::ostream& out, const std::string& name, const schema::MapType& m) {
	out << "using " << name << " = std::map<std::string, " << cppTypeName(m.value_type) << ">;\n";
}

void DefsGenerator::emitStructSerialization(std::ostream& out, const schema::StructType& s) {
	// to_json (value_from)
	out << "void tag_invoke(boost::json::value_from_tag, boost::json::value& jv, const " << s.name << "& v) {\n";
	out << "    boost::json::object obj;\n";

	// Serialize base classes first
	if (!s.allOf_bases.empty()) {
		for (const auto& base : s.allOf_bases) {
			out << "    { \n        auto base_jv = boost::json::value_from(static_cast<const " << cppTypeName(base)
				<< "&>(v));\n";
			out << "        auto base_obj = base_jv.get_object();\n";
			out << "        for (auto& [key, val] : base_obj) {\n";
			out << "            obj[key] = val;\n";
			out << "        }\n";
			out << "    }\n";
		}
	}

	// Serialize fields
	for (const auto& field : s.fields) {
		out << "    obj[\"" << field.name << "\"] = boost::json::value_from(v." << field.name << ");\n";
	}

	out << "    jv = std::move(obj);\n";
	out << "}\n\n";

	// from_json (value_to)
	out << s.name << " tag_invoke(boost::json::value_to_tag<" << s.name << ">, const boost::json::value& jv) {\n";
	out << "    " << s.name << " obj;\n";

	// Deserialize base classes first
	if (!s.allOf_bases.empty()) {
		for (const auto& base : s.allOf_bases) {
			out << "    static_cast<" << cppTypeName(base) << "&>(obj) = \n        boost::json::value_to<"
				<< cppTypeName(base) << ">(jv);\n";
		}
	}

	// Deserialize fields
	for (const auto& field : s.fields) {
		out << "    if (jv.if_object()->contains(\"" << field.name << "\")) {\n";
		out << "        obj." << field.name << " = boost::json::value_to<" << cppTypeName(field.type)
			<< ">(jv.as_object().at(\"" << field.name << "\"));\n";
		out << "    }\n";
	}

	out << "    return obj;\n";
	out << "}\n\n";
}

void DefsGenerator::emitVariantSerialization(std::ostream& out, const schema::VariantType& v, const schema::NormalizedAST& ast) {
	// Process alternatives to check if this is a duplicate or collapsed variant
	auto [processed_alternatives, should_collapse] = processVariantAlternatives(ast, typedef_chain_, v);
	std::string sig = getVariantSignature(ast, typedef_chain_, processed_alternatives);

	// Check if duplicate (already emitted signature exists)
	auto it = emitted_variant_signatures_.find(sig);
	if (it != emitted_variant_signatures_.end() && it->second != v.name) {
		// Duplicate variant - skip serialization (it's just an alias)
		return;
	}

	// Skip serialization for collapsed variants (they're typedefs)
	if (should_collapse) {
		return;
	}

	// to_json
	out << "void tag_invoke(boost::json::value_from_tag, boost::json::value& jv, const " << v.name << "& val) {\n";
	out << "    std::visit([&](const auto& inner) {\n";
	out << "        jv = boost::json::value_from(inner);\n";
	out << "    }, val);\n";
	out << "}\n\n";

	// from_json - simplified, just try each type
	out << v.name << " tag_invoke(boost::json::value_to_tag<" << v.name << ">, const boost::json::value& jv) {\n";
	for (size_t i = 0; i < processed_alternatives.size(); ++i) {
		const auto& alt = processed_alternatives[i];
		out << "    if constexpr (" << i << " < " << processed_alternatives.size() << ") {\n";
		out << "        try {\n";
		out << "            return " << v.name << "(boost::json::value_to<" << cppTypeName(alt) << ">(jv));\n";
		out << "        } catch (...) {\n";
		out << "            // Try next alternative\n";
		out << "        }\n";
		out << "    }\n";
	}
	out << "    throw std::runtime_error(\"No matching variant alternative\");\n";
	out << "}\n\n";
}

void DefsGenerator::emitEnumSerialization(std::ostream& out, const schema::EnumType& e) {
	// to_json
	out << "void tag_invoke(boost::json::value_from_tag, boost::json::value& jv, " << e.name << " val) {\n";
	out << "    switch (val) {\n";
	for (const auto& enum_val : e.values) {
		std::string safe_name = sanitize_enum_identifier(enum_val.name);
		out << "        case " << e.name << "::" << safe_name << ": "
			<< "jv = \"" << escapeCppString(enum_val.value) << "\"; break;\n";
	}
	out << "    }\n";
	out << "}\n\n";

	// from_json
	out << e.name << " tag_invoke(boost::json::value_to_tag<" << e.name << ">, const boost::json::value& jv) {\n";
	out << "    auto str = jv.as_string();\n";
	std::string default_val = e.values.empty() ? "0" : sanitize_enum_identifier(e.values[0].name);
	out << "    if (str == \"\") return " << e.name << "::" << default_val << ";\n";
	out << "    \n";
	for (const auto& enum_val : e.values) {
		std::string safe_name = sanitize_enum_identifier(enum_val.name);
		out << "    if (str == \"" << escapeCppString(enum_val.value) << "\") {\n";
		out << "        return " << e.name << "::" << safe_name << ";\n";
		out << "    }\n";
	}
	out << "    return " << e.name << "::" << default_val << ";\n";
	out << "}\n\n";
}

} // namespace codegen
