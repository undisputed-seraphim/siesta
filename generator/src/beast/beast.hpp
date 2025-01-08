// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <filesystem>
#include <fstream>

#include "../openapi2.hpp"
#include "../openapi3.hpp"
#include "../util.hpp"

namespace siesta::beast {

std::string clean_path_string(std::string_view original);

void beast(const std::filesystem::path& input, const std::filesystem::path& output, const openapi::OpenAPI& file);

template <std::derived_from<openapi::OpenAPI> OpenAPIRevision>
class BasePrinter {
public:
	BasePrinter(const OpenAPIRevision& file_, const std::string& name_)
		: file(file_)
		, name(name_) {}

protected:
	const OpenAPIRevision& file;
	std::string name;
	std::filesystem::path srv_hpp, srv_cpp, cli_hpp, cli_cpp;
	std::ofstream srv_hpp_ofs, srv_cpp_ofs, cli_hpp_ofs, cli_cpp_ofs;
};

} // namespace siesta::beast