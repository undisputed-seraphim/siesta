#include <charconv>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <string_view>
#include <vector>

#include "openapi2.hpp"
#include "util.hpp"

namespace fs = std::filesystem;
using namespace std::literals;

// Forward-declared backends
void beast(const fs::path& input, const fs::path& output, openapi::OpenAPI2& file);
void beauty(const fs::path& input, const fs::path& output, openapi::OpenAPI2& file);
void nghttp2(const fs::path& input, const fs::path& output, openapi::OpenAPI2& file);

int main(int argc, char* argv[]) {
	if (argc < 3) {
		std::cerr << "Two args required, path to JSON file, and output file path." << std::endl;
		return 1;
	}

	fs::path input(argv[1]);
	if (!fs::exists(input) || !fs::is_regular_file(input)) {
		std::cerr << "File at " << input << " does not exist." << std::endl;
		return 1;
	} else {
		std::cout << "Reading from " << input.string() << std::endl;
	}
	fs::path output(argv[2]);
	if (!fs::exists(output) || !fs::is_directory(output)) {
		std::cerr << "Output path " << output << " does not exist or is not a directory." << std::endl;
		return 1;
	} else {
		std::cout << "Writing to " << output.string() << std::endl;
	}

	openapi::OpenAPI2 file;
	if (!file.Load(argv[1])) {
		std::cerr << "Failed to load " << argv[1] << std::endl;
		return -1;
	}

	//nghttp2(input, output, file);
	// beauty(input, output, file);
	beast(input, output, file);

	// Write the struct definitions file, same for every backend.
	fs::path definitions_file = output / (input.stem().string() + "_defs.hpp");
	auto out = std::ofstream(definitions_file);
	out << "// Automatically generated from " << input.stem() << ". Do not modify this file.\n"
		<< "#pragma once\n"
		<< "#include <array>\n"
		<< "#include <any>\n"
		<< "#include <string>\n"
		<< "#include <string_view>\n"
		<< "#include <vector>\n"
		<< "using namespace std::literals;\n"
		<< std::endl;
	file.Print(out);

	return 0;
}
