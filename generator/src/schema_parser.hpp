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

public:
	/**
	 * Get C++ type for a primitive
	 */
	static std::string primitiveToCpp(
		PrimitiveKind kind,
		const std::optional<IntegerFormat>& fmt = std::nullopt,
		const std::optional<NumberFormat>& num_fmt = std::nullopt);
};

// Implementation
inline TypeRef SchemaParser::extractTypeRef(const openapi::v3::JsonSchema& schema) {
	if (schema.IsRef()) {
		std::string_view ref = schema.ref();
		size_t pos = ref.rfind('/');
		if (pos != std::string_view::npos) {
			// Sanitize the extracted type name
			std::string raw_name(ref.substr(pos + 1));
			std::string safe_name;
			safe_name.reserve(raw_name.size());
			for (char c : raw_name) {
				if (std::isalnum(c) || c == '_') {
					safe_name += c;
				} else {
					safe_name += '_';
				}
			}
			return TypeRef{safe_name, false};
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

inline std::string SchemaParser::primitiveToCpp(
	PrimitiveKind kind,
	const std::optional<IntegerFormat>& fmt,
	const std::optional<NumberFormat>& num_fmt) {
	switch (kind) {
	case PrimitiveKind::String:
		return "std::string";
	case PrimitiveKind::Integer:
		if (fmt && *fmt == IntegerFormat::Int64)
			return "int64_t";
		if (fmt && *fmt == IntegerFormat::UInt64)
			return "uint64_t";
		if (fmt && *fmt == IntegerFormat::UInt32)
			return "uint32_t";
		return "int32_t";
	case PrimitiveKind::Number:
		if (num_fmt && *num_fmt == NumberFormat::Double)
			return "double";
		return "float";
	case PrimitiveKind::Boolean:
		return "bool";
	case PrimitiveKind::Null:
		return "std::nullptr_t";
	}
	return "std::string";
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
				std::string mangled_name;
				mangled_name.reserve(raw_name.size());
				for (char c : raw_name) {
					if (std::isalnum(c) || c == '_') {
						mangled_name += c;
					} else {
						mangled_name += '_';
					}
				}

				cpp_type = mangled_name;

				if (added_types.find(mangled_name) == added_types.end()) {
					StructType mutable_t = *struct_ptr;
					mutable_t.name = mangled_name;
					ast.addType(mangled_name, std::move(mutable_t));
					added_types.insert(mangled_name);
				}
			} else if (const auto* prim_ptr = std::get_if<PrimitiveType>(&inline_schema_type)) {
				cpp_type = SchemaParser::primitiveToCpp(prim_ptr->kind, prim_ptr->int_format, prim_ptr->num_format);
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

inline SchemaType SchemaParser::parseSchema(
	const openapi::v3::JsonSchema& schema,
	std::string_view name,
	NormalizedAST& ast,
	std::unordered_set<std::string>& added_types) {
	auto type = schema.Type_();
	std::string desc = std::string(schema.description());

	switch (type) {
	case openapi::v3::JsonSchema::Type::object: {
		const auto& obj = static_cast<const openapi::v3::Object&>(schema);
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

		// TODO: Handle additionalProperties for map-like schemas
		// When additionalProperties: true and no properties exist, generate:
		//   using TypeName = std::map<std::string, boost::json::value>;
		// instead of an empty struct. This preserves semantic meaning for dynamic schemas.
		// See: https://swagger.io/docs/specification/data-models/objects/#dictionary

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
							cpp_type = primitiveToCpp(t.kind, t.int_format, t.num_format);
						} else if constexpr (std::is_same_v<T, ArrayType>) {
							cpp_type = "std::vector<" + cppTypeFromTypeRef(t.element_type) + ">";
						} else if constexpr (std::is_same_v<T, MapType>) {
							cpp_type = "std::map<std::string, " + cppTypeFromTypeRef(t.value_type) + ">";
						} else if constexpr (std::is_same_v<T, StructType>) {
							// Inline struct within a parent struct - keep local
							std::string raw_name = std::string(name) + "_" + std::string(prop_name);
							// Sanitize name: replace invalid characters with underscores
							std::string mangled_name;
							mangled_name.reserve(raw_name.size());
							for (char c : raw_name) {
								if (std::isalnum(c) || c == '_') {
									mangled_name += c;
								} else {
									mangled_name += '_';
								}
							}

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
						elem_cpp_type = primitiveToCpp(t.kind, t.int_format, t.num_format);
					} else if constexpr (std::is_same_v<T, StructType>) {
						// Sanitize name: replace invalid characters with underscores
						std::string sanitized_name;
						sanitized_name.reserve(elem_name.size());
						for (char c : elem_name) {
							if (std::isalnum(c) || c == '_') {
								sanitized_name += c;
							} else {
								sanitized_name += '_';
							}
						}

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
			VariantType variant;
			variant.name = std::string(name);
			variant.description = desc;

			// TODO: Extract discriminator information for runtime polymorphic dispatch
			// OpenAPI spec supports discriminator.propertyName to identify which alternative matches
			// Currently we generate std::variant but don't store discriminator metadata
			// Future work: Parse schema.discriminator() and populate variant.discriminator_property
			// See: https://swagger.io/docs/specification/data-models/oneof-anyof-allof/

			for (const auto& alt : oneOf) {
				// Try to extract $ref first
				auto alt_ref = extractTypeRef(alt);
				if (!alt_ref.name.empty()) {
					variant.alternatives.push_back(alt_ref);
					continue;
				}

				// If no $ref, parse inline schema and create a type reference
				std::string alt_type_name;
				// Pass the parent variant name to ensure proper naming of inline types
				auto alt_type = parseSchema(alt, std::string(name) + "_inline", ast, added_types);
				std::visit(
					[&](const auto& t) {
						using T = std::decay_t<decltype(t)>;

						if constexpr (std::is_same_v<T, PrimitiveType>) {
							// For primitives in oneOf, use the C++ type string directly
							alt_type_name = primitiveToCpp(t.kind, t.int_format, t.num_format);
						} else if constexpr (std::is_same_v<T, ArrayType>) {
							// For arrays in oneOf, use "std::vector<Element>" as the alternative name
							alt_type_name = "std::vector<" + cppTypeFromTypeRef(t.element_type) + ">";
						} else if constexpr (std::is_same_v<T, StructType>) {
							// Inline struct in oneOf - sanitize and add to AST
							std::string raw_name =
								std::string(name) + "_alt_" + std::to_string(variant.alternatives.size());
							std::string sanitized_name;
							sanitized_name.reserve(raw_name.size());
							for (char c : raw_name) {
								if (std::isalnum(c) || c == '_') {
									sanitized_name += c;
								} else {
									sanitized_name += '_';
								}
							}

							auto mutable_t = t;
							mutable_t.name = sanitized_name;
							alt_type_name = sanitized_name;

							if (added_types.find(sanitized_name) == added_types.end()) {
								ast.addType(sanitized_name, std::move(mutable_t));
								added_types.insert(sanitized_name);
							}
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

		// Check for anyOf (similar to oneOf but allows multiple matches)
		auto anyOf = schema.anyOf();
		if (!anyOf.empty()) {
			VariantType variant;
			variant.name = std::string(name);
			variant.description = desc;

			for (const auto& alt : anyOf) {
				// Try to extract $ref first
				auto alt_ref = extractTypeRef(alt);
				if (!alt_ref.name.empty()) {
					variant.alternatives.push_back(alt_ref);
					continue;
				}

				// If no $ref, parse inline schema and create a type reference
				std::string alt_type_name;
				// Pass the parent variant name to ensure proper naming of inline types
				auto alt_type = parseSchema(alt, std::string(name) + "_inline", ast, added_types);
				std::visit(
					[&](const auto& t) {
						using T = std::decay_t<decltype(t)>;

						if constexpr (std::is_same_v<T, PrimitiveType>) {
							// For primitives in anyOf, use the C++ type string directly
							alt_type_name = primitiveToCpp(t.kind, t.int_format, t.num_format);
						} else if constexpr (std::is_same_v<T, ArrayType>) {
							alt_type_name = "std::vector<" + cppTypeFromTypeRef(t.element_type) + ">";
						} else if constexpr (std::is_same_v<T, StructType>) {
							// Inline struct in anyOf - sanitize and add to AST
							std::string raw_name =
								std::string(name) + "_alt_" + std::to_string(variant.alternatives.size());
							std::string sanitized_name;
							sanitized_name.reserve(raw_name.size());
							for (char c : raw_name) {
								if (std::isalnum(c) || c == '_') {
									sanitized_name += c;
								} else {
									sanitized_name += '_';
								}
							}

							auto mutable_t = t;
							mutable_t.name = sanitized_name;
							alt_type_name = sanitized_name;

							if (added_types.find(sanitized_name) == added_types.end()) {
								ast.addType(sanitized_name, std::move(mutable_t));
								added_types.insert(sanitized_name);
							}
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

		// Check for implicit object (has properties, allOf, or additionalProperties but no explicit type)
		// We need to cast to Object to access properties()/allOf()
		// Only do this if the schema actually has those keys
		if (schema.HasKey("properties") || schema.HasKey("allOf") || schema.HasKey("additionalProperties")) {
			const auto& obj_schema = static_cast<const openapi::v3::Object&>(schema);
			return parseImplicitObject(obj_schema, name, ast, added_types);
		}

		// Truly unknown schema - warn and fallback to string
		std::cerr << "    [WARNING] Unknown schema type for '" << name << "' - defaulting to std::string\n";
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
