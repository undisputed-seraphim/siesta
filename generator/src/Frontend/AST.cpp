// SPDX-License-Identifier: Apache-2.0
#include "Frontend/AST.hpp"

namespace schema {

std::vector<std::string> NormalizedAST::validate() const {
	std::vector<std::string> errors;

	for (const auto& [name, type] : types_) {
		validateType(type, name, errors);
	}

	return errors;
}

void NormalizedAST::validateType(const SchemaType& type, const std::string& context, std::vector<std::string>& errors) const {
	std::visit(
		[&](const auto& t) {
			using T = std::decay_t<decltype(t)>;

			if constexpr (std::is_same_v<T, StructType>) {
				for (const auto& field : t.fields) {
					if (!field.type.is_inline && !hasType(field.type.name)) {
						errors.push_back(
							context + ": field '" + field.name + "' references unknown type '" + field.type.name +
							"'");
					}
				}
				for (const auto& base : t.allOf_bases) {
					if (!hasType(base.name)) {
						errors.push_back(context + ": base type '" + base.name + "' not found");
					}
				}
			} else if constexpr (std::is_same_v<T, VariantType>) {
				for (const auto& alt : t.alternatives) {
					if (!alt.is_inline && !hasType(alt.name)) {
						errors.push_back(context + ": variant alternative '" + alt.name + "' not found");
					}
				}
			} else if constexpr (std::is_same_v<T, ArrayType>) {
				if (!t.element_type.is_inline && !hasType(t.element_type.name)) {
					errors.push_back(context + ": array element type '" + t.element_type.name + "' not found");
				}
			} else if constexpr (std::is_same_v<T, MapType>) {
				if (!t.value_type.is_inline && !hasType(t.value_type.name)) {
					errors.push_back(context + ": map value type '" + t.value_type.name + "' not found");
				}
			}
		},
		type);
}

} // namespace schema
