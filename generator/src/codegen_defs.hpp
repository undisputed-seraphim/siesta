// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "dependency_graph.hpp"
#include "schema_ast.hpp"
#include <ostream>
#include <string>

namespace codegen {

/**
 * C++ Code Generator for defs.hpp and defs.cpp
 */
class DefsGenerator {
public:
	DefsGenerator(const analysis::TopologicalOrder& order, const schema::NormalizedAST& ast)
		: order_(order)
		, ast_(ast) {}

	/**
	 * Generate defs.hpp content
	 */
	void generateDefsHpp(std::ostream& out);

	/**
	 * Generate defs.cpp content
	 */
	void generateDefsCpp(std::ostream& out);

private:
	/**
	 * Emit a single struct definition
	 */
	void emitStruct(std::ostream& out, const schema::StructType& s);

	/**
	 * Emit struct serialization code
	 */
	void emitStructSerialization(std::ostream& out, const schema::StructType& s);

	/**
	 * Emit a single variant definition
	 */
	void emitVariant(std::ostream& out, const schema::VariantType& v);

	/**
	 * Emit variant serialization code
	 */
	void emitVariantSerialization(std::ostream& out, const schema::VariantType& v);

	// Arrays and maps are inline types - handled in struct fields

	/**
	 * Emit a single enum definition
	 */
	void emitEnum(std::ostream& out, const schema::EnumType& e);

	/**
	 * Emit enum serialization code
	 */
	void emitEnumSerialization(std::ostream& out, const schema::EnumType& e);

	/**
	 * Emit typedef for named primitive with enum values (deprecated - use emitEnumFromPrimitive)
	 */
	void emitPrimitiveTypedef(std::ostream& out, const std::string& name, const schema::PrimitiveType& p);

	/**
	 * Emit enum class from primitive type with enum values
	 */
	void emitEnumFromPrimitive(std::ostream& out, const std::string& name, const schema::PrimitiveType& p);

	/**
	 * Emit serialization for enum class from primitive
	 */
	void emitEnumFromPrimitiveSerialization(std::ostream& out, const std::string& name, const schema::PrimitiveType& p);

	/**
	 * Emit type alias for top-level array schema
	 */
	void emitArrayAlias(std::ostream& out, const std::string& name, const schema::ArrayType& arr);

	/**
	 * Emit type alias for top-level map schema
	 */
	void emitMapAlias(std::ostream& out, const std::string& name, const schema::MapType& m);

	/**
	 * Get C++ type name for a TypeRef
	 */
	std::string cppTypeName(const schema::TypeRef& ref) const;

	/**
	 * Get C++ type name for a SchemaType
	 */
	std::string cppTypeName(const schema::SchemaType& type) const;

	/**
	 * Convert primitive kind to C++ type
	 */
	static std::string primitiveToCpp(
		schema::PrimitiveKind kind,
		const std::optional<schema::IntegerFormat>& fmt = std::nullopt,
		const std::optional<schema::NumberFormat>& num_fmt = std::nullopt);

	/**
	 * Escape string for C++
	 */
	static std::string escapeCppString(const std::string& s);

	const analysis::TopologicalOrder& order_;
	const schema::NormalizedAST& ast_;

	// Track emitted variant signatures for duplicate detection
	mutable std::unordered_map<std::string, std::string> emitted_variant_signatures_;

	// Track typedef chains for resolution (e.g., ModelIdsShared -> Verbosity -> std::string)
	mutable std::unordered_map<std::string, std::string> typedef_chain_;
};

// Implementation
inline std::string DefsGenerator::primitiveToCpp(
	schema::PrimitiveKind kind,
	const std::optional<schema::IntegerFormat>& fmt,
	const std::optional<schema::NumberFormat>& num_fmt) {
	switch (kind) {
	case schema::PrimitiveKind::String:
		return "std::string";
	case schema::PrimitiveKind::Integer:
		if (fmt && *fmt == schema::IntegerFormat::Int64)
			return "int64_t";
		if (fmt && *fmt == schema::IntegerFormat::UInt64)
			return "uint64_t";
		if (fmt && *fmt == schema::IntegerFormat::UInt32)
			return "uint32_t";
		return "int32_t";
	case schema::PrimitiveKind::Number:
		if (num_fmt && *num_fmt == schema::NumberFormat::Double)
			return "double";
		return "float";
	case schema::PrimitiveKind::Boolean:
		return "bool";
	case schema::PrimitiveKind::Null:
		return "std::nullptr_t";
	}
	return "std::string";
}

inline std::string DefsGenerator::cppTypeName(const schema::TypeRef& ref) const {
	// For inline types, the name contains the full C++ type (e.g., "std::string", "int64_t")
	// For named types, just return the name
	return ref.name;
}

inline std::string DefsGenerator::cppTypeName(const schema::SchemaType& type) const {
	std::string result;
	std::visit(
		[&](const auto& t) {
			using T = std::decay_t<decltype(t)>;

			if constexpr (std::is_same_v<T, schema::StructType>) {
				result = t.name;
			} else if constexpr (std::is_same_v<T, schema::VariantType>) {
				result = t.name;
			} else if constexpr (std::is_same_v<T, schema::ArrayType>) {
				result = "std::vector<" + cppTypeName(t.element_type) + ">";
			} else if constexpr (std::is_same_v<T, schema::MapType>) {
				result = "std::map<std::string, " + cppTypeName(t.value_type) + ">";
			} else if constexpr (std::is_same_v<T, schema::EnumType>) {
				result = t.name;
			} else if constexpr (std::is_same_v<T, schema::PrimitiveType>) {
				result = primitiveToCpp(t.kind, t.int_format, t.num_format);
			}
		},
		type);
	return result;
}

inline std::string DefsGenerator::escapeCppString(const std::string& s) {
	std::string result;
	for (char c : s) {
		switch (c) {
		case '\\':
			result += "\\\\";
			break;
		case '"':
			result += "\\\"";
			break;
		case '\n':
			result += "\\n";
			break;
		case '\r':
			result += "\\r";
			break;
		case '\t':
			result += "\\t";
			break;
		default:
			result += c;
		}
	}
	return result;
}

} // namespace codegen
