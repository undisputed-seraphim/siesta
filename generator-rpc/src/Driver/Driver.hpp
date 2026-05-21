// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>
#include <string_view>

namespace driver {

bool generateFromOpenRPC(const std::filesystem::path& input_path,
                          const std::filesystem::path& output_path);

} // namespace driver
