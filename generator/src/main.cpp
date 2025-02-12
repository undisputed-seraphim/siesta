// SPDX-License-Identifier: Apache-2.0
#include <boost/program_options.hpp>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

#include "beast/beast.hpp"
#include "openapi.hpp"
#include "openapi2.hpp"
#include "util.hpp"

namespace fs = std::filesystem;
namespace po = ::boost::program_options;
using namespace std::literals;

int main(int argc, char* argv[]) try {
	fs::path input_json;
	fs::path output_dir;
	po::options_description desc;
	auto opts = desc.add_options();
	opts("input,i", po::value<fs::path>(&input_json), "Path to input JSON file.");
	opts("output,o", po::value<fs::path>(&output_dir), "Path to output directory.");
	opts("help,h", "Print this help message.");
	po::variables_map vm;
	po::store(po::parse_command_line(argc, argv, desc), vm);
	po::notify(vm);
	if (vm.count("help") || vm.empty()) {
		std::cout << desc << std::endl;
		return 1;
	}

	if (!fs::exists(input_json) || !fs::is_regular_file(input_json)) {
		throw std::runtime_error("File at " + input_json.string() + " does not exist.");
	} else {
		std::cout << "Reading from " << input_json.string() << std::endl;
	}
	if (!fs::exists(output_dir)) {
		fs::create_directory(output_dir);
	}
	if (!fs::is_directory(output_dir)) {
		throw std::runtime_error("Output path " + output_dir.string() + " is not a directory.");
	} else {
		std::cout << "Writing to " << output_dir.string() << std::endl;
	}

	openapi::OpenAPI file;
	if (!file.Load(input_json.string())) {
		std::cerr << "Failed to load " << input_json.string() << std::endl;
		return -1;
	}
	if (file.MajorVersion() == 2) {
		siesta::beast::beast(input_json, output_dir, static_cast<openapi::v2::OpenAPIv2&>(file));
	} else if (file.MajorVersion() == 3) {
		siesta::beast::beast(input_json, output_dir, static_cast<openapi::v3::OpenAPIv3&>(file));
	} else {
		return -1;
	}

	return 0;
} catch (const std::exception& e) {
	std::cerr << e.what() << std::endl;
	return -1;
}
