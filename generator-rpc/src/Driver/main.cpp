// SPDX-License-Identifier: Apache-2.0

#include "Driver/Driver.hpp"
#include <filesystem>
#include <iostream>
#include <string_view>

namespace fs = std::filesystem;

int main(int argc, char* argv[]) try {
	if (argc < 3) {
		std::cerr << "Usage: siesta-rpc-generator --input <file> [--format proto|openrpc]\n";
		return 1;
	}

	fs::path input_path;
	std::string format = "openrpc";

	for (int i = 1; i < argc; ++i) {
		std::string_view arg = argv[i];
		if (arg == "--input" && i + 1 < argc) input_path = argv[++i];
		else if (arg == "--format" && i + 1 < argc) format = argv[++i];
	}

	if (input_path.empty()) {
		std::cerr << "--input is required.\n";
		return 1;
	}

	bool ok;
	if (format == "proto") {
		ok = driver::generateFromProto(input_path, {});
	} else if (format == "openrpc") {
		ok = driver::generateFromOpenRPC(input_path, {});
	} else {
		std::cerr << "Unknown format: " << format << " (use proto or openrpc)\n";
		return 1;
	}

	return ok ? 0 : 1;
} catch (const std::exception& e) {
	std::cerr << e.what() << "\n";
	return 1;
}
