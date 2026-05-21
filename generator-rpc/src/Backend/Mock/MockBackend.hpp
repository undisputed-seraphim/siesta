// SPDX-License-Identifier: Apache-2.0
#pragma once
/// Mock/debug backend — prints the middle-end output to stdout.
/// Does not generate any real code.  Used to validate the frontend → IR pipeline.

#include "IR/RPCIR.hpp"
#include "IR/CodegenArgs.hpp"
#include "Frontend/AST.hpp"
#include <iostream>
#include <ostream>
#include <vector>

namespace backend::mock {

inline void debug_print(const schema::NormalizedAST& ast,
                         const std::vector<codegen::rpc::RPCMethod>& methods,
                         std::ostream& out = std::cout) {
	out << "═══════════════════════════════════════════\n";
	out << "  Mock Backend — Middle-End Debug Output\n";
	out << "═══════════════════════════════════════════\n\n";

	// ── AST types ─────────────────────────────────────────
	out << "── Schema Types (" << ast.getTypes().size() << ") ──\n";
	for (const auto& [name, type] : ast.getTypes()) {
		std::visit([&](const auto& t) {
			using T = std::decay_t<decltype(t)>;
			if constexpr (std::is_same_v<T, schema::StructType>) {
				out << "  struct " << name << " { ";
				for (size_t i = 0; i < t.fields.size(); ++i) {
					if (i) out << ", ";
					out << t.fields[i].name << ": " << t.fields[i].type.name;
				}
				out << " }\n";
			} else if constexpr (std::is_same_v<T, schema::VariantType>) {
				out << "  variant " << name << " (nullable=" << t.is_nullable << ") [";
				for (size_t i = 0; i < t.alternatives.size(); ++i) {
					if (i) out << " | ";
					out << t.alternatives[i].name;
				}
				out << "]\n";
			} else if constexpr (std::is_same_v<T, schema::EnumType>) {
				out << "  enum " << name << " { ";
				for (size_t i = 0; i < t.values.size(); ++i) {
					if (i) out << ", ";
					out << t.values[i].name;
				}
				out << " }\n";
			} else if constexpr (std::is_same_v<T, schema::ArrayType>) {
				out << "  array  " << name << " = vector<" << t.element_type.name << ">\n";
			} else if constexpr (std::is_same_v<T, schema::MapType>) {
				out << "  map    " << name << " = map<string, " << t.value_type.name << ">\n";
			} else if constexpr (std::is_same_v<T, schema::PrimitiveType>) {
				out << "  prim   " << name;
				if (!t.enum_values.empty()) {
					out << " (enum-values: ";
					for (size_t i = 0; i < t.enum_values.size(); ++i) {
						if (i) out << ", ";
						out << t.enum_values[i];
					}
					out << ")";
				}
				out << "\n";
			}
		}, type);
	}
	out << "\n";

	// ── Methods ────────────────────────────────────────────
	out << "── Methods (" << methods.size() << ") ──\n";
	for (const auto& m : methods) {
		out << "  " << m.name;
		if (m.is_notification) out << " [notification]";
		if (m.deprecated) out << " [deprecated]";
		out << "\n";
		if (!m.summary.empty())
			out << "    summary: " << m.summary << "\n";
		if (!m.description.empty())
			out << "    desc:    " << m.description << "\n";
		out << "    params (" << m.param_structure << "):\n";
		for (const auto& p : m.params) {
			out << "      " << p.cpp_type << " " << p.name;
			if (p.required) out << " [required]";
			if (p.deprecated) out << " [deprecated]";
			if (!p.description.empty()) out << " // " << p.description;
			out << "\n";
		}
		if (!m.is_notification) {
			out << "    result: " << m.result_type.name << "\n";
		}
		if (!m.errors.empty()) {
			out << "    errors:\n";
			for (const auto& e : m.errors)
				out << "      " << e.code << ": " << e.message << "\n";
		}
		out << "\n";
	}

	out << "═══════════════════════════════════════════\n";
	out << "  End of debug output\n";
	out << "═══════════════════════════════════════════\n";
}

} // namespace backend::mock
