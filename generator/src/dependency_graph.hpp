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
	bool detectCycles(std::vector<std::vector<std::string>>& out_cycles) const;

	/**
	 * Topological sort using Kahn's algorithm
	 * Returns ordered list of type names, or empty vector if cycles exist
	 */
	std::vector<std::string> topologicalSort() const;

	/**
	 * Build dependency graph from normalized AST
	 */
	static DependencyGraph buildFromAST(const schema::NormalizedAST& ast);

private:
	static void buildDependencies(const std::string& name, const schema::SchemaType& type, DependencyGraph& graph);

	void dfsDetectCycle(
		const std::string& node,
		std::unordered_set<std::string>& visited,
		std::unordered_set<std::string>& rec_stack,
		std::vector<std::string>& path,
		std::vector<std::vector<std::string>>& out_cycles) const;

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
TopologicalOrder sortTypes(const schema::NormalizedAST& ast);

} // namespace analysis
