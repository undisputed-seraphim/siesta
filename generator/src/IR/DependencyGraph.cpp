// SPDX-License-Identifier: Apache-2.0
#include "IR/DependencyGraph.hpp"
#include <queue>

namespace analysis {

bool DependencyGraph::detectCycles(std::vector<std::vector<std::string>>& out_cycles) const {
	out_cycles.clear();

	std::unordered_set<std::string> visited;
	std::unordered_set<std::string> rec_stack;
	std::vector<std::string> path;

	for (const auto& [node, _] : adjacency_) {
		if (visited.find(node) == visited.end()) {
			dfsDetectCycle(node, visited, rec_stack, path, out_cycles);
		}
	}

	return !out_cycles.empty();
}

std::vector<std::string> DependencyGraph::topologicalSort() const {
	std::vector<std::string> result;

	std::unordered_map<std::string, std::vector<std::string>> reverse_adj;
	for (const auto& [from, neighbors] : adjacency_) {
		for (const auto& to : neighbors) {
			reverse_adj[to].push_back(from);
		}
	}

	std::unordered_map<std::string, int> in_degree;
	for (const auto& [node, _] : adjacency_) {
		in_degree[node] = 0;
	}
	for (const auto& [from, neighbors] : adjacency_) {
		in_degree[from] += neighbors.size();
	}

	std::queue<std::string> queue;
	for (const auto& [node, degree] : in_degree) {
		if (degree == 0) {
			queue.push(node);
		}
	}

	LOG_SORT(
		"topologicalSort: %zu nodes in adjacency_, %zu with in_degree=0 (ready to process)",
		adjacency_.size(),
		queue.size());

	while (!queue.empty()) {
		std::string current = queue.front();
		queue.pop();
		result.push_back(current);

		for (const auto& dependent : reverse_adj[current]) {
			in_degree[dependent]--;
			if (in_degree[dependent] == 0) {
				queue.push(dependent);
			}
		}
	}

	if (result.size() != adjacency_.size()) {
		LOG_SORT("topologicalSort: CYCLE DETECTED — processed %zu of %zu nodes", result.size(), adjacency_.size());
		return {};
	}

	LOG_SORT("topologicalSort: SUCCESS — %zu types ordered", result.size());
	return result;
}

DependencyGraph DependencyGraph::buildFromAST(const schema::NormalizedAST& ast) {
	DependencyGraph graph;

	LOG_DEP("buildFromAST: starting with %zu types from AST", ast.getTypes().size());

	for (const auto& [name, type] : ast.getTypes()) {
		buildDependencies(name, type, graph);
	}

	LOG_DEP(
		"buildFromAST: done — %zu dependency edges collected, %zu unique nodes in adjacency map",
		graph.dependencies_.size(),
		graph.adjacency_.size());
	return graph;
}

void DependencyGraph::buildDependencies(const std::string& name, const schema::SchemaType& type, DependencyGraph& graph) {
	std::visit(
		[&](const auto& t) {
			using T = std::decay_t<decltype(t)>;

			if constexpr (std::is_same_v<T, schema::StructType>) {
				for (const auto& field : t.fields) {
					if (!codegen::isSyntheticCppType(field.type.name)) {
						graph.addDependency({name}, field.type, DepKind::Value);
					} else if (field.type.name.rfind("std::vector<", 0) == 0) {
						std::string elem_type = field.type.name.substr(12);
						elem_type.pop_back();
						if (!codegen::isSyntheticCppType(elem_type)) {
							graph.addDependency({name}, {elem_type, false}, DepKind::ArrayElem);
						}
					} else if (field.type.name.rfind("std::map<std::string, ", 0) == 0) {
						std::string value_type = field.type.name.substr(22);
						value_type.pop_back();
						if (!codegen::isSyntheticCppType(value_type)) {
							graph.addDependency({name}, {value_type, false}, DepKind::MapValue);
						}
					}
				}
				for (const auto& base : t.allOf_bases) {
					if (!codegen::isSyntheticCppType(base.name)) {
						graph.addDependency({name}, base, DepKind::Base);
					}
				}
			} else if constexpr (std::is_same_v<T, schema::VariantType>) {
				for (const auto& alt : t.alternatives) {
					if (codegen::isSyntheticCppType(alt.name)) {
						std::string dep_type;
						if (alt.name.rfind("std::vector<", 0) == 0) {
							dep_type = alt.name.substr(12);
							dep_type.pop_back();
						} else if (alt.name.rfind("std::map<std::string, ", 0) == 0) {
							dep_type = alt.name.substr(22);
							dep_type.pop_back();
						}
						if (!dep_type.empty() && !codegen::isSyntheticCppType(dep_type)) {
							graph.addDependency({name}, {dep_type, false}, DepKind::Variant);
						}
					} else {
						graph.addDependency({name}, alt, DepKind::Variant);
					}
				}
			} else if constexpr (std::is_same_v<T, schema::ArrayType>) {
				if (!codegen::isSyntheticCppType(t.element_type.name)) {
					graph.addDependency({name}, t.element_type, DepKind::ArrayElem);
				}
			} else if constexpr (std::is_same_v<T, schema::MapType>) {
				if (!codegen::isSyntheticCppType(t.value_type.name)) {
					graph.addDependency({name}, t.value_type, DepKind::MapValue);
				}
			}
		},
		type);
}

void DependencyGraph::dfsDetectCycle(
	const std::string& node,
	std::unordered_set<std::string>& visited,
	std::unordered_set<std::string>& rec_stack,
	std::vector<std::string>& path,
	std::vector<std::vector<std::string>>& out_cycles) const {
	visited.insert(node);
	rec_stack.insert(node);
	path.push_back(node);

	auto it = adjacency_.find(node);
	if (it != adjacency_.end()) {
		for (const auto& neighbor : it->second) {
			if (visited.find(neighbor) == visited.end()) {
				dfsDetectCycle(neighbor, visited, rec_stack, path, out_cycles);
			} else if (rec_stack.find(neighbor) != rec_stack.end()) {
				std::vector<std::string> cycle;
				bool in_cycle = false;
				for (const auto& n : path) {
					if (n == neighbor)
						in_cycle = true;
					if (in_cycle)
						cycle.push_back(n);
				}
				cycle.push_back(neighbor);
				out_cycles.push_back(cycle);
			}
		}
	}

	path.pop_back();
	rec_stack.erase(node);
}

TopologicalOrder sortTypes(const schema::NormalizedAST& ast) {
	TopologicalOrder result;

	LOG_SORT("sortTypes: AST has %zu types", ast.getTypes().size());

	auto graph = DependencyGraph::buildFromAST(ast);

	if (graph.detectCycles(result.cycles)) {
		result.has_cycles = true;
		return result;
	}

	result.ordered_types = graph.topologicalSort();

	std::unordered_set<std::string> ast_names;
	for (const auto& [name, _] : ast.getTypes()) {
		ast_names.insert(name);
	}
	std::vector<std::string> filtered;
	for (const auto& name : result.ordered_types) {
		if (ast_names.count(name)) {
			filtered.push_back(name);
		}
	}
	result.ordered_types = std::move(filtered);

	if (!result.has_cycles && result.ordered_types.empty()) {
		for (const auto& [name, _] : ast.getTypes()) {
			result.ordered_types.push_back(name);
		}
	} else if (!result.has_cycles) {
		std::unordered_set<std::string> sorted_set(result.ordered_types.begin(), result.ordered_types.end());
		std::vector<std::string> missing;
		for (const auto& [name, _] : ast.getTypes()) {
			if (sorted_set.find(name) == sorted_set.end()) {
				missing.push_back(name);
			}
		}
		if (!missing.empty()) {
			for (const auto& m : missing) {
				result.ordered_types.push_back(m);
			}
		}
	}

	return result;
}

} // namespace analysis
