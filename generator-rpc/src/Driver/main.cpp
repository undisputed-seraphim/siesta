// SPDX-License-Identifier: Apache-2.0

#include "Driver/Driver.hpp"
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

int main(int argc, char* argv[]) try {
	if (argc < 3) {
		std::cerr << "Usage: siesta-openrpc-generator --input <openrpc.json> --output <dir>\n";
		return 1;
	}

	fs::path input_json;
	fs::path output_dir;

	for (int i = 1; i < argc; ++i) {
		std::string_view arg = argv[i];
		if (arg == "--input" && i + 1 < argc) input_json = argv[++i];
		else if (arg == "--output" && i + 1 < argc) output_dir = argv[++i];
	}

	if (input_json.empty() || output_dir.empty()) {
		std::cerr << "Both --input and --output are required.\n";
		return 1;
	}

	if (!driver::generateFromOpenRPC(input_json, output_dir))
		return 1;

	return 0;
} catch (const std::exception& e) {
	std::cerr << e.what() << "\n";
	return 1;
}
