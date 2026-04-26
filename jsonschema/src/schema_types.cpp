#include "schema_types.hpp"
#include <cmath>
#include <regex>
#include <sstream>

namespace jsonschema {

// SchemaURI implementation
SchemaURI SchemaURI::parse(std::string uri) {
	SchemaURI result;
	auto fragment_pos = uri.find('#');
	if (fragment_pos != std::string::npos) {
		result.base = uri.substr(0, fragment_pos);
		result.fragment = uri.substr(fragment_pos);
	} else {
		result.base = std::move(uri);
	}
	return result;
}

std::string SchemaURI::to_string() const {
	std::string result = base;
	if (!fragment.empty()) {
		result += fragment;
	}
	return result;
}

std::string SchemaURI::resolve_fragment() const {
	if (fragment.empty()) {
		return base.empty() ? "#" : base;
	}
	return fragment;
}

// InstanceLocation implementation
std::string InstanceLocation::to_string() const {
	if (segments.empty()) {
		return "";
	}
	std::ostringstream oss;
	for (const auto& seg : segments) {
		oss << '/' << seg;
	}
	return oss.str();
}

InstanceLocation InstanceLocation::operator/(std::string segment) const {
	InstanceLocation result(*this);
	result.segments.push_back(std::move(segment));
	return result;
}

// Helper: check if JSON values are equal
bool SchemaCompiler::json_equal(simdjson::dom::element a, simdjson::dom::element b) {
	if (a.type() != b.type()) {
		return false;
	}

	switch (a.type()) {
	case simdjson::dom::element_type::ARRAY: {
		auto arr_a = a.get_array();
		auto arr_b = b.get_array();
		auto iter_a = arr_a.begin();
		auto iter_b = arr_b.begin();
		while (iter_a != arr_a.end() && iter_b != arr_b.end()) {
			if (!json_equal(*iter_a++, *iter_b++)) {
				return false;
			}
		}
		return iter_a == arr_a.end() && iter_b == arr_b.end();
	}
	case simdjson::dom::element_type::OBJECT: {
		auto obj_a = a.get_object();
		auto obj_b = b.get_object();
		std::unordered_map<std::string, simdjson::dom::element> map_b;
		for (const auto& field : obj_b) {
			map_b.emplace(std::string(field.key), field.value);
		}
		for (const auto& field : obj_a) {
			auto it = map_b.find(std::string(field.key));
			if (it == map_b.end() || !json_equal(field.value, it->second)) {
				return false;
			}
		}
		return true;
	}
	case simdjson::dom::element_type::STRING: {
		std::string_view va, vb;
		simdjson::simdjson_result<std::string_view> ra = a.get(va);
		simdjson::simdjson_result<std::string_view> rb = b.get(vb);
		return va == vb;
	}
	case simdjson::dom::element_type::BOOL: {
		bool ba = a.get_bool();
		bool bb = b.get_bool();
		return ba == bb;
	}
	case simdjson::dom::element_type::NULL_VALUE:
		return true;
	case simdjson::dom::element_type::INT64:
	case simdjson::dom::element_type::UINT64:
	case simdjson::dom::element_type::DOUBLE: {
		double da = 0, db = 0;
		simdjson::simdjson_result<double> ra = a.get(da);
		simdjson::simdjson_result<double> rb = b.get(db);
		return std::abs(da - db) < 1e-10;
	}
	default:
		return false;
	}
}

// Type conversion helper
std::string SchemaCompiler::type_to_string(simdjson::dom::element instance) {
	switch (instance.type()) {
	case simdjson::dom::element_type::ARRAY:
		return "array";
	case simdjson::dom::element_type::OBJECT:
		return "object";
	case simdjson::dom::element_type::INT64:
	case simdjson::dom::element_type::UINT64:
		return "integer";
	case simdjson::dom::element_type::DOUBLE:
		return "number";
	case simdjson::dom::element_type::STRING:
		return "string";
	case simdjson::dom::element_type::BOOL:
		return "boolean";
	case simdjson::dom::element_type::NULL_VALUE:
		return "null";
	default:
		return "unknown";
	}
}

// Type matching helper
bool SchemaCompiler::check_type_match(simdjson::dom::element instance, std::string_view type_str) {
	switch (instance.type()) {
	case simdjson::dom::element_type::ARRAY:
		return type_str == "array";
	case simdjson::dom::element_type::OBJECT:
		return type_str == "object";
	case simdjson::dom::element_type::STRING:
		return type_str == "string";
	case simdjson::dom::element_type::BOOL:
		return type_str == "boolean";
	case simdjson::dom::element_type::NULL_VALUE:
		return type_str == "null";
	case simdjson::dom::element_type::INT64:
	case simdjson::dom::element_type::UINT64:
		if (type_str == "integer" || type_str == "number") {
			return true;
		}
		return false;
	case simdjson::dom::element_type::DOUBLE: {
		double d = 0;
		simdjson::simdjson_result<double> r = instance.get(d);
		if (type_str == "number") {
			return true;
		}
		if (type_str == "integer") {
			return std::floor(d) == d;
		}
		return false;
	}
	default:
		return false;
	}
}

// Constructor
SchemaCompiler::SchemaCompiler(Callback callback)
	: callback_(std::move(callback)) {}

// Compile schema from JSON string
bool SchemaCompiler::compile(std::string_view schema_json) {
	errors_.clear();
	anchors_.clear();
	ids_.clear();

	auto root = parser_.parse_into_document(schema_doc_, std::string(schema_json));
	if (root.error()) {
		return false;
	}

	root_node_ = SchemaNode{};
	root_node_.uri = SchemaURI::parse("#");
	root_node_.keyword = Keyword::Schema;
	root_node_.value = root.value();

	build_schema_tree(root.value(), root_node_, SchemaURI::parse("#"), "");
	collect_anchors(root_node_);

	return true;
}

// Build schema tree recursively
void SchemaCompiler::build_schema_tree(simdjson::dom::element elem, SchemaNode& node, SchemaURI uri, std::string path) {
	if (elem.type() != simdjson::dom::element_type::OBJECT) {
		return;
	}

	for (auto field : elem.get_object()) {
		std::string key = std::string(field.key);
		auto value = field.value;
		Keyword kw = Keyword::Unknown;

		// Map keyword string to enum
		if (key == "$schema")
			kw = Keyword::Schema;
		else if (key == "$id")
			kw = Keyword::ID;
		else if (key == "$anchor")
			kw = Keyword::Anchor;
		else if (key == "$ref")
			kw = Keyword::Ref;
		else if (key == "$dynamicRef")
			kw = Keyword::DynamicRef;
		else if (key == "$dynamicAnchor")
			kw = Keyword::DynamicAnchor;
		else if (key == "$defs")
			kw = Keyword::Defs;
		else if (key == "definitions")
			kw = Keyword::Definitions;
		else if (key == "type")
			kw = Keyword::Type;
		else if (key == "enum")
			kw = Keyword::Enum;
		else if (key == "const")
			kw = Keyword::Const;
		else if (key == "multipleOf")
			kw = Keyword::MultipleOf;
		else if (key == "maximum")
			kw = Keyword::Maximum;
		else if (key == "minimum")
			kw = Keyword::Minimum;
		else if (key == "exclusiveMaximum")
			kw = Keyword::ExclusiveMaximum;
		else if (key == "exclusiveMinimum")
			kw = Keyword::ExclusiveMinimum;
		else if (key == "maxLength")
			kw = Keyword::MaxLength;
		else if (key == "minLength")
			kw = Keyword::MinLength;
		else if (key == "pattern")
			kw = Keyword::Pattern;
		else if (key == "maxItems")
			kw = Keyword::MaxItems;
		else if (key == "minItems")
			kw = Keyword::MinItems;
		else if (key == "uniqueItems")
			kw = Keyword::UniqueItems;
		else if (key == "maxContains")
			kw = Keyword::MaxContains;
		else if (key == "minContains")
			kw = Keyword::MinContains;
		else if (key == "maxProperties")
			kw = Keyword::MaxProperties;
		else if (key == "minProperties")
			kw = Keyword::MinProperties;
		else if (key == "required")
			kw = Keyword::Required;
		else if (key == "dependentRequired")
			kw = Keyword::DependentRequired;
		else if (key == "properties")
			kw = Keyword::Properties;
		else if (key == "patternProperties")
			kw = Keyword::PatternProperties;
		else if (key == "additionalProperties")
			kw = Keyword::AdditionalProperties;
		else if (key == "unevaluatedProperties")
			kw = Keyword::UnevaluatedProperties;
		else if (key == "propertyNames")
			kw = Keyword::PropertyNames;
		else if (key == "items")
			kw = Keyword::Items;
		else if (key == "prefixItems")
			kw = Keyword::PrefixItems;
		else if (key == "additionalItems")
			kw = Keyword::AdditionalItems;
		else if (key == "unevaluatedItems")
			kw = Keyword::UnevaluatedItems;
		else if (key == "contains")
			kw = Keyword::Contains;
		else if (key == "allOf")
			kw = Keyword::AllOf;
		else if (key == "anyOf")
			kw = Keyword::AnyOf;
		else if (key == "oneOf")
			kw = Keyword::OneOf;
		else if (key == "not")
			kw = Keyword::Not;
		else if (key == "if")
			kw = Keyword::If;
		else if (key == "then")
			kw = Keyword::Then;
		else if (key == "else")
			kw = Keyword::Else;
		else if (key == "dependentSchemas")
			kw = Keyword::DependentSchemas;
		else if (key == "title")
			kw = Keyword::Title;
		else if (key == "description")
			kw = Keyword::Description;
		else if (key == "default")
			kw = Keyword::Default;
		else if (key == "deprecated")
			kw = Keyword::Deprecated;
		else if (key == "readOnly")
			kw = Keyword::ReadOnly;
		else if (key == "writeOnly")
			kw = Keyword::WriteOnly;
		else if (key == "contentEncoding")
			kw = Keyword::ContentEncoding;
		else if (key == "contentMediaType")
			kw = Keyword::ContentMediaType;
		else if (key == "contentSchema")
			kw = Keyword::ContentSchema;

		if (kw == Keyword::Unknown) {
			continue;
		}

		SchemaNode child;
		child.uri = SchemaURI::parse(uri.to_string() + "/" + key);
		child.keyword = kw;
		child.value = value;
		child.parent = &node;

		// Handle $id and set base URI for children
		if (kw == Keyword::ID) {
			std::string_view id_value;
			if (value.get(id_value) == simdjson::SUCCESS) {
				child.id = std::string(id_value);
				ids_.emplace(child.id, &child);
				uri = SchemaURI::parse(std::string(id_value));
				// Update root node's URI base if this is at the root level
				if (node.parent == nullptr) {
					root_node_.uri.base = std::string(id_value);
				}
			}
		}

		// Handle $anchor
		if (kw == Keyword::Anchor) {
			std::string_view anchor_value;
			if (value.get(anchor_value) == simdjson::SUCCESS) {
				child.anchor = "#" + std::string(anchor_value);
				child.uri.fragment = "#" + std::string(anchor_value);
			}
		}

		// Handle $dynamicAnchor
		if (kw == Keyword::DynamicAnchor) {
			std::string_view anchor_value;
			if (value.get(anchor_value) == simdjson::SUCCESS) {
				child.dynamic_anchor = "#" + std::string(anchor_value);
			}
		}

		// Recursively build children for schema keywords
		switch (kw) {
		case Keyword::Defs:
		case Keyword::Definitions: {
			int index = 0;
			for (auto def : value.get_object()) {
				std::string def_name = std::string(def.key);
				std::string full_uri;
				if (!uri.base.empty() && uri.fragment.empty() && node.parent == nullptr) {
					full_uri = uri.base + "#/definitions/" + def_name;
				} else {
					full_uri = uri.to_string() + "/" + key + "/" + def_name;
				}
				child.uri = SchemaURI::parse(full_uri);
				build_schema_tree(def.value, child, child.uri, path + "/" + key + "/" + def_name);
				node.children.push_back(std::move(child));
				++index;
			}
			break;
		}
		case Keyword::Properties:
		case Keyword::PatternProperties:
		case Keyword::DependentRequired:
		case Keyword::DependentSchemas: {
			SchemaNode kw_node;
			kw_node.uri = SchemaURI::parse(uri.to_string() + "/" + key);
			kw_node.keyword = kw;
			kw_node.value = value;

			for (auto prop : value.get_object()) {
				std::string prop_name = std::string(prop.key);
				SchemaNode prop_node;
				prop_node.uri = SchemaURI::parse(kw_node.uri.to_string() + "/" + prop_name);
				prop_node.keyword = Keyword::Unknown;
				prop_node.value = prop.value;
				build_schema_tree(prop.value, prop_node, prop_node.uri, path + "/" + key + "/" + prop_name);
				kw_node.children.push_back(std::move(prop_node));
			}
			node.children.push_back(std::move(kw_node));
			break;
		}
		case Keyword::AllOf:
		case Keyword::AnyOf:
		case Keyword::OneOf: {
			int index = 0;
			for (auto item : value.get_array()) {
				SchemaNode item_node;
				item_node.uri = SchemaURI::parse(uri.to_string() + "/" + key + "/" + std::to_string(index));
				item_node.value = item;
				build_schema_tree(item, item_node, item_node.uri, path + "/" + key + "/" + std::to_string(index));
				node.children.push_back(std::move(item_node));
				++index;
			}
			break;
		}
		case Keyword::PrefixItems: {
			int index = 0;
			for (auto item : value.get_array()) {
				SchemaNode item_node;
				item_node.uri = SchemaURI::parse(uri.to_string() + "/" + key + "/" + std::to_string(index));
				item_node.value = item;
				build_schema_tree(item, item_node, item_node.uri, path + "/" + key + "/" + std::to_string(index));
				node.children.push_back(std::move(item_node));
				++index;
			}
			break;
		}
		case Keyword::Items:
		case Keyword::AdditionalProperties:
		case Keyword::UnevaluatedProperties:
		case Keyword::AdditionalItems:
		case Keyword::Contains:
		case Keyword::PropertyNames:
		case Keyword::Not:
		case Keyword::If:
		case Keyword::Then:
		case Keyword::Else:
		case Keyword::ContentSchema: {
			if (value.type() == simdjson::dom::element_type::OBJECT) {
				build_schema_tree(value, child, child.uri, path + "/" + key);
			} else {
				child.value = value;
			}
			node.children.push_back(std::move(child));
			break;
		}
		default: {
			child.value = value;
			if (value.type() == simdjson::dom::element_type::OBJECT) {
				build_schema_tree(value, child, child.uri, path + "/" + key);
			}
			node.children.push_back(std::move(child));
			break;
		}
		}
	}
}

// Collect anchors for $ref resolution
void SchemaCompiler::collect_anchors(const SchemaNode& node) {
	if (node.has_anchor()) {
		anchors_.emplace(node.uri.to_string(), const_cast<SchemaNode*>(&node));
	}
	if (node.has_id()) {
		ids_.emplace(node.id, const_cast<SchemaNode*>(&node));
	}
	if (node.has_dynamic_anchor()) {
		anchors_.emplace(node.dynamic_anchor, const_cast<SchemaNode*>(&node));
	}

	// Also register all nodes by their URI for $ref resolution
	anchors_.emplace(node.uri.to_string(), const_cast<SchemaNode*>(&node));

	for (const auto& child : node.children) {
		collect_anchors(child);
	}
}

// Find reference target
SchemaNode* SchemaCompiler::find_ref(const std::string& ref_uri) {
	auto it = anchors_.find(ref_uri);
	if (it != anchors_.end()) {
		return it->second;
	}

	it = ids_.find(ref_uri);
	if (it != ids_.end()) {
		return it->second;
	}

	// Try fragment-only resolution against root schema's base URI
	auto fragment_pos = ref_uri.find('#');
	if (fragment_pos != std::string::npos) {
		std::string fragment = ref_uri.substr(fragment_pos);

		it = anchors_.find(fragment);
		if (it != anchors_.end()) {
			return it->second;
		}

		if (!root_node_.uri.base.empty()) {
			std::string resolved = root_node_.uri.base + fragment;
			it = anchors_.find(resolved);
			if (it != anchors_.end()) {
				return it->second;
			}
		}
	}

	return nullptr;
}

// Validate JSON instance against schema
bool SchemaCompiler::validate(std::string_view instance_json) {
	errors_.clear();

	auto instance_doc = parser_.parse(instance_json);
	if (instance_doc.error()) {
		errors_.emplace_back(
			InstanceLocation(),
			"#",
			"Invalid JSON instance: " + std::string(simdjson::error_message(instance_doc.error())));
		return false;
	}

	EvaluationContext ctx;
	validate_node(root_node_, *instance_doc, "#", "", ctx);

	return errors_.empty();
}

// Pattern validation helper
void SchemaCompiler::validate_pattern(
	simdjson::dom::element instance,
	std::string_view pattern_sv,
	const std::string& schema_loc,
	const std::string& inst_loc) {
	std::string_view str;
	auto result = instance.get(str);
	if (result == simdjson::SUCCESS) {
		try {
			std::string pattern_str(pattern_sv);
			std::regex re(pattern_str);
			std::string inst_str(str);
			if (!std::regex_search(inst_str, re)) {
				std::string msg = "String \"" + inst_str + "\" does not match pattern /" + pattern_str + "/";
				errors_.emplace_back(InstanceLocation(), schema_loc, msg);
			}
		} catch (const std::regex_error& e) {
			if (std::string(str).find(pattern_sv) == std::string::npos) {
				std::string msg =
					"String \"" + std::string(str) + "\" does not match pattern /" + std::string(pattern_sv) + "/";
				errors_.emplace_back(InstanceLocation(), schema_loc, msg);
			}
		}
	}
}

// Check if a schema node with boolean value accepts the instance
static bool bool_schema_accepts(const SchemaNode& node) {
	if (node.value.type() == simdjson::dom::element_type::BOOL) {
		bool accepts;
		return node.value.get(accepts) == simdjson::SUCCESS && accepts;
	}
	return true; // Non-boolean schemas always "accept" at this level
}

// Recursive validation with evaluation tracking
void SchemaCompiler::validate_node(
	const SchemaNode& node,
	simdjson::dom::element instance,
	std::string schema_location,
	std::string instance_location,
	EvaluationContext& ctx) {
	if (callback_) {
		callback_(node, instance_location);
	}

	switch (node.keyword) {
	case Keyword::Schema: {
		// Root schema - validate all child keywords
		for (const auto& child : node.children) {
			validate_node(child, instance, child.uri.to_string(), instance_location, ctx);
		}
		break;
	}
	case Keyword::Type: {
		std::string actual_type = type_to_string(instance);
		if (node.value.type() == simdjson::dom::element_type::STRING) {
			std::string_view type_str;
			if (node.value.get(type_str) == simdjson::SUCCESS) {
				if (!check_type_match(instance, type_str)) {
					errors_.emplace_back(
						InstanceLocation(),
						schema_location,
						"Type mismatch: expected \"" + std::string(type_str) + "\", got \"" + actual_type + "\"");
				}
			}
		} else if (node.value.type() == simdjson::dom::element_type::ARRAY) {
			bool matched = false;
			for (auto type_val : node.value.get_array()) {
				std::string_view type_str;
				if (type_val.get(type_str) == simdjson::SUCCESS && check_type_match(instance, type_str)) {
					matched = true;
					break;
				}
			}
			if (!matched) {
				errors_.emplace_back(
					InstanceLocation(),
					schema_location,
					"Type mismatch: expected one of array types, got \"" + actual_type + "\"");
			}
		}
		break;
	}

	case Keyword::Enum: {
		bool found = false;
		for (auto enum_val : node.value.get_array()) {
			if (json_equal(instance, enum_val)) {
				found = true;
				break;
			}
		}
		if (!found) {
			std::stringstream ss;
			ss << "Value not in enum: allowed values are [";
			bool first = true;
			for (auto enum_val : node.value.get_array()) {
				if (!first)
					ss << ", ";
				first = false;
				// Simple string representation of the value
				std::string_view sv;
				int64_t iv;
				uint64_t uv;
				double dv;
				bool bv;
				if (enum_val.get(sv) == simdjson::SUCCESS)
					ss << "\"" << std::string(sv) << "\"";
				else if (enum_val.get(iv) == simdjson::SUCCESS)
					ss << iv;
				else if (enum_val.get(uv) == simdjson::SUCCESS)
					ss << uv;
				else if (enum_val.get(dv) == simdjson::SUCCESS)
					ss << dv;
				else if (enum_val.get(bv) == simdjson::SUCCESS)
					ss << (bv ? "true" : "false");
				else
					ss << "<" << type_to_string(enum_val) << ">";
			}
			ss << "]";
			errors_.emplace_back(InstanceLocation(), schema_location, ss.str());
		}
		break;
	}

	case Keyword::Const: {
		if (!json_equal(instance, node.value)) {
			std::stringstream ss;
			ss << "Value does not match const: expected ";
			std::string_view sv;
			int64_t iv;
			uint64_t uv;
			double dv;
			bool bv;
			if (node.value.get(sv) == simdjson::SUCCESS)
				ss << "\"" << std::string(sv) << "\"";
			else if (node.value.get(iv) == simdjson::SUCCESS)
				ss << iv;
			else if (node.value.get(uv) == simdjson::SUCCESS)
				ss << uv;
			else if (node.value.get(dv) == simdjson::SUCCESS)
				ss << dv;
			else if (node.value.get(bv) == simdjson::SUCCESS)
				ss << (bv ? "true" : "false");
			else
				ss << "<" << type_to_string(node.value) << ">";
			ss << ", got " << type_to_string(instance);
			errors_.emplace_back(InstanceLocation(), schema_location, ss.str());
		}
		break;
	}

	case Keyword::MultipleOf: {
		double divisor;
		if (node.value.get(divisor) == simdjson::SUCCESS && divisor != 0) {
			double value;
			if (instance.get(value) == simdjson::SUCCESS) {
				double remainder = std::fmod(value, divisor);
				if (std::abs(remainder) > 1e-10 && std::abs(remainder - divisor) > 1e-10) {
					errors_.emplace_back(
						InstanceLocation(),
						schema_location,
						"Value " + std::to_string(value) + " is not a multiple of " + std::to_string(divisor));
				}
			}
		}
		break;
	}

	case Keyword::Maximum: {
		double max_val;
		if (node.value.get(max_val) == simdjson::SUCCESS) {
			double value;
			if (instance.get(value) == simdjson::SUCCESS && value > max_val) {
				errors_.emplace_back(
					InstanceLocation(),
					schema_location,
					"Value " + std::to_string(value) + " exceeds maximum of " + std::to_string(max_val));
			}
		}
		break;
	}

	case Keyword::Minimum: {
		double min_val;
		if (node.value.get(min_val) == simdjson::SUCCESS) {
			double value;
			if (instance.get(value) == simdjson::SUCCESS && value < min_val) {
				errors_.emplace_back(
					InstanceLocation(),
					schema_location,
					"Value " + std::to_string(value) + " is below minimum of " + std::to_string(min_val));
			}
		}
		break;
	}

	case Keyword::ExclusiveMaximum: {
		double max_val;
		if (node.value.get(max_val) == simdjson::SUCCESS) {
			double value;
			if (instance.get(value) == simdjson::SUCCESS && value >= max_val) {
				errors_.emplace_back(
					InstanceLocation(),
					schema_location,
					"Value " + std::to_string(value) + " must be less than exclusiveMaximum of " +
						std::to_string(max_val));
			}
		}
		break;
	}

	case Keyword::ExclusiveMinimum: {
		double min_val;
		if (node.value.get(min_val) == simdjson::SUCCESS) {
			double value;
			if (instance.get(value) == simdjson::SUCCESS && value <= min_val) {
				errors_.emplace_back(
					InstanceLocation(),
					schema_location,
					"Value " + std::to_string(value) + " must be greater than exclusiveMinimum of " +
						std::to_string(min_val));
			}
		}
		break;
	}

	case Keyword::MaxLength:
	case Keyword::MinLength: {
		int64_t limit;
		if (node.value.get(limit) == simdjson::SUCCESS) {
			std::string_view str;
			if (instance.get(str) == simdjson::SUCCESS) {
				size_t actual_len = str.size();
				bool exceeded = (node.keyword == Keyword::MaxLength) ? static_cast<int64_t>(actual_len) > limit
																	 : static_cast<int64_t>(actual_len) < limit;
				if (exceeded) {
					std::string msg = node.keyword == Keyword::MaxLength
										  ? "String length " + std::to_string(actual_len) + " exceeds maxLength of " +
												std::to_string(limit)
										  : "String length " + std::to_string(actual_len) + " is below minLength of " +
												std::to_string(limit);
					errors_.emplace_back(InstanceLocation(), schema_location, msg);
				}
			}
		}
		break;
	}

	case Keyword::Pattern: {
		std::string_view pattern;
		if (node.value.get(pattern) == simdjson::SUCCESS) {
			validate_pattern(instance, pattern, schema_location, instance_location);
		}
		break;
	}

	case Keyword::MaxItems:
	case Keyword::MinItems: {
		int64_t limit;
		if (node.value.get(limit) == simdjson::SUCCESS) {
			if (instance.type() == simdjson::dom::element_type::ARRAY) {
				size_t count = 0;
				for ([[maybe_unused]] auto item : instance.get_array())
					++count;
				bool exceeded = (node.keyword == Keyword::MaxItems) ? count > static_cast<size_t>(limit)
																	: count < static_cast<size_t>(limit);
				if (exceeded) {
					std::string msg =
						node.keyword == Keyword::MaxItems
							? "Array length " + std::to_string(count) + " exceeds maxItems of " + std::to_string(limit)
							: "Array length " + std::to_string(count) + " is below minItems of " +
								  std::to_string(limit);
					errors_.emplace_back(InstanceLocation(), schema_location, msg);
				}
			}
		}
		break;
	}

	case Keyword::UniqueItems: {
		bool unique;
		if (node.value.get(unique) == simdjson::SUCCESS && unique) {
			if (instance.type() == simdjson::dom::element_type::ARRAY) {
				std::unordered_set<std::string> seen;
				int dup_index = -1;
				int idx = 0;
				for (auto item : instance.get_array()) {
					std::string serialized = simdjson::minify(item);
					if (seen.contains(serialized)) {
						dup_index = idx;
						break;
					}
					seen.insert(std::move(serialized));
					++idx;
				}
				if (dup_index >= 0) {
					errors_.emplace_back(
						InstanceLocation(),
						schema_location,
						"Array contains duplicate items at index " + std::to_string(dup_index));
				}
			}
		}
		break;
	}

	case Keyword::MaxContains:
	case Keyword::MinContains: {
		int64_t limit;
		if (node.value.get(limit) == simdjson::SUCCESS) {
			if (node.keyword == Keyword::MaxContains) {
				ctx.max_contains_count = static_cast<size_t>(limit);
			} else {
				ctx.min_contains_count = static_cast<size_t>(limit);
			}
		}
		break;
	}

	case Keyword::MaxProperties:
	case Keyword::MinProperties: {
		int64_t limit;
		if (node.value.get(limit) == simdjson::SUCCESS) {
			if (instance.type() == simdjson::dom::element_type::OBJECT) {
				size_t count = 0;
				for ([[maybe_unused]] auto field : instance.get_object())
					++count;
				bool exceeded = (node.keyword == Keyword::MaxProperties) ? count > static_cast<size_t>(limit)
																		 : count < static_cast<size_t>(limit);
				if (exceeded) {
					std::string msg = node.keyword == Keyword::MaxProperties
										  ? "Object has " + std::to_string(count) +
												" properties, exceeds maxProperties of " + std::to_string(limit)
										  : "Object has " + std::to_string(count) +
												" properties, below minProperties of " + std::to_string(limit);
					errors_.emplace_back(InstanceLocation(), schema_location, msg);
				}
			}
		}
		break;
	}

	case Keyword::Required: {
		if (instance.type() == simdjson::dom::element_type::OBJECT) {
			auto obj = instance.get_object();
			for (auto req : node.value.get_array()) {
				std::string_view req_name;
				if (req.get(req_name) == simdjson::SUCCESS) {
					bool found = false;
					for (auto field : obj) {
						if (field.key == req_name) {
							found = true;
							break;
						}
					}
					if (!found) {
						errors_.emplace_back(
							InstanceLocation(), schema_location, "Missing required property: " + std::string(req_name));
					}
				}
			}
		}
		break;
	}

	case Keyword::AllOf: {
		for (const auto& child : node.children) {
			validate_node(child, instance, child.uri.to_string(), instance_location, ctx);
		}
		break;
	}

	case Keyword::AnyOf: {
		bool matched = false;
		size_t prev_errors = errors_.size();
		for (const auto& child : node.children) {
			size_t current_errors = errors_.size();
			validate_node(child, instance, child.uri.to_string(), instance_location, ctx);
			if (errors_.size() == current_errors) {
				matched = true;
				break;
			}
		}
		errors_.resize(prev_errors);
		if (!matched) {
			errors_.emplace_back(InstanceLocation(), schema_location, "Value does not match any of the anyOf schemas");
		}
		break;
	}

	case Keyword::OneOf: {
		size_t matches = 0;
		size_t prev_errors = errors_.size();
		for (const auto& child : node.children) {
			size_t current_errors = errors_.size();
			validate_node(child, instance, child.uri.to_string(), instance_location, ctx);
			if (errors_.size() == current_errors) {
				++matches;
			} else {
				errors_.resize(current_errors);
			}
		}
		errors_.resize(prev_errors);
		if (matches != 1) {
			std::string msg = "Value must match exactly one of the oneOf schemas";
			if (matches == 0)
				msg += " (matched " + std::to_string(matches) + ")";
			else
				msg += " (matched " + std::to_string(matches) + ", expected 1)";
			errors_.emplace_back(InstanceLocation(), schema_location, msg);
		}
		break;
	}

	case Keyword::Not: {
		if (!node.children.empty()) {
			size_t prev_errors = errors_.size();
			validate_node(node.children[0], instance, node.children[0].uri.to_string(), instance_location, ctx);
			if (errors_.size() == prev_errors) {
				errors_.emplace_back(InstanceLocation(), schema_location, "Value must not match the \"not\" schema");
			} else {
				errors_.resize(prev_errors);
			}
		}
		break;
	}

	case Keyword::If: {
		if (!node.children.empty()) {
			size_t prev_errors = errors_.size();
			validate_node(node.children[0], instance, node.children[0].uri.to_string(), instance_location, ctx);
			bool if_matched = (errors_.size() == prev_errors);
			errors_.resize(prev_errors);

			const SchemaNode* then_node = nullptr;
			const SchemaNode* else_node = nullptr;
			if (node.parent) {
				for (const auto& sibling : node.parent->children) {
					if (sibling.keyword == Keyword::Then)
						then_node = &sibling;
					else if (sibling.keyword == Keyword::Else)
						else_node = &sibling;
				}
			}

			if (if_matched && then_node) {
				validate_node(*then_node, instance, then_node->uri.to_string(), instance_location, ctx);
			} else if (!if_matched && else_node) {
				validate_node(*else_node, instance, else_node->uri.to_string(), instance_location, ctx);
			}
		}
		break;
	}

	case Keyword::Properties: {
		if (instance.type() == simdjson::dom::element_type::OBJECT) {
			auto obj = instance.get_object();

			for (const auto& child : node.children) {
				std::string full_uri = child.uri.to_string();
				std::string prop_name = full_uri;
				size_t last_slash = prop_name.rfind('/');
				if (last_slash != std::string::npos) {
					prop_name = prop_name.substr(last_slash + 1);
				}

				simdjson::dom::element prop_value;
				bool found = false;
				for (auto field : obj) {
					if (std::string(field.key) == prop_name) {
						prop_value = field.value;
						found = true;
						break;
					}
				}

				if (!found) {
					continue;
				}

				std::string prop_inst_loc =
					instance_location.empty() ? "/" + prop_name : instance_location + "/" + prop_name;

				for (const auto& subchild : child.children) {
					validate_node(subchild, prop_value, subchild.uri.to_string(), prop_inst_loc, ctx);
				}

				ctx.evaluated_properties.emplace(prop_name);
			}
		}
		break;
	}

	case Keyword::AdditionalProperties: {
		if (instance.type() == simdjson::dom::element_type::OBJECT && !node.children.empty()) {
			auto obj = instance.get_object();
			for (auto field : obj) {
				std::string key = std::string(field.key);
				if (ctx.evaluated_properties.find(key) == ctx.evaluated_properties.end()) {
					std::string prop_inst_loc = instance_location.empty() ? "/" + key : instance_location + "/" + key;
					validate_node(node.children[0], field.value, node.children[0].uri.to_string(), prop_inst_loc, ctx);
				}
			}
		}
		break;
	}

	case Keyword::Items: {
		if (instance.type() == simdjson::dom::element_type::ARRAY && !node.children.empty()) {
			size_t index = 0;
			for (auto item : instance.get_array()) {
				std::string item_inst_loc = instance_location.empty() ? "/" + std::to_string(index)
																	  : instance_location + "/" + std::to_string(index);
				validate_node(node.children[0], item, node.children[0].uri.to_string(), item_inst_loc, ctx);
				ctx.evaluated_items.push_back(true);
				++index;
			}
		}
		break;
	}

	case Keyword::PrefixItems: {
		if (instance.type() == simdjson::dom::element_type::ARRAY) {
			size_t index = 0;
			size_t child_index = 0;
			for (auto item : instance.get_array()) {
				std::string item_inst_loc = instance_location.empty() ? "/" + std::to_string(index)
																	  : instance_location + "/" + std::to_string(index);

				if (child_index < node.children.size()) {
					validate_node(
						node.children[child_index],
						item,
						node.children[child_index].uri.to_string(),
						item_inst_loc,
						ctx);
				} else if (!node.children.empty()) {
					validate_node(node.children.back(), item, node.children.back().uri.to_string(), item_inst_loc, ctx);
				}

				ctx.evaluated_items.push_back(true);
				++index;
				if (child_index < node.children.size())
					++child_index;
			}
		}
		break;
	}

	case Keyword::Contains: {
		if (instance.type() == simdjson::dom::element_type::ARRAY && !node.children.empty()) {
			size_t match_count = 0;
			size_t index = 0;
			for (auto item : instance.get_array()) {
				std::string item_inst_loc = instance_location.empty() ? "/" + std::to_string(index)
																	  : instance_location + "/" + std::to_string(index);
				size_t prev_errors = errors_.size();
				validate_node(node.children[0], item, node.children[0].uri.to_string(), item_inst_loc, ctx);
				if (errors_.size() == prev_errors) {
					++match_count;
				} else {
					errors_.resize(prev_errors);
				}
				++index;
			}

			if (match_count < 1) {
				errors_.emplace_back(
					InstanceLocation(),
					schema_location,
					"Array does not contain any items matching the \"contains\" schema");
			}
		}
		break;
	}

	case Keyword::Ref: {
		std::string_view ref_target;
		if (node.value.get(ref_target) == simdjson::SUCCESS) {
			SchemaNode* target = find_ref(std::string(ref_target));
			if (target) {
				validate_node(*target, instance, target->uri.to_string(), instance_location, ctx);
			} else {
				errors_.emplace_back(
					InstanceLocation(), schema_location, "Cannot resolve $ref: " + std::string(ref_target));
			}
		}
		break;
	}

	case Keyword::PatternProperties: {
		if (instance.type() == simdjson::dom::element_type::OBJECT) {
			auto obj = instance.get_object();

			for (const auto& child : node.children) {
				std::string pattern_name = child.uri.to_string();
				size_t last_slash = pattern_name.rfind('/');
				if (last_slash != std::string::npos) {
					pattern_name = pattern_name.substr(last_slash + 1);
				}

				for (auto field : obj) {
					std::string key = std::string(field.key);
					try {
						std::regex re(pattern_name);
						if (std::regex_search(key, re)) {
							std::string prop_inst_loc =
								instance_location.empty() ? "/" + key : instance_location + "/" + key;
							for (const auto& subchild : child.children) {
								validate_node(subchild, field.value, subchild.uri.to_string(), prop_inst_loc, ctx);
							}
							ctx.evaluated_properties.emplace(key);
						}
					} catch (const std::regex_error&) {
						if (key.find(pattern_name) != std::string::npos) {
							std::string prop_inst_loc =
								instance_location.empty() ? "/" + key : instance_location + "/" + key;
							for (const auto& subchild : child.children) {
								validate_node(subchild, field.value, subchild.uri.to_string(), prop_inst_loc, ctx);
							}
							ctx.evaluated_properties.emplace(key);
						}
					}
				}
			}
		}
		break;
	}

	case Keyword::DependentRequired: {
		if (instance.type() == simdjson::dom::element_type::OBJECT) {
			auto obj = instance.get_object();

			for (const auto& child : node.children) {
				std::string dep_name = child.uri.to_string();
				size_t last_slash = dep_name.rfind('/');
				if (last_slash != std::string::npos) {
					dep_name = dep_name.substr(last_slash + 1);
				}

				bool property_exists = false;
				for (auto field : obj) {
					if (std::string(field.key) == dep_name) {
						property_exists = true;
						break;
					}
				}

				if (property_exists && !child.children.empty()) {
					for (const auto& req_prop : child.children[0].children) {
						std::string req_name = req_prop.uri.to_string();
						size_t last_slash = req_name.rfind('/');
						if (last_slash != std::string::npos) {
							req_name = req_name.substr(last_slash + 1);
						}

						bool found = false;
						for (auto field : obj) {
							if (std::string(field.key) == req_name) {
								found = true;
								break;
							}
						}

						if (!found) {
							errors_.emplace_back(
								InstanceLocation(),
								schema_location,
								"Missing required property '" + req_name + "' due to dependency '" + dep_name + "'");
						}
					}
				}
			}
		}
		break;
	}

	case Keyword::DependentSchemas: {
		if (instance.type() == simdjson::dom::element_type::OBJECT) {
			auto obj = instance.get_object();

			for (const auto& child : node.children) {
				std::string dep_name = child.uri.to_string();
				size_t last_slash = dep_name.rfind('/');
				if (last_slash != std::string::npos) {
					dep_name = dep_name.substr(last_slash + 1);
				}

				bool property_exists = false;
				for (auto field : obj) {
					if (std::string(field.key) == dep_name) {
						property_exists = true;
						break;
					}
				}

				if (property_exists) {
					for (const auto& subchild : child.children) {
						validate_node(subchild, instance, subchild.uri.to_string(), instance_location, ctx);
					}
				}
			}
		}
		break;
	}

	case Keyword::PropertyNames: {
		if (instance.type() == simdjson::dom::element_type::OBJECT && !node.children.empty()) {
			auto obj = instance.get_object();
			for (auto field : obj) {
				std::string key = std::string(field.key);

				size_t prev_errors = errors_.size();

				if (field.value.type() == simdjson::dom::element_type::STRING) {
					std::string_view key_view;
					if (field.value.get(key_view) == simdjson::SUCCESS) {
						for (const auto& subchild : node.children[0].children) {
							switch (subchild.keyword) {
							case Keyword::Pattern: {
								std::string_view pattern;
								if (subchild.value.get(pattern) == simdjson::SUCCESS) {
									validate_pattern(field.value, pattern, subchild.uri.to_string(), instance_location);
								}
								break;
							}
							case Keyword::MaxLength:
							case Keyword::MinLength: {
								int64_t limit;
								if (subchild.value.get(limit) == simdjson::SUCCESS) {
									size_t actual_len = key_view.size();
									bool exceeded = (subchild.keyword == Keyword::MaxLength)
														? static_cast<int64_t>(actual_len) > limit
														: static_cast<int64_t>(actual_len) < limit;
									if (exceeded) {
										std::string msg = subchild.keyword == Keyword::MaxLength
															  ? "Property name \"" + key + "\" (length " +
																	std::to_string(actual_len) +
																	") exceeds maxLength of " + std::to_string(limit)
															  : "Property name \"" + key + "\" (length " +
																	std::to_string(actual_len) +
																	") is below minLength of " + std::to_string(limit);
										errors_.emplace_back(InstanceLocation(), subchild.uri.to_string(), msg);
									}
								}
								break;
							}
							case Keyword::Const: {
								std::string_view const_val;
								if (subchild.value.get(const_val) == simdjson::SUCCESS) {
									if (key_view != const_val) {
										errors_.emplace_back(
											InstanceLocation(),
											subchild.uri.to_string(),
											"Property name \"" + key + "\" does not match const \"" +
												std::string(const_val) + "\"");
									}
								}
								break;
							}
							case Keyword::Enum: {
								bool found = false;
								for (auto enum_val : subchild.value.get_array()) {
									std::string_view enum_str;
									if (enum_val.get(enum_str) == simdjson::SUCCESS && enum_str == key_view) {
										found = true;
										break;
									}
								}
								if (!found) {
									errors_.emplace_back(
										InstanceLocation(),
										subchild.uri.to_string(),
										"Property name \"" + key + "\" not in enum");
								}
								break;
							}
							default:
								break;
							}
						}
					}
				}

				if (errors_.size() > prev_errors) {
					std::string prop_inst_loc = instance_location.empty() ? "/" + key : instance_location + "/" + key;
					errors_.emplace_back(
						InstanceLocation(),
						schema_location,
						"Property name \"" + key + "\" does not match propertyNames schema");
				}
			}
		}
		break;
	}

	case Keyword::AdditionalItems: {
		if (instance.type() == simdjson::dom::element_type::ARRAY && !node.children.empty()) {
			size_t index = 0;
			for (auto item : instance.get_array()) {
				if (index >= node.parent->children.size()) {
					std::string item_inst_loc = instance_location.empty()
													? "/" + std::to_string(index)
													: instance_location + "/" + std::to_string(index);
					validate_node(node.children[0], item, node.children[0].uri.to_string(), item_inst_loc, ctx);
				}
				++index;
			}
		}
		break;
	}

	case Keyword::UnevaluatedProperties: {
		if (instance.type() == simdjson::dom::element_type::OBJECT && !node.children.empty()) {
			auto obj = instance.get_object();
			for (auto field : obj) {
				std::string key = std::string(field.key);
				if (ctx.evaluated_properties.find(key) == ctx.evaluated_properties.end()) {
					std::string prop_inst_loc = instance_location.empty() ? "/" + key : instance_location + "/" + key;
					validate_node(node.children[0], field.value, node.children[0].uri.to_string(), prop_inst_loc, ctx);
				}
			}
		}
		break;
	}

	case Keyword::UnevaluatedItems: {
		if (instance.type() == simdjson::dom::element_type::ARRAY && !node.children.empty()) {
			size_t index = 0;
			for (auto item : instance.get_array()) {
				if (index >= ctx.evaluated_items.size() || !ctx.evaluated_items[index]) {
					std::string item_inst_loc = instance_location.empty()
													? "/" + std::to_string(index)
													: instance_location + "/" + std::to_string(index);
					validate_node(node.children[0], item, node.children[0].uri.to_string(), item_inst_loc, ctx);
				}
				++index;
			}
		}
		break;
	}

	default:
		// Handle boolean schemas for nodes without specific keyword processing
		if (node.value.type() == simdjson::dom::element_type::BOOL) {
			bool accepts;
			if (node.value.get(accepts) == simdjson::SUCCESS && !accepts) {
				errors_.emplace_back(
					InstanceLocation(), schema_location, "Value does not match schema (schema is false)");
			}
		}
		break;
	}
}

} // namespace jsonschema
