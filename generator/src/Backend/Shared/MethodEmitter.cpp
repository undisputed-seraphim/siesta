// SPDX-License-Identifier: Apache-2.0
#include "Backend/Shared/MethodEmitter.hpp"
#include <string>

namespace codegen {

void emitMethodSignature(std::ostream& out, const Endpoint& ep,
                         std::string_view completion_token_type) {
	out << "\tauto " << ep.function_name << "(";

	bool has_previous = false;

	if (ep.has_request_body) {
		out << "const " << ep.body_type << "& body";
		has_previous = true;
	}

	for (size_t i = 0; i < ep.params.size(); ++i) {
		if (has_previous) {
			out << ", ";
		}
		const auto& p = ep.params[i];
		if (!p.required) {
			out << "std::optional<" << p.cpp_type << "> ";
		} else {
			out << p.cpp_type << " ";
		}
		out << p.name;
		has_previous = true;
	}

	if (has_previous) {
		out << ", ";
	}
	out << "::boost::asio::completion_token_for<void(" << completion_token_type
	    << ")> auto&& token";
	out << ")";
}

void emitPathParams(std::ostream& out, const std::vector<const ClientParam*>& path_params) {
	for (const auto* p : path_params) {
		bool is_string = p->is_string_type;
		if (p->required) {
			if (is_string) {
				out << "\t\tif (auto _p = target_path.find(\"{}\"); _p != std::string::npos) target_path.replace(_p, 2, "
					<< p->name << ");\n";
			} else {
				out << "\t\tif (auto _p = target_path.find(\"{}\"); _p != std::string::npos) target_path.replace(_p, 2, std::to_string("
					<< p->name << "));\n";
			}
		} else {
			out << "\t\tif (" << p->name << ".has_value()) {\n";
			if (is_string) {
				out << "\t\t\tif (auto _p = target_path.find(\"{}\"); _p != std::string::npos) target_path.replace(_p, 2, *"
					<< p->name << ");\n";
			} else {
				out << "\t\t\tif (auto _p = target_path.find(\"{}\"); _p != std::string::npos) target_path.replace(_p, 2, std::to_string(*"
					<< p->name << "));\n";
			}
			out << "\t\t}\n";
		}
	}
}

void emitQueryParams(std::ostream& out, const std::vector<const ClientParam*>& params) {
	for (const auto* p : params) {
		bool is_vector = p->is_vector_type;
		bool is_string = p->is_string_type;
		bool is_vector_string = p->is_vector_string_type;

		if (p->required) {
			if (is_vector_string) {
				out << "\t\tfor (size_t _i = 0; _i < " << p->name << ".size(); ++_i) {\n";
				out << "\t\t\t_sep(); query += \"" << p->wire_name << "=\" + url_encode(" << p->name << "[_i]);\n";
				out << "\t\t}\n";
			} else if (is_vector) {
				out << "\t\tfor (size_t _i = 0; _i < " << p->name << ".size(); ++_i) {\n";
				out << "\t\t\t_sep(); query += \"" << p->wire_name << "=\" + query_value(" << p->name << "[_i]);\n";
				out << "\t\t}\n";
			} else if (is_string) {
				out << "\t\t_sep(); query += \"" << p->wire_name << "=\" + url_encode(" << p->name << ");\n";
			} else {
				out << "\t\t_sep(); query += \"" << p->wire_name << "=\" + query_value(" << p->name << ");\n";
			}
		} else {
			out << "\t\tif (" << p->name << ".has_value()) {\n";
			if (is_vector_string) {
				out << "\t\t\tfor (size_t _i = 0; _i < " << p->name << "->size(); ++_i) {\n";
				out << "\t\t\t\t_sep(); query += \"" << p->wire_name << "=\" + url_encode((*" << p->name << ")[_i]);\n";
				out << "\t\t\t}\n";
			} else if (is_vector) {
				out << "\t\t\tfor (size_t _i = 0; _i < " << p->name << "->size(); ++_i) {\n";
				out << "\t\t\t\t_sep(); query += \"" << p->wire_name << "=\" + query_value((*" << p->name << ")[_i]);\n";
				out << "\t\t\t}\n";
			} else if (is_string) {
				out << "\t\t\t_sep(); query += \"" << p->wire_name << "=\" + url_encode(*" << p->name << ");\n";
			} else {
				out << "\t\t\t_sep(); query += \"" << p->wire_name << "=\" + query_value(*" << p->name << ");\n";
			}
			out << "\t\t}\n";
		}
	}
}

} // namespace codegen
