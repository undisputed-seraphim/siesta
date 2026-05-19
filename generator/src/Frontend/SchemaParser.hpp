// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "Frontend/openapi3.hpp"
#include "Frontend/AST.hpp"
#include "Support/Utils.hpp"
#include <cctype>
#include <optional>
#include <string_view>
#include <unordered_set>

namespace schema {

class SchemaParser {
public:
	static SchemaType parseSchema(
		const openapi::v3::JsonSchema& schema,
		std::string_view name,
		NormalizedAST& ast,
		std::unordered_set<std::string>& added_types);

	static TypeRef extractTypeRef(const openapi::v3::JsonSchema& schema);

	static std::string cppTypeFromTypeRef(const TypeRef& ref);

private:
	static std::optional<IntegerFormat> parseIntegerFormat(std::string_view format);
	static std::optional<NumberFormat> parseNumberFormat(std::string_view format);
	static std::optional<StringFormat> parseStringFormat(std::string_view format);

	static SchemaType parseObjectSchema(
		const openapi::v3::JsonSchema& schema,
		std::string_view name,
		NormalizedAST& ast,
		std::unordered_set<std::string>& added_types);

	static SchemaType parseArraySchema(
		const openapi::v3::JsonSchema& schema,
		std::string_view name,
		NormalizedAST& ast,
		std::unordered_set<std::string>& added_types);

	static SchemaType parsePrimitiveSchema(
		const openapi::v3::JsonSchema& schema,
		std::string_view name,
		NormalizedAST& ast,
		std::unordered_set<std::string>& added_types);

	static SchemaType parseUnknownSchema(
		const openapi::v3::JsonSchema& schema,
		std::string_view name,
		NormalizedAST& ast,
		std::unordered_set<std::string>& added_types);

	static SchemaType parseImplicitObject(
		const openapi::v3::Object& obj,
		std::string_view name,
		NormalizedAST& ast,
		std::unordered_set<std::string>& added_types);

	static VariantType buildVariant(
		const openapi::v3::JsonSchema::SchemaList& alternatives,
		std::string_view name,
		std::string_view desc,
		NormalizedAST& ast,
		std::unordered_set<std::string>& added_types);
};

} // namespace schema
