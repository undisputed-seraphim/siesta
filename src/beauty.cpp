#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>

#include "openapi2.hpp"

namespace fs = std::filesystem;
using namespace std::literals;

// Beauty does not support all methods, so we have to filter and ignore unsupported methods.
constexpr auto SupportedVerbs = std::array{
    "get"sv, "put"sv, "post"sv, "options"sv, "delete"sv
};

// Writes header and impl files for beauty.
void beauty(const fs::path& input, const fs::path& output, openapi::OpenAPI2& file) {
    fs::path paths_header = output / (input.stem().string() + "_paths.hpp");
    fs::path paths_impl   = output / (input.stem().string() + "_paths.cpp");

    // Write the header file
    std::ofstream out(paths_header);
    out << "#pragma once\n"
        << "#include <beauty/beauty.hpp>\n"
        << '\n'
        << "using Request = beauty::request;\n"
        << "using Response = beauty::response;\n"
        << '\n'
        << "// DO NOT EDIT. Automatically generated from " << input.filename().string() << '\n'
        << "// This file contains function prototypes for each path/requestmethod pair.\n"
        << "// Implement the function bodies for each prototype here.\n\n";
    for (const auto& [pathstr, path] : file.paths()) {
        for (const auto& [opstr, op] : path.operations()) {

            out << ";\n\n";
        }
    }
    out << '\n'
        << "// Call this function to get an instance of a server object with all paths laid out.\n"
        << "beauty::server add_routes();" << std::endl;

    // Write the server impl file
    out = std::ofstream(paths_impl);
    out << "// DO NOT EDIT. Automatically generated from " << input.filename().string() << '\n'
        << "#include \"" << paths_header.filename().string() << "\"\n\n"
        << "beauty::server& add_routes(beauty::server& server) {\n";

    std::string function_name;
    for (const auto& [pathstr, path] : file.paths()) {
        out << "\tserver.add_route(\"" << pathstr << "\")\n";
        for (const auto& [opstr, op] : path.operations()) {
            if (std::none_of(SupportedVerbs.begin(), SupportedVerbs.end(), [&opstr](std::string_view sv) {return sv == opstr;})) {
                continue;
            }
            std::string_view opstr_ = opstr;
            if (opstr_ == "delete") {
                opstr_ = "del";
            }
            out << "\t\t." << opstr_ << "([] (const Request& req, Response& res) {\n"
                << "\t\t\t" << op.operationId() << "(req, res);\n"
                << "\t\t});\n";
        }
    }
    out << "\treturn server;\n"
        << "}" << std::endl;
}