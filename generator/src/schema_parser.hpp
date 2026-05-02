// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "openapi3.hpp"
#include "schema_ast.hpp"
#include "util.hpp"
#include <cctype>
#include <string_view>
#include <unordered_set>

namespace schema {

/**
 * Parse OpenAPI v3 JsonSchema into our normalized AST types
 */
class SchemaParser {
public:
	/**
	 * Parse a single schema definition
	 * If schema is inline (object/array within another), adds it to ast with mangled name
	 */
	static SchemaType parseSchema(
		const openapi::v3::JsonSchema& schema,
		std::string_view name,
		NormalizedAST& ast,
		std::unordered_set<std::string>& added_types);

	/**
	 * Extract type reference from schema (handles $ref and inline types)
	 */
	static TypeRef extractTypeRef(const openapi::v3::JsonSchema& schema);

	/**
	 * Get C++ type string for a TypeRef (handles both named and inline types)
	 */
	static std::string cppTypeFromTypeRef(const TypeRef& ref);

private:
	/**
	 * Helper to convert format string to our format enums
	 */
	static std::optional<IntegerFormat> parseIntegerFormat(std::string_view format);
	static std::optional<NumberFormat> parseNumberFormat(std::string_view format);
	static std::optional<StringFormat> parseStringFormat(std::string_view format);
};

// Implementation
inline TypeRef SchemaParser::extractTypeRef(const openapi::v3::JsonSchema& schema) {
	if (schema.IsRef()) {
		std::string_view ref = schema.ref();
		size_t pos = ref.rfind('/');
		if (pos != std::string_view::npos) {
			// Sanitize the extracted type name
			std::string raw_name(ref.substr(pos + 1));
			return TypeRef{sanitize(std::string_view(raw_name)), false};
		}
	}
	return TypeRef{};
}

inline std::optional<IntegerFormat> SchemaParser::parseIntegerFormat(std::string_view format) {
	if (format == "int32")
		return IntegerFormat::Int32;
	if (format == "int64")
		return IntegerFormat::Int64;
	if (format == "uint32")
		return IntegerFormat::UInt32;
	if (format == "uint64")
		return IntegerFormat::UInt64;
	return std::nullopt;
}

inline std::optional<NumberFormat> SchemaParser::parseNumberFormat(std::string_view format) {
	if (format == "float")
		return NumberFormat::Float;
	if (format == "double")
		return NumberFormat::Double;
	return std::nullopt;
}

inline std::optional<StringFormat> SchemaParser::parseStringFormat(std::string_view format) {
	if (format == "date")
		return StringFormat::Date;
	if (format == "date-time")
		return StringFormat::DateTime;
	if (format == "email")
		return StringFormat::Email;
	if (format == "uuid")
		return StringFormat::UUID;
	if (format == "uri")
		return StringFormat::URI;
	if (format == "byte" || format == "base64")
		return StringFormat::Base64;
	return std::nullopt;
}

inline std::string SchemaParser::cppTypeFromTypeRef(const TypeRef& ref) {
	if (ref.name.empty()) {
		return "std::string"; // fallback for broken inline types
	}
	return ref.name;
}

/**
 * Parse a schema that is implicitly an object (has properties/allOf/anyOf but no explicit type)
 */
static SchemaType parseImplicitObject(
	const openapi::v3::Object& obj,
	std::string_view name,
	NormalizedAST& ast,
	std::unordered_set<std::string>& added_types) {

	StructType struct_type;
	struct_type.name = std::string(name);
	struct_type.description = std::string(obj.description());

	// Process allOf bases (inheritance)
	for (const auto& allOf_schema : obj.allOf()) {
		if (allOf_schema.IsRef()) {
			auto base_ref = SchemaParser::extractTypeRef(allOf_schema);
			if (!base_ref.name.empty()) {
				struct_type.allOf_bases.push_back(base_ref);
			}
		} else {
			// Inline schema in allOf - parse recursively
			std::string inline_name = std::string(name) + "_base_" + std::to_string(struct_type.allOf_bases.size());
			auto inline_type = SchemaParser::parseSchema(allOf_schema, inline_name, ast, added_types);

			const auto* struct_ptr = std::get_if<StructType>(&inline_type);
			if (struct_ptr) {
				// Inline struct in allOf - add to AST with mangled name
				std::string mangled_name = sanitize(struct_ptr->name);
				struct_type.allOf_bases.push_back(TypeRef{mangled_name, false});

				if (added_types.find(mangled_name) == added_types.end()) {
					StructType mutable_t = *struct_ptr;
					mutable_t.name = mangled_name;
					ast.addType(mangled_name, std::move(mutable_t));
					added_types.insert(mangled_name);
				}
			}
		}
	}

	// Process properties
	auto props = obj.properties();
	for (const auto& [prop_name, prop_schema] : props) {
		Member member;
		member.name = sanitize(std::string(prop_name));
		member.description = std::string(prop_schema.description());

		if (prop_schema.IsRef()) {
			member.type = SchemaParser::extractTypeRef(prop_schema);
		} else {
			auto inline_schema_type = SchemaParser::parseSchema(prop_schema, "", ast, added_types);
			std::string cpp_type;

			const auto* struct_ptr = std::get_if<StructType>(&inline_schema_type);
			if (struct_ptr) {
				// Inline struct within a parent struct - keep local
				std::string raw_name = std::string(name) + "_" + std::string(prop_name);
				std::string mangled_name = sanitize(std::string_view(raw_name));

				cpp_type = mangled_name;

				if (added_types.find(mangled_name) == added_types.end()) {
					StructType mutable_t = *struct_ptr;
					mutable_t.name = mangled_name;
					ast.addType(mangled_name, std::move(mutable_t));
					added_types.insert(mangled_name);
				}
			} else if (const auto* prim_ptr = std::get_if<PrimitiveType>(&inline_schema_type)) {
				cpp_type = codegen::primitiveToCpp(prim_ptr->kind, prim_ptr->int_format, prim_ptr->num_format);
			} else if (const auto* arr_ptr = std::get_if<ArrayType>(&inline_schema_type)) {
				cpp_type = "std::vector<" + SchemaParser::cppTypeFromTypeRef(arr_ptr->element_type) + ">";
			} else if (const auto* map_ptr = std::get_if<MapType>(&inline_schema_type)) {
				cpp_type = "std::map<std::string, " + SchemaParser::cppTypeFromTypeRef(map_ptr->value_type) + ">";
			} else {
				cpp_type = "std::string";
			}

			member.type.is_inline = true;
			member.type.name = cpp_type;
		}

		member.required = false;
		struct_type.fields.push_back(std::move(member));
	}

	return struct_type;
}

/**
 * Build a variant type from a list of alternatives (used for oneOf/anyOf)
 */
static VariantType buildVariant(
	const openapi::v3::JsonSchema::SchemaList& alternatives,
	std::string_view name,
	std::string_view desc,
	NormalizedAST& ast,
	std::unordered_set<std::string>& added_types) {
	VariantType variant;
	variant.name = std::string(name);
	variant.description = std::string(desc);

	size_t i = 0;
	for (const auto& alt : alternatives) {
		auto alt_ref = SchemaParser::extractTypeRef(alt);
		if (!alt_ref.name.empty()) {
			variant.alternatives.push_back(alt_ref);
			++i;
			continue;
		}

		std::string alt_type_name;
		auto alt_type = SchemaParser::parseSchema(alt, std::string(name) + "_inline", ast, added_types);
		std::visit(
			[&](const auto& t) {
				using T = std::decay_t<decltype(t)>;

				if constexpr (std::is_same_v<T, PrimitiveType>) {
					alt_type_name = codegen::primitiveToCpp(t.kind, t.int_format, t.num_format);
				} else if constexpr (std::is_same_v<T, ArrayType>) {
					alt_type_name = "std::vector<" + SchemaParser::cppTypeFromTypeRef(t.element_type) + ">";
				} else if constexpr (std::is_same_v<T, StructType>) {
					std::string raw_name = std::string(name) + "_alt_" + std::to_string(variant.alternatives.size());
					std::string sanitized_name = sanitize(std::string_view(raw_name));

					auto mutable_t = t;
					mutable_t.name = sanitized_name;
					alt_type_name = sanitized_name;

					if (added_types.find(sanitized_name) == added_types.end()) {
						ast.addType(sanitized_name, std::move(mutable_t));
						added_types.insert(sanitized_name);
					}
				} else if constexpr (std::is_same_v<T, VariantType>) {
					// Nested variant - flatten its alternatives into the parent
					for (const auto& nested_alt : t.alternatives) {
						variant.alternatives.push_back(nested_alt);
					}
					alt_type_name.clear(); // Don't add a duplicate
				} else {
					alt_type_name = "std::string";
				}
			},
			alt_type);

		if (!alt_type_name.empty()) {
			variant.alternatives.push_back(TypeRef{alt_type_name, true});
		}
	}

	return variant;
}

inline SchemaType SchemaParser::parseSchema(
	const openapi::v3::JsonSchema& schema,
	std::string_view name,
	NormalizedAST& ast,
	std::unordered_set<std::string>& added_types) {
	auto type = schema.Type_();
	std::string desc = std::string(schema.description());

	// Log entry: schema name, detected type, and what keys are present
	std::string keys_present;
	if (schema.IsRef())
		keys_present += " $ref";
	if (!schema.oneOf().empty())
		keys_present += " oneOf";
	if (!schema.anyOf().empty())
		keys_present += " anyOf";
	if (schema.HasKey("properties"))
		keys_present += " properties";
	if (schema.HasKey("allOf"))
		keys_present += " allOf";
	if (schema.HasKey("additionalProperties"))
		keys_present += " additionalProperties";
	if (!schema.enum_().empty())
		keys_present += " enum";
	if (!schema.format().empty())
		keys_present += " format=" + std::string(schema.format());

	switch (type) {
	case openapi::v3::JsonSchema::Type::object: {
		const auto& obj = static_cast<const openapi::v3::Object&>(schema);

		// Check if this object also has oneOf/anyOf — treat as variant (polymorphic object)
		bool has_oneof = !schema.oneOf().empty();
		bool has_anyof = !schema.anyOf().empty();
		if (has_oneof || has_anyof) {
			return buildVariant(has_oneof ? schema.oneOf() : schema.anyOf(), name, desc, ast, added_types);
		}

		StructType struct_type;
		struct_type.name = std::string(name);
		struct_type.description = desc;

		for (const auto& allOf_schema : obj.allOf()) {
			if (allOf_schema.IsRef()) {
				auto base_ref = extractTypeRef(allOf_schema);
				if (!base_ref.name.empty()) {
					struct_type.allOf_bases.push_back(base_ref);
				}
			}
		}

		auto props = obj.properties();
		for (const auto& [prop_name, prop_schema] : props) {
			Member member;
			member.name = sanitize(std::string(prop_name));
			member.description = std::string(prop_schema.description());

			if (prop_schema.IsRef()) {
				member.type = extractTypeRef(prop_schema);
			} else {
				// Pass parent struct name for proper inline type naming
				auto inline_schema_type =
					parseSchema(prop_schema, std::string(name) + "_" + std::string(prop_name), ast, added_types);
				std::string cpp_type;
				std::visit(
					[&](const auto& t) {
						using T = std::decay_t<decltype(t)>;

						if constexpr (std::is_same_v<T, PrimitiveType>) {
							cpp_type = codegen::primitiveToCpp(t.kind, t.int_format, t.num_format);
						} else if constexpr (std::is_same_v<T, ArrayType>) {
							cpp_type = "std::vector<" + cppTypeFromTypeRef(t.element_type) + ">";
						} else if constexpr (std::is_same_v<T, MapType>) {
							cpp_type = "std::map<std::string, " + cppTypeFromTypeRef(t.value_type) + ">";
						} else if constexpr (std::is_same_v<T, StructType>) {
							// Inline struct within a parent struct - keep local
							std::string raw_name = std::string(name) + "_" + std::string(prop_name);
							std::string mangled_name = sanitize(std::string_view(raw_name));

							// Set the struct's name before adding to AST
							auto mutable_t = t;
							mutable_t.name = mangled_name;
							cpp_type = mangled_name;

							// Add the inline struct to AST if not already added
							if (added_types.find(mangled_name) == added_types.end()) {
								ast.addType(mangled_name, std::move(mutable_t));
								added_types.insert(mangled_name);
							}
						} else {
							cpp_type = "std::string";
						}
					},
					inline_schema_type);

				member.type.is_inline = true;
				member.type.name = cpp_type;
			}

			member.required = false;
			struct_type.fields.push_back(std::move(member));
		}

		return struct_type;
	}

	case openapi::v3::JsonSchema::Type::array: {
		const auto& arr = static_cast<const openapi::v3::Array&>(schema);
		ArrayType array_type;
		array_type.description = desc;

		auto items = arr.items();
		if (items.IsRef()) {
			array_type.element_type = extractTypeRef(items);
		} else {
			std::string elem_name = std::string(name) + "_entry";

			// For top-level arrays (name is empty), use a unique name based on array type
			if (name.empty()) {
				elem_name = "ArrayEntry_" + std::to_string(ast.getTypeCount());
			}

			auto elem_schema_type = parseSchema(items, elem_name, ast, added_types);
			std::string elem_cpp_type;
			std::visit(
				[&](const auto& t) {
					using T = std::decay_t<decltype(t)>;

					if constexpr (std::is_same_v<T, PrimitiveType>) {
						elem_cpp_type = codegen::primitiveToCpp(t.kind, t.int_format, t.num_format);
					} else if constexpr (std::is_same_v<T, StructType>) {
						// Sanitize name: replace invalid characters with underscores
						std::string sanitized_name = sanitize(std::string_view(elem_name));

						// Set the struct's name before adding to AST
						auto mutable_t = t;
						mutable_t.name = sanitized_name;
						elem_cpp_type = sanitized_name;

						if (added_types.find(sanitized_name) == added_types.end()) {
							ast.addType(sanitized_name, std::move(mutable_t));
							added_types.insert(sanitized_name);
						}
					} else {
						elem_cpp_type = "std::string";
					}
				},
				elem_schema_type);

			array_type.element_type.is_inline = true;
			array_type.element_type.name = elem_cpp_type;
		}

		return array_type;
	}

	case openapi::v3::JsonSchema::Type::string: {
		PrimitiveType prim;
		prim.kind = PrimitiveKind::String;
		prim.str_format = parseStringFormat(schema.format());
		prim.description = desc;

		// Extract enum values if present
		auto enum_vals = schema.enum_();
		for (const auto& enum_val : enum_vals) {
			std::string_view sv;
			// simdjson get_string() returns error code (0 = success)
			if (!enum_val.get_string().get(sv)) {
				prim.enum_values.emplace_back(sv);
			}
		}

		return prim;
	}

	case openapi::v3::JsonSchema::Type::integer: {
		PrimitiveType prim;
		prim.kind = PrimitiveKind::Integer;
		prim.int_format = parseIntegerFormat(schema.format());
		prim.description = desc;

		// Extract enum values if present
		auto enum_vals = schema.enum_();
		for (const auto& enum_val : enum_vals) {
			int64_t num_val;
			if (!enum_val.get_int64().get(num_val)) {
				prim.enum_values.push_back(std::to_string(num_val));
			} else {
				// Try as string
				std::string_view sv;
				if (!enum_val.get_string().get(sv)) {
					prim.enum_values.emplace_back(sv);
				}
			}
		}

		return prim;
	}

	case openapi::v3::JsonSchema::Type::number: {
		PrimitiveType prim;
		prim.kind = PrimitiveKind::Number;
		prim.num_format = parseNumberFormat(schema.format());
		prim.description = desc;
		return prim;
	}

	case openapi::v3::JsonSchema::Type::boolean: {
		PrimitiveType prim;
		prim.kind = PrimitiveKind::Boolean;
		prim.description = desc;
		return prim;
	}

	case openapi::v3::JsonSchema::Type::unknown: {
		// Check for oneOf first (variant type)
		auto oneOf = schema.oneOf();
		if (!oneOf.empty()) {
			// TODO: Extract discriminator information for runtime polymorphic dispatch
			// OpenAPI spec supports discriminator.propertyName to identify which alternative matches
			// Currently we generate std::variant but don't store discriminator metadata
			// Future work: Parse schema.discriminator() and populate variant.discriminator_property
			// See: https://swagger.io/docs/specification/data-models/oneof-anyof-allof/
			return buildVariant(oneOf, name, desc, ast, added_types);
		}

		// Check for anyOf (similar to oneOf but allows multiple matches)
		auto anyOf = schema.anyOf();
		if (!anyOf.empty()) {
			return buildVariant(anyOf, name, desc, ast, added_types);
		}

		// Check for implicit object (has properties, allOf, or additionalProperties but no explicit type)
		// We need to cast to Object to access properties()/allOf()
		// Only do this if the schema actually has those keys
		if (schema.HasKey("properties") || schema.HasKey("allOf") || schema.HasKey("additionalProperties")) {
			const auto& obj_schema = static_cast<const openapi::v3::Object&>(schema);
			return parseImplicitObject(obj_schema, name, ast, added_types);
		}

		// Truly unknown schema - fallback to string
		PrimitiveType prim;
		prim.kind = PrimitiveKind::String;
		prim.description = desc;
		return prim;
	}

	default: {
		PrimitiveType prim;
		prim.kind = PrimitiveKind::String;
		prim.description = desc;
		return prim;
	}
	}
}

} // namespace schema
