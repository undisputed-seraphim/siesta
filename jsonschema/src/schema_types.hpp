#ifndef JSONSCHEMA_SCHEMA_TYPES_HPP
#define JSONSCHEMA_SCHEMA_TYPES_HPP

#include <concepts>
#include <cstdint>
#include <functional>
#include <simdjson.h>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace jsonschema {

// Forward declarations
class SchemaNode;

// Concepts for type checking
template <typename T>
concept CallbackFunction = std::invocable<const T&, const std::string&>;

template <typename T>
concept JsonValue = requires(T t) {
	{ t.type() } -> std::same_as<simdjson::dom::element_type>;
};

// URI type for schema locations
struct SchemaURI {
	std::string base;
	std::string fragment;

	SchemaURI() = default;
	explicit SchemaURI(std::string uri)
		: SchemaURI(parse(std::move(uri))) {}

	static SchemaURI parse(std::string uri);
	bool operator==(const SchemaURI& other) const = default;
	auto operator<=>(const SchemaURI&) const = default;

	std::string to_string() const;
	std::string resolve_fragment() const;
};

// JSON Instance location
struct InstanceLocation {
	std::vector<std::string> segments;

	InstanceLocation() = default;
	explicit InstanceLocation(std::vector<std::string> segs)
		: segments(std::move(segs)) {}

	std::string to_string() const;
	InstanceLocation operator/(std::string segment) const;
	bool empty() const { return segments.empty(); }
};

// Schema keyword enumeration covering draft 2020-12
enum class Keyword : uint8_t {
	Unknown,
	// Core validation keywords
	Type,
	Enum,
	Const,
	MultipleOf,
	Maximum,
	Minimum,
	ExclusiveMaximum,
	ExclusiveMinimum,
	MaxLength,
	MinLength,
	Pattern,
	MaxItems,
	MinItems,
	UniqueItems,
	MaxContains,
	MinContains,
	MaxProperties,
	MinProperties,
	Required,
	DependentRequired,
	// Schema composition
	AllOf,
	AnyOf,
	OneOf,
	Not,
	// Structural keywords
	Properties,
	PatternProperties,
	AdditionalProperties,
	PropertyNames,
	Items,
	PrefixItems,
	AdditionalItems,
	UnevaluatedItems,
	UnevaluatedProperties,
	Contains,
	// Metadata
	Title,
	Description,
	Default,
	Deprecated,
	ReadOnly,
	WriteOnly,
	// Content keywords
	ContentEncoding,
	ContentMediaType,
	ContentSchema,
	// Anchor/Reference keywords
	Schema,
	ID,
	Anchor,
	Ref,
	DynamicRef,
	DynamicAnchor,
	Defs,
	Definitions,
	// Conditional keywords
	If,
	Then,
	Else,
	DependentSchemas,
};

// Schema node representing a position in the schema tree
struct SchemaNode {
	SchemaURI uri;
	std::string id;
	std::string anchor;
	std::string dynamic_anchor;
	Keyword keyword = Keyword::Unknown;
	simdjson::dom::element value{};
	std::vector<SchemaNode> children;
	const SchemaNode* parent = nullptr;

	bool has_id() const noexcept { return !id.empty(); }
	bool has_anchor() const noexcept { return !anchor.empty(); }
	bool has_dynamic_anchor() const noexcept { return !dynamic_anchor.empty(); }

	const simdjson::dom::element& get_value() const { return value; }
};

// Validation error information
struct ValidationError {
	InstanceLocation instance_location;
	std::string schema_uri;
	std::string message;

	ValidationError() = default;
	ValidationError(InstanceLocation iloc, std::string suri, std::string msg)
		: instance_location(std::move(iloc))
		, schema_uri(std::move(suri))
		, message(std::move(msg)) {}
};

// Evaluation context for tracking evaluated items/properties
struct EvaluationContext {
	std::vector<bool> evaluated_items;
	std::unordered_set<std::string> evaluated_properties;
	size_t min_contains_count = 0;
	size_t max_contains_count = 0;
};

// Schema compiler with validation capabilities
class SchemaCompiler {
public:
	using Callback = std::function<void(const SchemaNode&, const std::string&)>;

	explicit SchemaCompiler(Callback callback = nullptr);

	[[nodiscard]] bool compile(std::string_view schema_json);
	[[nodiscard]] bool validate(std::string_view instance_json);

	const std::vector<ValidationError>& errors() const noexcept { return errors_; }
	const SchemaNode& root() const noexcept { return root_node_; }

private:
	// Schema tree building
	void build_schema_tree(simdjson::dom::element elem, SchemaNode& node, SchemaURI uri, std::string path);
	void collect_anchors(const SchemaNode& node);
	SchemaNode* find_ref(const std::string& ref_uri);

	// Validation
	void validate_node(
		const SchemaNode& node,
		simdjson::dom::element instance,
		std::string schema_location,
		std::string instance_location,
		EvaluationContext& ctx);

	// Helper functions
	[[nodiscard]] static bool check_type_match(simdjson::dom::element instance, std::string_view type_str);
	[[nodiscard]] static std::string type_to_string(simdjson::dom::element instance);
	[[nodiscard]] static bool json_equal(simdjson::dom::element a, simdjson::dom::element b);
	void validate_pattern(
		simdjson::dom::element instance,
		std::string_view pattern,
		const std::string& schema_loc,
		const std::string& inst_loc);

	Callback callback_;
	SchemaNode root_node_;
	std::unordered_map<std::string, SchemaNode*> anchors_;
	std::unordered_map<std::string, SchemaNode*> ids_;
	std::vector<ValidationError> errors_;
	simdjson::dom::parser parser_;
	simdjson::dom::document schema_doc_;
};

} // namespace jsonschema

#endif // JSONSCHEMA_SCHEMA_TYPES_HPP
