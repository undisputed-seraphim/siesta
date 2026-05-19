#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace schema {

// Forward declarations
struct StructType;
struct VariantType;
struct ArrayType;
struct MapType;
struct EnumType;

/**
 * Type reference - can be to a named type or inline anonymous type
 */
struct TypeRef {
	std::string name;
	bool is_inline = false; // true for inline anonymous types

	bool operator==(const TypeRef& other) const = default;
	auto operator<=>(const TypeRef&) const = default;
};

/**
 * Primitive type kinds
 */
enum class PrimitiveKind { String, Integer, Number, Boolean, Null };

/**
 * Integer format
 */
enum class IntegerFormat { Int32, Int64, UInt32, UInt64 };

/**
 * Number format
 */
enum class NumberFormat { Float, Double };

/**
 * String format
 */
enum class StringFormat { None, Date, DateTime, Email, UUID, URI, Base64 };

/**
 * Primitive type with optional format
 */
struct PrimitiveType {
	PrimitiveKind kind;
	std::optional<IntegerFormat> int_format;
	std::optional<NumberFormat> num_format;
	std::optional<StringFormat> str_format;
	std::vector<std::string> enum_values; // If non-empty, this is a named primitive with enum
	std::string description;
};

/**
 * Array type definition
 */
struct ArrayType {
	TypeRef element_type;
	std::string description;
};

/**
 * Map type definition (key-value pairs)
 */
struct MapType {
	TypeRef value_type;
	std::string description;
};

/**
 * Enum value
 */
struct EnumValue {
	std::string name;
	std::string value; // JSON string representation
	std::string description;
};

/**
 * Enum type definition
 */
struct EnumType {
	std::string name;
	std::vector<EnumValue> values;
	bool is_string = true; // TODO: support integer enums
	std::string description;
};

/**
 * Struct field/member
 */
struct Member {
	std::string name;
	TypeRef type;
	bool required = false;
	std::optional<std::string> default_value;
	std::string description;
};

/**
 * Struct type definition with inheritance support
 */
struct StructType {
	std::string name;
	std::vector<Member> fields;
	std::vector<TypeRef> allOf_bases; // IS-A relationships (base classes)
	std::string description;
};

/**
 * Variant type definition (oneOf/anyOf)
 */
struct VariantType {
	std::string name;
	std::vector<TypeRef> alternatives;
	bool is_nullable = false;
	std::optional<std::string> discriminator_property; // OpenAPI discriminator for runtime dispatch
	std::string description;
};

/**
 * Schema type - can be struct, variant, array, map, enum, or primitive
 */
using SchemaType = std::variant<StructType, VariantType, ArrayType, MapType, EnumType, PrimitiveType>;

/**
 * Path item - HTTP endpoint definition
 */
struct PathItem {
	std::string path;
	std::unordered_map<std::string, std::string> operations; // method -> operation_id
	std::vector<Member> parameters;							 // path/query/header parameters
	std::string description;
};

/**
 * Normalized AST - immutable, validated representation of OpenAPI schema
 */
class NormalizedAST {
public:
	using TypeMap = std::unordered_map<std::string, SchemaType>;
	using PathMap = std::unordered_map<std::string, PathItem>;

	/**
	 * Add a type to the AST
	 */
	void addType(const std::string& name, SchemaType&& type) { types_[name] = std::move(type); }

	/**
	 * Add a path endpoint
	 */
	void addPath(const std::string& path, PathItem&& item) { paths_[path] = std::move(item); }

	/**
	 * Get type by name (returns nullptr if not found)
	 */
	const SchemaType* getType(const std::string& name) const noexcept {
		auto it = types_.find(name);
		return (it != types_.end()) ? &it->second : nullptr;
	}

	/**
	 * Get mutable type by name
	 */
	SchemaType* getType(const std::string& name) {
		auto it = types_.find(name);
		return (it != types_.end()) ? &it->second : nullptr;
	}

	/**
	 * Check if type exists
	 */
	bool hasType(const std::string& name) const { return types_.find(name) != types_.end(); }

	/**
	 * Get all types (read-only view)
	 */
	const TypeMap& getTypes() const { return types_; }

	/**
	 * Get number of types in the AST
	 */
	size_t getTypeCount() const { return types_.size(); }

	/**
	 * Get all paths (read-only view)
	 */
	const PathMap& getPaths() const { return paths_; }

	/**
	 * Get path by path string
	 */
	const PathItem* getPath(const std::string& path) const {
		auto it = paths_.find(path);
		return (it != paths_.end()) ? &it->second : nullptr;
	}

	/**
	 * Validate the AST for common schema errors
	 * Returns list of error messages (empty if valid)
	 */
	std::vector<std::string> validate() const;

private:
	void validateType(const SchemaType& type, const std::string& context, std::vector<std::string>& errors) const;

	TypeMap types_;
	PathMap paths_;
};

} // namespace schema
