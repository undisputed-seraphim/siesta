// SPDX-License-Identifier: Apache-2.0
#include "codegen_python.hpp"
#include "endpoint_util.hpp"
#include "openapi.hpp"
#include "openapi3.hpp"
#include <algorithm>

namespace codegen {

PythonGenerator::PythonGenerator(const schema::NormalizedAST& ast, const openapi::v3::OpenAPIv3& spec)
	: ast_(ast)
	, spec_(spec) {
	endpoints_ = parseEndpoints();
}

ClientParam PythonGenerator::resolveAndMapParameter(const openapi::v3::Parameter& raw_param,
                                                     const std::unordered_map<std::string, ClientParam>& fetched_params) {
	ClientParam p;

	if (raw_param.IsRef()) {
		std::string ref_name = refComponentName(raw_param.ref());
		auto it = fetched_params.find(ref_name);
		if (it != fetched_params.end()) {
			p = it->second;
			return p;
		}
	}

	p.name = std::string(raw_param.name());
	p.location = std::string(raw_param.in());
	p.required = raw_param.required();
	p.description = std::string(raw_param.description());
	auto schema = raw_param.schema();
	p.schema_type = std::string(schema.type());
	if (!schema.format().empty()) {
		p.format = std::string(schema.format());
	}
	p.cpp_type = schemaToCppType(schema);
	return p;
}

std::string PythonGenerator::schemaToCppType(const openapi::v3::JsonSchema& schema) {
	if (schema.IsRef()) {
		return resolveRefName(schema.ref());
	}
	std::string_view type = schema.type();
	std::string_view format = schema.format();
	if (type == "string")
		return "std::string";
	if (type == "integer") {
		if (format == "int64")
			return "int64_t";
		if (format == "uint32")
			return "uint32_t";
		if (format == "uint64")
			return "uint64_t";
		return "int32_t";
	}
	if (type == "number")
		return "double";
	if (type == "boolean")
		return "bool";
	if (type == "array") {
		const auto& arr = static_cast<const openapi::v3::Array&>(schema);
		auto items = arr.items();
		std::string elem_type = schemaToCppType(items);
		return "std::vector<" + elem_type + ">";
	}
	return "std::string";
}

std::vector<PyEndpoint> PythonGenerator::parseEndpoints() {
	std::vector<PyEndpoint> endpoints;
	const auto& paths = spec_.paths();
	const auto& comp_params_raw = spec_.components().parameters();

	// Pre-fetch components/parameters to avoid simdjson on-demand re-iteration
	std::unordered_map<std::string, ClientParam> fetched_params;
	for (const auto& [n, p_obj] : comp_params_raw) {
		ClientParam cp;
		cp.name = std::string(p_obj.name());
		cp.location = std::string(p_obj.in());
		cp.required = p_obj.required();
		cp.description = std::string(p_obj.description());
		auto schema = p_obj.schema();
		cp.schema_type = std::string(schema.type());
		if (!schema.format().empty()) {
			cp.format = std::string(schema.format());
		}
		cp.cpp_type = schemaToCppType(schema);
		fetched_params[std::string(n)] = std::move(cp);
	}

	// Collect path-level parameters into a map by name
	std::unordered_map<std::string, ClientParam> path_params_map;
	for (const auto& [path_sv, path_obj] : paths) {
		auto path_params_list = path_obj.parameters();
		for (const auto& param : path_params_list) {
			ClientParam cp = resolveAndMapParameter(param, fetched_params);
			path_params_map[cp.name] = cp;
		}
	}

	for (const auto& [path_sv, path_obj] : paths) {
		std::string path(path_sv);
		auto path_ops = path_obj.operations();

		for (const auto& [method_sv, op_obj] : path_ops) {
			std::string method(method_sv);
			std::transform(method.begin(), method.end(), method.begin(), ::tolower);

			if (!isSupportedMethod(method)) {
				continue;
			}

			PyEndpoint ep;
			ep.method = method;
			ep.path = path;

			try {
				auto summary = op_obj.summary();
				if (!summary.empty()) {
					ep.summary = std::string(summary);
				}
				auto description = op_obj.description();
				if (!description.empty()) {
					ep.description = std::string(description);
				}
			} catch (...) {
			}

			ep.function_name = generateFunctionName(method, path);

			std::unordered_map<std::string, ClientParam> op_overrides;
			std::vector<std::string> param_order;
			for (const auto& [name, _] : path_params_map) {
				param_order.push_back(name);
			}

			auto op_params_list = op_obj.parameters();
			for (const auto& param : op_params_list) {
				ClientParam cp = resolveAndMapParameter(param, fetched_params);
				auto it = path_params_map.find(cp.name);
				if (it == path_params_map.end() && op_overrides.find(cp.name) == op_overrides.end()) {
					param_order.push_back(cp.name);
				}
				op_overrides[cp.name] = cp;
			}

			auto lookup = [&](const std::string& name) -> const ClientParam& {
				auto oit = op_overrides.find(name);
				if (oit != op_overrides.end()) return oit->second;
				return path_params_map.at(name);
			};
			std::vector<ClientParam> ordered_params;
			for (const auto& name : param_order) {
				const auto& cp = lookup(name);
				if (cp.location == "path") {
					ordered_params.push_back(cp);
				}
			}
			for (const auto& name : param_order) {
				const auto& cp = lookup(name);
				if (cp.location == "query") {
					ordered_params.push_back(cp);
				}
			}
			for (const auto& name : param_order) {
				const auto& cp = lookup(name);
				if (cp.location == "header") {
					ordered_params.push_back(cp);
				}
			}

			// Sanitize parameter names to avoid conflicts
			for (auto& p : ordered_params) {
				p.name = sanitizeParamName(p.name);
			}
			ep.params = std::move(ordered_params);
			endpoints.push_back(std::move(ep));
		}
	}

	return endpoints;
}

void PythonGenerator::emitModulePreamble(std::ostream& out, const std::string& module_name) {
	out << "// SPDX-License-Identifier: Apache-2.0\n";
	out << "// Auto-generated Python extension module\n";
	out << "// Generated by siesta-generator\n";
	out << "//\n";
	out << "// Build instructions:\n";
	out << "//   1. Ensure nanobind is available (find_package(nanobind 2.12 REQUIRED))\n";
	out << "//   2. Link against siesta, boost::json, boost::system\n";
	out << "//   3. Use: nanobind_add_module(" << module_name << " " << module_name << ".cpp)\n";
	out << "\n";
	out << "#include <nanobind/nanobind.h>\n";
	out << "#include <nanobind/stl/string.h>\n";
	out << "#include <nanobind/stl/optional.h>\n";
	out << "#include <nanobind/stl/variant.h>\n";
	out << "#include <nanobind/stl/vector.h>\n";
	out << "#include <nanobind/stl/shared_ptr.h>\n";
	out << "\n";
	out << "#include <boost/asio/use_future.hpp>\n";
	out << "#include <boost/json.hpp>\n";
	out << "#include <future>\n";
	out << "\n";
	out << "#include \"client.hpp\"\n";
	out << "#include \"openapi_defs.hpp\"\n";
	out << "\n";
	out << "namespace nb = nanobind;\n";
	out << "\n";
	out << "namespace {\n";
	out << "\n";
	out << "// Convert boost::json::value to Python object\n";
	out << "nb::object json_to_python(const boost::json::value& jv) {\n";
	out << "\ttry {\n";
	out << "\t\tswitch (jv.kind()) {\n";
	out << "\t\tcase boost::json::kind::null:\n";
	out << "\t\t\treturn nb::none();\n";
	out << "\t\tcase boost::json::kind::bool_:\n";
	out << "\t\t\treturn nb::bool_(jv.get_bool());\n";
	out << "\t\tcase boost::json::kind::int64:\n";
	out << "\t\t\treturn nb::int_(jv.get_int64());\n";
	out << "\t\tcase boost::json::kind::uint64:\n";
	out << "\t\t\treturn nb::int_(jv.get_uint64());\n";
	out << "\t\tcase boost::json::kind::double_:\n";
	out << "\t\t\treturn nb::float_(jv.get_double());\n";
	out << "\t\tcase boost::json::kind::string:\n";
	out << "\t\t\treturn nb::str(jv.as_string().data());\n";
	out << "\t\tcase boost::json::kind::array: {\n";
	out << "\t\t\tnb::list py_list;\n";
	out << "\t\t\tfor (const auto& elem : jv.get_array()) {\n";
	out << "\t\t\t\tpy_list.append(json_to_python(elem));\n";
	out << "\t\t\t}\n";
	out << "\t\t\treturn std::move(py_list);\n";
	out << "\t\t}\n";
	out << "\t\tcase boost::json::kind::object: {\n";
	out << "\t\t\tnb::dict py_dict;\n";
	out << "\t\t\tfor (const auto& member : jv.get_object()) {\n";
	out << "\t\t\t\tpy_dict[nb::str(member.key().data())] = json_to_python(member.value());\n";
	out << "\t\t\t}\n";
	out << "\t\t\treturn std::move(py_dict);\n";
	out << "\t\t}\n";
	out << "\t\tdefault:\n";
	out << "\t\t\treturn nb::str(\"<unknown json type>\");\n";
	out << "\t\t}\n";
	out << "\t} catch (const std::exception& e) {\n";
	out << "\t\treturn nb::str(\"<json conversion error: \") + nb::str(e.what()) + nb::str(\">\");\n";
	out << "\t}\n";
	out << "}\n";
	out << "\n";
	out << "// Extract JSON body from HTTP response\n";
	out << "nb::object extract_response_json(const ::siesta::beast::ClientBase::outcome_type& outcome) {\n";
	out << "\ttry {\n";
	out << "\t\tconst auto& response = outcome.value();\n";
	out << "\t\tconst auto& body = response.body();\n";
	out << "\t\tif (body.empty()) {\n";
	out << "\t\t\treturn nb::dict();\n";
	out << "\t\t}\n";
	out << "\t\t// Parse JSON from response body\n";
	out << "\t\tboost::system::error_code ec;\n";
	out << "\t\tauto jv = boost::json::parse(body, ec);\n";
	out << "\t\tif (ec) {\n";
	out << "\t\t\t// If JSON parsing fails, return raw body as string\n";
	out << "\t\t\treturn nb::str(body.c_str());\n";
	out << "\t\t}\n";
	out << "\t\treturn json_to_python(jv);\n";
	out << "\t} catch (const std::exception& e) {\n";
	out << "\t\tnb::dict err;\n";
	out << "\t\terr[\"error\"] = nb::str(e.what());\n";
	out << "\t\treturn std::move(err);\n";
	out << "\t}\n";
	out << "}\n";
	out << "\n";
	out << "} // anonymous namespace\n";
}

void PythonGenerator::emitEndpointWrapper(std::ostream& out, const PyEndpoint& ep, bool is_last) {
	// Emit method signature with parameters
	out << "\t\t.def(\"" << ep.function_name << "\",\n";
	out << "\t\t\t[](ClientWrapper& self";

	// Emit parameters
	for (size_t i = 0; i < ep.params.size(); ++i) {
		out << ", ";
		const auto& param = ep.params[i];
		if (!param.required) {
			out << "std::optional<" << param.cpp_type << "> " << param.name;
		} else {
			out << param.cpp_type << " " << param.name;
		}
	}

	out << ") -> nb::object {\n";

	// Get the io_context from the wrapper
	out << "\t\tboost::asio::io_context& ctx = self.ctx;\n";

	// Execute async call synchronously via use_future
	out << "\t\ttry {\n";

	// Generate the actual method call with use_future as last argument
	out << "\t\t\tauto future = self.client." << ep.function_name << "(";
	for (size_t i = 0; i < ep.params.size(); ++i) {
		if (i > 0)
			out << ", ";
		out << ep.params[i].name;
	}
	if (!ep.params.empty()) {
		out << ", ";
	}
	out << "boost::asio::use_future);\n";

	// Run the io_context to process the async request
	out << "\t\t\tctx.run();\n";

	// Get the result from the future
	out << "\t\t\tauto outcome = future.get();\n";

	// Extract JSON from response body
	out << "\t\t\treturn extract_response_json(outcome);\n";

	// Catch block
	out << "\t\t} catch (const std::exception& e) {\n";
	out << "\t\t\tnb::dict err;\n";
	out << "\t\t\terr[\"error\"] = nb::str(e.what());\n";
	out << "\t\t\treturn std::move(err);\n";
	out << "\t\t}\n";

	// Close lambda and .def() call
	std::string doc = ep.description.empty() ? ep.summary : ep.description;
	bool has_trailing = !ep.params.empty() || !doc.empty();

	if (has_trailing) {
		out << "\t\t},\n";
	} else if (is_last) {
		out << "\t\t});\n";
	} else {
		out << "\t\t),\n";
	}

	// Emit parameter names for documentation
	if (!ep.params.empty()) {
		out << "\t\t";
		for (size_t i = 0; i < ep.params.size(); ++i) {
			if (i > 0)
				out << ", ";
			out << "nb::arg(\"" << ep.params[i].name << "\")";
		}
		out << ",\n";
	}

	// Emit docstring as C++ string literal
	if (!doc.empty()) {
		std::string escaped_doc;
		for (char c : doc) {
			if (c == '"')
				escaped_doc += "\\\"";
			else if (c == '\\')
				escaped_doc += "\\\\";
			else if (c == '\n')
				escaped_doc += "\\n";
			else
				escaped_doc += c;
		}
		out << "\t\t\"" << escaped_doc << "\"\n";
	}

	// Close .def() - use ) for chaining, ; only on last
	if (has_trailing) {
		if (is_last) {
			out << "\t\t);\n";
		} else {
			out << "\t\t)\n";
		}
	}
}
void PythonGenerator::emitModuleBody(std::ostream& out, const std::vector<PyEndpoint>& endpoints) {
	out << "\n";
	out << "// Python extension module definition\n";
	out << "// The module name should match the shared library name (e.g., _binance.so -> _binance)\n";
	out << "\n";

	// Wrapper struct that owns the io_context
	out << "struct ClientWrapper {\n";
	out << "\topenapi::Client client;\n";
	out << "\tboost::asio::io_context ctx;\n";
	out << "\tClientWrapper(std::string host = \"localhost\", uint16_t port = 443)\n";
	out << "\t\t: client(ctx) {\n";
	out << "\t\tclient.start(boost::asio::ip::make_address(host), port);\n";
	out << "\t}\n";
	out << "\t~ClientWrapper() { client.stop(); }\n";
	out << "\tauto& context() { return ctx; }\n";
	out << "\tvoid start(std::string host, uint16_t port) {\n";
	out << "\t\tclient.stop();\n";
	out << "\t\tclient.start(boost::asio::ip::make_address(host), port);\n";
	out << "\t}\n";
	out << "\tvoid stop() { client.stop(); }\n";
	out << "};\n";
	out << "\n";

	// Client class definition
	out << "NB_MODULE(siesta_bindings, m) {\n";
	out << "\tm.doc() = \"Siesta-generated Python bindings for OpenAPI client\";\n";
	out << "\n";
	out << "\t// Client class wrapping the C++ siesta client\n";
	out << "\tnb::class_<ClientWrapper>(m, \"Client\")\n";
	out << "\t\t.def(nb::init<std::string, uint16_t>(), nb::arg(\"host\") = std::string(\"localhost\"), "
		   "nb::arg(\"port\") = 443)\n";
	out << "\t\t.def(\"start\", &ClientWrapper::start, nb::arg(\"host\"), nb::arg(\"port\"))\n";
	out << "\t\t.def(\"stop\", &ClientWrapper::stop)\n";
	out << "\t\t// Endpoint methods will be added here by the generator\n";

	// Emit each endpoint wrapper
	for (size_t i = 0; i < endpoints.size(); ++i) {
		out << "\n";
		emitEndpointWrapper(out, endpoints[i], i == endpoints.size() - 1);
	}

	out << "\n";
	out << "}\n";
}

void PythonGenerator::generatePythonModule(std::ostream& out, const std::string& module_name) {
	emitModulePreamble(out, module_name);
	out << "\n";
	emitModuleBody(out, endpoints_);
	out << "\n";
}

} // namespace codegen
