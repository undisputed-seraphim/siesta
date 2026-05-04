// SPDX-License-Identifier: Apache-2.0
#include "schema_parser.hpp"
#include "util.hpp"

namespace schema {

// --- Simple accessor helpers ---

TypeRef SchemaParser::extractTypeRef(const openapi::v3::JsonSchema& schema) {
	if (schema.IsRef()) {
		std::string_view ref = schema.ref();
		size_t pos = ref.rfind('/');
		if (pos != std::string_view::npos) {
			std::string raw_name(ref.substr(pos + 1));
			return TypeRef{codegen::sanitize(std::string_view(raw_name)), false};
		}
	}
	return TypeRef{};
}

std::optional<IntegerFormat> SchemaParser::parseIntegerFormat(std::string_view format) {
	if (format == "int32")  return IntegerFormat::Int32;
	if (format == "int64")  return IntegerFormat::Int64;
	if (format == "uint32") return IntegerFormat::UInt32;
	if (format == "uint64") return IntegerFormat::UInt64;
	return std::nullopt;
}

std::optional<NumberFormat> SchemaParser::parseNumberFormat(std::string_view format) {
	if (format == "float")  return NumberFormat::Float;
	if (format == "double") return NumberFormat::Double;
	return std::nullopt;
}

std::optional<StringFormat> SchemaParser::parseStringFormat(std::string_view format) {
	if (format == "date")      return StringFormat::Date;
	if (format == "date-time")  return StringFormat::DateTime;
	if (format == "email")      return StringFormat::Email;
	if (format == "uuid")       return StringFormat::UUID;
	if (format == "uri")        return StringFormat::URI;
	if (format == "byte" || format == "base64") return StringFormat::Base64;
	return std::nullopt;
}

std::string SchemaParser::cppTypeFromTypeRef(const TypeRef& ref) {
	if (ref.name.empty()) return "std::string";
	return ref.name;
}

// --- Sub-parsers extracted from parseSchema ---

SchemaType SchemaParser::parseObjectSchema(
	const openapi::v3::JsonSchema& schema,
	std::string_view name,
	NormalizedAST& ast,
	std::unordered_set<std::string>& added_types)
{
	const auto& obj = static_cast<const openapi::v3::Object&>(schema);
	std::string desc = std::string(schema.description());

	// oneOf/anyOf on an object type means variant
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
			if (!base_ref.name.empty())
				struct_type.allOf_bases.push_back(base_ref);
		}
	}

	auto props = obj.properties();
	for (const auto& [prop_name, prop_schema] : props) {
		Member member;
		member.name = codegen::sanitize(std::string(prop_name));
		member.description = std::string(prop_schema.description());

		if (prop_schema.IsRef()) {
			member.type = extractTypeRef(prop_schema);
		} else {
			auto inline_schema_type = parseSchema(
				prop_schema, std::string(name) + "_" + std::string(prop_name),
				ast, added_types);

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
						std::string raw = std::string(name) + "_" + std::string(prop_name);
						std::string mangled = codegen::sanitize(std::string_view(raw));
						auto mutable_t = t;
						mutable_t.name = mangled;
						cpp_type = mangled;
						if (added_types.find(mangled) == added_types.end()) {
							ast.addType(mangled, std::move(mutable_t));
							added_types.insert(mangled);
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

SchemaType SchemaParser::parseArraySchema(
	const openapi::v3::JsonSchema& schema,
	std::string_view name,
	NormalizedAST& ast,
	std::unordered_set<std::string>& added_types)
{
	const auto& arr = static_cast<const openapi::v3::Array&>(schema);
	ArrayType array_type;
	array_type.description = std::string(schema.description());

	auto items = arr.items();
	if (items.IsRef()) {
		array_type.element_type = extractTypeRef(items);
	} else {
		std::string elem_name = std::string(name) + "_entry";
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
					std::string san = codegen::sanitize(std::string_view(elem_name));
					auto mutable_t = t;
					mutable_t.name = san;
					elem_cpp_type = san;
					if (added_types.find(san) == added_types.end()) {
						ast.addType(san, std::move(mutable_t));
						added_types.insert(san);
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

SchemaType SchemaParser::parsePrimitiveSchema(
	const openapi::v3::JsonSchema& schema,
	std::string_view name,
	NormalizedAST& ast,
	std::unordered_set<std::string>& added_types)
{
	(void)name;
	(void)ast;
	(void)added_types;

	auto type = schema.Type_();
	std::string desc = std::string(schema.description());

	switch (type) {
	case openapi::v3::JsonSchema::Type::string: {
		PrimitiveType prim;
		prim.kind = PrimitiveKind::String;
		prim.str_format = parseStringFormat(schema.format());
		prim.description = desc;

		for (const auto& enum_val : schema.enum_()) {
			std::string_view sv;
			if (!enum_val.get_string().get(sv))
				prim.enum_values.emplace_back(sv);
		}
		return prim;
	}

	case openapi::v3::JsonSchema::Type::integer: {
		PrimitiveType prim;
		prim.kind = PrimitiveKind::Integer;
		prim.int_format = parseIntegerFormat(schema.format());
		prim.description = desc;

		for (const auto& enum_val : schema.enum_()) {
			int64_t num_val;
			if (!enum_val.get_int64().get(num_val))
				prim.enum_values.push_back(std::to_string(num_val));
			else {
				std::string_view sv;
				if (!enum_val.get_string().get(sv))
					prim.enum_values.emplace_back(sv);
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

	default:
		{
			PrimitiveType prim;
			prim.kind = PrimitiveKind::String;
			return prim;
		}
	}
}

SchemaType SchemaParser::parseUnknownSchema(
	const openapi::v3::JsonSchema& schema,
	std::string_view name,
	NormalizedAST& ast,
	std::unordered_set<std::string>& added_types)
{
	std::string desc = std::string(schema.description());

	auto oneOf = schema.oneOf();
	if (!oneOf.empty())
		return buildVariant(oneOf, name, desc, ast, added_types);

	auto anyOf = schema.anyOf();
	if (!anyOf.empty())
		return buildVariant(anyOf, name, desc, ast, added_types);

	if (schema.HasKey("properties") || schema.HasKey("allOf") || schema.HasKey("additionalProperties")) {
		const auto& obj_schema = static_cast<const openapi::v3::Object&>(schema);
		return parseImplicitObject(obj_schema, name, ast, added_types);
	}

	{
		PrimitiveType prim;
		prim.kind = PrimitiveKind::String;
		prim.description = desc;
		return prim;
	}
}

SchemaType SchemaParser::parseImplicitObject(
	const openapi::v3::Object& obj,
	std::string_view name,
	NormalizedAST& ast,
	std::unordered_set<std::string>& added_types)
{
	StructType struct_type;
	struct_type.name = std::string(name);
	struct_type.description = std::string(obj.description());

	for (const auto& allOf_schema : obj.allOf()) {
		if (allOf_schema.IsRef()) {
			auto base_ref = extractTypeRef(allOf_schema);
			if (!base_ref.name.empty())
				struct_type.allOf_bases.push_back(base_ref);
		} else {
			std::string inline_name = std::string(name) + "_base_" +
				std::to_string(struct_type.allOf_bases.size());
			auto inline_type = parseSchema(allOf_schema, inline_name, ast, added_types);

			const auto* struct_ptr = std::get_if<StructType>(&inline_type);
			if (struct_ptr) {
				std::string mangled_name = codegen::sanitize(struct_ptr->name);
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

	auto props = obj.properties();
	for (const auto& [prop_name, prop_schema] : props) {
		Member member;
		member.name = codegen::sanitize(std::string(prop_name));
		member.description = std::string(prop_schema.description());

		if (prop_schema.IsRef()) {
			member.type = extractTypeRef(prop_schema);
		} else {
			auto inline_schema_type = parseSchema(prop_schema, "", ast, added_types);
			std::string cpp_type;

			const auto* struct_ptr = std::get_if<StructType>(&inline_schema_type);
			if (struct_ptr) {
				std::string raw_name = std::string(name) + "_" + std::string(prop_name);
				std::string mangled_name = codegen::sanitize(std::string_view(raw_name));
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
				cpp_type = "std::vector<" + cppTypeFromTypeRef(arr_ptr->element_type) + ">";
			} else if (const auto* map_ptr = std::get_if<MapType>(&inline_schema_type)) {
				cpp_type = "std::map<std::string, " + cppTypeFromTypeRef(map_ptr->value_type) + ">";
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

VariantType SchemaParser::buildVariant(
	const openapi::v3::JsonSchema::SchemaList& alternatives,
	std::string_view name,
	std::string_view desc,
	NormalizedAST& ast,
	std::unordered_set<std::string>& added_types)
{
	VariantType variant;
	variant.name = std::string(name);
	variant.description = std::string(desc);

	for (const auto& alt : alternatives) {
		auto alt_ref = extractTypeRef(alt);
		if (!alt_ref.name.empty()) {
			variant.alternatives.push_back(alt_ref);
			continue;
		}

		std::string alt_type_name;
		auto alt_type = parseSchema(alt, std::string(name) + "_inline", ast, added_types);
		std::visit(
			[&](const auto& t) {
				using T = std::decay_t<decltype(t)>;

				if constexpr (std::is_same_v<T, PrimitiveType>) {
					alt_type_name = codegen::primitiveToCpp(t.kind, t.int_format, t.num_format);
				} else if constexpr (std::is_same_v<T, ArrayType>) {
					alt_type_name = "std::vector<" + cppTypeFromTypeRef(t.element_type) + ">";
				} else if constexpr (std::is_same_v<T, StructType>) {
					std::string raw_name = std::string(name) + "_alt_" +
						std::to_string(variant.alternatives.size());
					std::string sanitized_name = codegen::sanitize(std::string_view(raw_name));
					auto mutable_t = t;
					mutable_t.name = sanitized_name;
					alt_type_name = sanitized_name;
					if (added_types.find(sanitized_name) == added_types.end()) {
						ast.addType(sanitized_name, std::move(mutable_t));
						added_types.insert(sanitized_name);
					}
				} else if constexpr (std::is_same_v<T, VariantType>) {
					for (const auto& nested_alt : t.alternatives)
						variant.alternatives.push_back(nested_alt);
					alt_type_name.clear();
				} else {
					alt_type_name = "std::string";
				}
			},
			alt_type);

		if (!alt_type_name.empty())
			variant.alternatives.push_back(TypeRef{alt_type_name, true});
	}

	return variant;
}

// --- Main dispatch ---

SchemaType SchemaParser::parseSchema(
	const openapi::v3::JsonSchema& schema,
	std::string_view name,
	NormalizedAST& ast,
	std::unordered_set<std::string>& added_types)
{
	auto type = schema.Type_();

	switch (type) {
	case openapi::v3::JsonSchema::Type::object:
		return parseObjectSchema(schema, name, ast, added_types);

	case openapi::v3::JsonSchema::Type::array:
		return parseArraySchema(schema, name, ast, added_types);

	case openapi::v3::JsonSchema::Type::string:
	case openapi::v3::JsonSchema::Type::integer:
	case openapi::v3::JsonSchema::Type::number:
	case openapi::v3::JsonSchema::Type::boolean:
		return parsePrimitiveSchema(schema, name, ast, added_types);

	case openapi::v3::JsonSchema::Type::unknown:
		return parseUnknownSchema(schema, name, ast, added_types);

	default:
		{
			PrimitiveType prim;
			prim.kind = PrimitiveKind::String;
			prim.description = std::string(schema.description());
			return prim;
		}
	}
}

} // namespace schema
