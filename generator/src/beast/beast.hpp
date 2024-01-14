#pragma once

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>

#include "../openapi2.hpp"
#include "../openapi3.hpp"
#include "../util.hpp"

namespace siesta::beast {

std::string clean_path_string(std::string_view original);

void beast(const std::filesystem::path& input, const std::filesystem::path& output, const openapi::v2::OpenAPIv2& file);
void beast(const std::filesystem::path& input, const std::filesystem::path& output, const openapi::v3::OpenAPIv3& file);

void beast_client_hpp(
	const std::filesystem::path& input,
	const std::filesystem::path& output,
	const openapi::v2::OpenAPIv2& file);
void beast_client_cpp(
	const std::filesystem::path& input,
	const std::filesystem::path& output,
	const openapi::v2::OpenAPIv2& file);

void beast_server_cpp(
	const std::filesystem::path& input,
	const std::filesystem::path& output,
	const openapi::v2::OpenAPIv2& file);
void beast_server_hpp(
	const std::filesystem::path& input,
	const std::filesystem::path& output,
	const openapi::v2::OpenAPIv2& file);

} // namespace siesta::beast