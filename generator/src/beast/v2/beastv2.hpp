// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>
#include <fstream>
#include <openapi2.hpp>

namespace siesta::beast::v2 {

class V2Printer final : private BasePrinter<openapi::v2::OpenAPIv2> {
public:
	V2Printer(const openapi::v2::OpenAPIv2& file_, const std::string& name_)
		: BasePrinter(file_, name_) {}

	void print_client(const std::filesystem::path& output_dir);
	void print_server(const std::filesystem::path& output_dir);

private:
	// Client only
	void print_client_header();
	void print_method_declarations(std::string& indent);
	void print_method_declaration(
		std::string_view pathstr,
		const openapi::v2::Path& path,
		std::string_view opstr,
		const openapi::v2::Operation& op,
		std::string& indent);
	void print_query_parameters(
		std::string_view pathstr,
		const openapi::v2::Path& path,
		std::string_view opstr,
		const openapi::v2::Operation& op);
	void print_function_body(
		std::string_view pathstr,
		const openapi::v2::Path& path,
		std::string_view opstr,
		const openapi::v2::Operation& op,
		std::string& indent);

	void print_client_body(const openapi::v2::Operation& op, std::string& indent);

	// Server only
	void print_server_hpp();
	void print_server_cpp();
	void print_query_details(const openapi::v2::Operation& op, std::string& indent);
	void print_dispatcher_function(std::string className);
};

} // namespace siesta::beast::v2