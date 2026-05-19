// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "IR/CodegenArgs.hpp"
#include "IR/DependencyGraph.hpp"
#include "Frontend/AST.hpp"
#include "Support/Utils.hpp"
#include <ostream>
#include <string>

namespace codegen {

struct DefsEmitState {
	std::unordered_map<std::string, std::string> variant_signatures;
	std::unordered_map<std::string, std::string> typedef_chain;
	struct Preprocessed {
		std::vector<schema::TypeRef> alternatives;
		bool should_collapse = false;
		std::string signature;
	};
	std::unordered_map<std::string, Preprocessed> preprocessed;
};

class DefsGenerator : public ICodeGenerator {
public:
	void operator()(const CodegenArgs& args, const std::filesystem::path& output_dir) override;

private:
	void generateDefsHpp(std::ostream& out,
	                     const analysis::TopologicalOrder& order,
	                     const schema::NormalizedAST& ast);
	void generateDefsCpp(std::ostream& out,
	                     const analysis::TopologicalOrder& order,
	                     const schema::NormalizedAST& ast);

	void emitStruct(std::ostream& out, const schema::StructType& s);
	void emitStructSerialization(std::ostream& out, const schema::StructType& s);
	void emitVariant(std::ostream& out, const schema::VariantType& v, const schema::NormalizedAST& ast, DefsEmitState&);
	void emitVariantSerialization(std::ostream& out, const schema::VariantType& v, const schema::NormalizedAST& ast, const DefsEmitState&);
	void emitEnum(std::ostream& out, const schema::EnumType& e);
	void emitEnumSerialization(std::ostream& out, const schema::EnumType& e);
	void emitPrimitiveTypedef(std::ostream& out, const std::string& name, const schema::PrimitiveType& p);
	void emitEnumFromPrimitive(std::ostream& out, const std::string& name, const schema::PrimitiveType& p);
	void emitEnumFromPrimitiveSerialization(std::ostream& out, const std::string& name, const schema::PrimitiveType& p);
	void emitArrayAlias(std::ostream& out, const std::string& name, const schema::ArrayType& arr);
	void emitMapAlias(std::ostream& out, const std::string& name, const schema::MapType& m);

	std::string cppTypeName(const schema::TypeRef& ref) const;
	std::string cppTypeName(const schema::SchemaType& type) const;

	std::string ns_;
};

// Implementation

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
				result = codegen::primitiveToCpp(t.kind, t.int_format, t.num_format);
			}
		},
		type);
	return result;
}

} // namespace codegen
