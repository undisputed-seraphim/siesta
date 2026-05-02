#pragma once

#include "schema_ast.hpp"
#include "util.hpp"
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace analysis {

/**
 * Dependency kinds - different types of type relationships
 */
enum class DepKind {
	Value,	   // Field contains this type by value
	Variant,   // Type is a variant alternative
	Base,	   // Type is a base class (allOf)
	ArrayElem, // Type is array element
	MapValue   // Type is map value
};

/**
 * A single dependency edge: 'from' depends on 'to'
 */
struct Dependency {
	schema::TypeRef from;
	schema::TypeRef to;
	DepKind kind;

	bool operator==(const Dependency& other) const = default;
};

/**
 * Dependency graph - tracks all type dependencies for topological sorting
 */
class DependencyGraph {
public:
	/**
	 * Add a dependency edge
	 */
	void addDependency(const schema::TypeRef& from, const schema::TypeRef& to, DepKind kind) {
		dependencies_.push_back({from, to, kind});
		adjacency_[from.name].push_back(to.name);
		// Ensure target is also in the map (even if it has no outgoing edges)
		if (adjacency_.find(to.name) == adjacency_.end()) {
			adjacency_[to.name] = {};
		}
	}

	/**
	 * Get all dependencies
	 */
	const std::vector<Dependency>& getDependencies() const { return dependencies_; }

	/**
	 * Detect cycles in the dependency graph using DFS
	 * Returns true if cycles exist, populates out_cycles with cycle paths
	 */
	bool detectCycles(std::vector<std::vector<std::string>>& out_cycles) const {
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

	/**
	 * Topological sort using Kahn's algorithm
	 * Returns ordered list of type names, or empty vector if cycles exist
	 */
	std::vector<std::string> topologicalSort() const {
		std::vector<std::string> result;

		// Build reverse adjacency: if A depends on B, then B->A in reverse (B must come before A)
		std::unordered_map<std::string, std::vector<std::string>> reverse_adj;
		for (const auto& [from, neighbors] : adjacency_) {
			for (const auto& to : neighbors) {
				reverse_adj[to].push_back(from);
			}
		}

		// Calculate in-degrees (number of dependencies each node has)
		std::unordered_map<std::string, int> in_degree;
		for (const auto& [node, _] : adjacency_) {
			in_degree[node] = 0;
		}
		for (const auto& [from, neighbors] : adjacency_) {
			in_degree[from] += neighbors.size();
		}

		// Queue of nodes with no dependencies
		std::queue<std::string> queue;
		for (const auto& [node, degree] : in_degree) {
			if (degree == 0) {
				queue.push(node);
			}
		}

		// Process nodes in order
		while (!queue.empty()) {
			std::string current = queue.front();
			queue.pop();
			result.push_back(current);

			// For each node that depends on current, decrease its in-degree
			for (const auto& dependent : reverse_adj[current]) {
				in_degree[dependent]--;
				if (in_degree[dependent] == 0) {
					queue.push(dependent);
				}
			}
		}

		// Check for cycles: if not all nodes processed, there's a cycle
		if (result.size() != adjacency_.size()) {
			return {}; // Cycle detected
		}

		return result;
	}

	/**
	 * Build dependency graph from normalized AST
	 */
	static DependencyGraph buildFromAST(const schema::NormalizedAST& ast) {
		DependencyGraph graph;

		for (const auto& [name, type] : ast.getTypes()) {
			buildDependencies(name, type, graph);
		}

		return graph;
	}

private:
	static void buildDependencies(const std::string& name, const schema::SchemaType& type, DependencyGraph& graph) {
		std::visit(
			[&](const auto& t) {
				using T = std::decay_t<decltype(t)>;

				if constexpr (std::is_same_v<T, schema::StructType>) {
					// Fields depend on their types (including inline structs)
					for (const auto& field : t.fields) {
						// Skip synthetic C++ types - they're not real AST nodes
						if (!isSyntheticCppType(field.type.name)) {
							graph.addDependency({name}, field.type, DepKind::Value);
						} else if (field.type.name.rfind("std::vector<", 0) == 0) {
							// For inline arrays like std::vector<T>, depend on T
							// Extract element type from "std::vector<T>"
							std::string elem_type = field.type.name.substr(12); // skip "std::vector<"
							elem_type.pop_back();								// remove ">"
							if (!isSyntheticCppType(elem_type)) {
								graph.addDependency({name}, {elem_type, false}, DepKind::ArrayElem);
							}
						} else if (field.type.name.rfind("std::map<std::string, ", 0) == 0) {
							// For inline maps like std::map<std::string, T>, depend on T
							std::string value_type = field.type.name.substr(22); // skip "std::map<std::string, "
							value_type.pop_back();								 // remove ">"
							if (!isSyntheticCppType(value_type)) {
								graph.addDependency({name}, {value_type, false}, DepKind::MapValue);
							}
						}
						// Primitives like std::string, int64_t have no dependencies
					}
					// Base classes are dependencies
					for (const auto& base : t.allOf_bases) {
						if (!isSyntheticCppType(base.name)) {
							graph.addDependency({name}, base, DepKind::Base);
						}
					}
				} else if constexpr (std::is_same_v<T, schema::VariantType>) {
					// All alternatives must be defined before the variant
					for (const auto& alt : t.alternatives) {
						if (isSyntheticCppType(alt.name)) {
							// Extract real dependencies from synthetic C++ types
							std::string dep_type;
							if (alt.name.rfind("std::vector<", 0) == 0) {
								// std::vector<T> - depend on T
								dep_type = alt.name.substr(12);
								dep_type.pop_back(); // remove ">"
							} else if (alt.name.rfind("std::map<std::string, ", 0) == 0) {
								// std::map<std::string, T> - depend on T
								dep_type = alt.name.substr(22);
								dep_type.pop_back(); // remove ">"
							}
							if (!dep_type.empty() && !isSyntheticCppType(dep_type)) {
								graph.addDependency({name}, {dep_type, false}, DepKind::Variant);
							}
						} else {
							// Real named type (including inline structs from oneOf/anyOf) - add dependency
							// The is_inline flag just means it was created inline, not that it's synthetic
							graph.addDependency({name}, alt, DepKind::Variant);
						}
					}
				} else if constexpr (std::is_same_v<T, schema::ArrayType>) {
					// For ArrayType schemas, depend on element type (not std::vector<...>)
					if (!isSyntheticCppType(t.element_type.name)) {
						graph.addDependency({name}, t.element_type, DepKind::ArrayElem);
					}
				} else if constexpr (std::is_same_v<T, schema::MapType>) {
					// For MapType schemas, depend on value type (not std::map<...>)
					if (!isSyntheticCppType(t.value_type.name)) {
						graph.addDependency({name}, t.value_type, DepKind::MapValue);
					}
				}
				// Primitives and enums have no dependencies
			},
			type);
	}

	void dfsDetectCycle(
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
					// Found a cycle! Extract it
					std::vector<std::string> cycle;
					bool in_cycle = false;
					for (const auto& n : path) {
						if (n == neighbor)
							in_cycle = true;
						if (in_cycle)
							cycle.push_back(n);
					}
					cycle.push_back(neighbor); // Complete the cycle
					out_cycles.push_back(cycle);
				}
			}
		}

		path.pop_back();
		rec_stack.erase(node);
	}

	std::vector<Dependency> dependencies_;
	std::unordered_map<std::string, std::vector<std::string>> adjacency_; // node -> types it depends on
};

/**
 * Result of topological sort - ordered list with validation
 */
struct TopologicalOrder {
	std::vector<std::string> ordered_types;
	bool has_cycles = false;
	std::vector<std::vector<std::string>> cycles;

	bool isValid() const { return !has_cycles && !ordered_types.empty(); }
};

/**
 * Perform topological sort on AST with cycle detection
 */
inline TopologicalOrder sortTypes(const schema::NormalizedAST& ast) {
	TopologicalOrder result;

	auto graph = DependencyGraph::buildFromAST(ast);

	// Check for cycles
	if (graph.detectCycles(result.cycles)) {
		result.has_cycles = true;
		return result;
	}

	// Perform topological sort
	result.ordered_types = graph.topologicalSort();

	if (!result.has_cycles && result.ordered_types.empty()) {
		// Fallback: if no types were sorted (shouldn't happen normally),
		// add all AST types in arbitrary order
		for (const auto& [name, _] : ast.getTypes()) {
			result.ordered_types.push_back(name);
		}
	}

	return result;
}

} // namespace analysis
