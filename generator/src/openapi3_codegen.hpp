// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>

namespace openapi::v3::codegen {

bool generateFromOpenAPI(const std::filesystem::path& input_path, const std::filesystem::path& output_path);

} // namespace openapi::v3::codegen
