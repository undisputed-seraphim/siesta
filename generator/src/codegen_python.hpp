// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "codegen_client.hpp"
#include "openapi3.hpp"
#include "schema_ast.hpp"
#include "util.hpp"
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace openapi::v3 {
class OpenAPIv3;
}

namespace codegen {

/**
 * Shared endpoint data structure used by both ClientGenerator and PythonGenerator
 */
struct PyEndpoint {
	std::string method;
	std::string path;
	std::string function_name;
	std::string summary;
	std::string description;
	std::vector<ClientParam> params;
};

/**
 * Backend target: Generate Python extension module source (NB_MODULE)
 *
 * Generates a C++ file that:
 * - Includes the generated client.hpp and defs.hpp
 * - Creates a nanobind module with a Client class
 * - Wraps each endpoint with synchronous execution (use_future + .get())
 * - Converts JSON responses to Python dicts/lists
 */
class PythonGenerator {
public:
	/**
	 * Construct generator with AST and OpenAPI spec
	 */
	PythonGenerator(const schema::NormalizedAST& ast, const openapi::v3::OpenAPIv3& spec);

	/**
	 * Generate Python module source (.cpp file)
	 */
	void generatePythonModule(std::ostream& out, const std::string& module_name);

private:
	/**
	 * Parse all paths from the OpenAPI spec into PyEndpoint objects
	 */
	std::vector<PyEndpoint> parseEndpoints();

	/**
	 * Extract the type name from a $ref path
	 */
	static std::string resolveRefName(std::string_view ref);

	/**
	 * Resolve a parameter $ref to its actual definition from components
	 */
	ClientParam resolveAndMapParameter(const openapi::v3::Parameter& raw_param);

	/**
	 * Map an OpenAPI schema to a C++ type string
	 */
	std::string schemaToCppType(const openapi::v3::JsonSchema& schema);

	/**
	 * Generate the function name from HTTP method and path
	 */
	static std::string generateFunctionName(std::string_view method, std::string_view path);

	/**
	 * Emit the module preamble (includes, namespace, JSON conversion helpers)
	 */
	void emitModulePreamble(std::ostream& out, const std::string& module_name);

	/**
	 * Emit the NB_MODULE definition with Client class and endpoints
	 */
	void emitModuleBody(std::ostream& out, const std::vector<PyEndpoint>& endpoints);

	/**
	 * Emit a single synchronous endpoint wrapper
	 */
	void emitEndpointWrapper(std::ostream& out, const PyEndpoint& ep, bool is_last);

	/**
	 * Emit a parameter conversion helper call
	 */
	void emitParamConversion(std::ostream& out, const ClientParam& param, const std::string& py_var);

	const schema::NormalizedAST& ast_;
	const openapi::v3::OpenAPIv3& spec_;
	std::vector<PyEndpoint> endpoints_;
};

} // namespace codegen
