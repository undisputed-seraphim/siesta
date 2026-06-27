// SPDX-License-Identifier: Apache-2.0
#pragma once
/// Transport-agnostic dispatch utilities.
/// Used by all backend server emitters for endpoint classification
/// and path matching.

#include "IR/EndpointIR.hpp"
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace codegen {

struct DispatchSets {
	std::vector<const Endpoint*> static_eps;
	std::vector<const Endpoint*> param_eps;
	std::vector<const Endpoint*> ws_eps;
};

inline void collectDispatchSets(const std::vector<Endpoint>& endpoints, DispatchSets& ds) {
	for (const auto& ep : endpoints) {
		if (ep.is_websocket) {
			ds.ws_eps.push_back(&ep);
		} else if (ep.path_template.find("{}") != std::string::npos) {
			ds.param_eps.push_back(&ep);
		} else {
			ds.static_eps.push_back(&ep);
		}
	}
}

inline void emitMatchPath(std::ostream& out) {
	out << "bool match_path(std::string_view pattern, std::string_view target) {\n";
	out << "\twhile (!pattern.empty() && !target.empty()) {\n";
	out << "\t\tif (pattern.front() == '/' && target.front() == '/') {\n";
	out << "\t\t\tpattern.remove_prefix(1);\n";
	out << "\t\t\ttarget.remove_prefix(1);\n";
	out << "\t\t\tcontinue;\n";
	out << "\t\t}\n";
	out << "\t\tauto pp = pattern.find('/');\n";
	out << "\t\tauto tp = target.find('/');\n";
	out << "\t\tauto ps = pattern.substr(0, pp);\n";
	out << "\t\tauto ts = target.substr(0, tp);\n";
	out << "\t\tif (ps != \"{}\"sv && ps != ts) return false;\n";
	out << "\t\tpattern = pp == std::string_view::npos ? \"\"sv : pattern.substr(pp);\n";
	out << "\t\ttarget  = tp == std::string_view::npos ? \"\"sv : target.substr(tp);\n";
	out << "\t}\n";
	out << "\treturn pattern == target;\n";
	out << "}\n\n";
}

} // namespace codegen
