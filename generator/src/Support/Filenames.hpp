// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string_view>

namespace codegen::filenames {
inline constexpr std::string_view SERVER_HPP = "server.hpp";
inline constexpr std::string_view SERVER_CPP = "server.cpp";
inline constexpr std::string_view CLIENT_HPP = "client.hpp";
inline constexpr std::string_view DEFS_HPP   = "openapi_defs.hpp";
inline constexpr std::string_view DEFS_CPP   = "openapi_defs.cpp";
inline constexpr std::string_view PY_MODULE  = "py_module.cpp";
inline constexpr std::string_view SERVER_PY  = "server_py.cpp";
} // namespace codegen::filenames
