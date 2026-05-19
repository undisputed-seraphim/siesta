// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>
#include <string_view>

namespace openapi::v3::codegen {

enum class GenMode { client, server, both };

bool generateFromOpenAPI(const std::filesystem::path& input_path,
						 const std::filesystem::path& output_path,
						 GenMode mode = GenMode::both,
						 bool python = true,
						 const std::string& backend = "beast",
						 const std::string& ns = "");

} // namespace openapi::v3::codegen
