#pragma once

#include <filesystem>
#include <string_view>

namespace driver {

enum class GenMode { client, server, both };

bool generate(const std::filesystem::path& input_path,
              const std::filesystem::path& output_path,
              GenMode mode = GenMode::both,
              bool python = true,
              const std::string& backend = "beast",
              const std::string& ns = "");

} // namespace driver
