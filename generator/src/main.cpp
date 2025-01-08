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

int main(int argc, char* argv[]) try {
	std::setlocale(LC_ALL, "en_US.UTF-8");

	fs::path input_json;
	fs::path output_dir;
	po::options_description desc;
	auto opts = desc.add_options();
	opts("input,i", po::value<fs::path>(&input_json)->required(), "Path to input JSON file.");
	opts("output,o", po::value<fs::path>(&output_dir)->required(), "Path to output directory.");
	opts("coroutine", po::bool_switch()->default_value(false), "Generate coroutines instead of templated callbacks for endpoints.");
	opts("namespace", po::value<std::string>()->default_value("openapi"), "Set a custom namespace, defaults to 'openapi'.");
	opts("help,h", "Print this help message.");
	po::variables_map vm;
	po::store(po::parse_command_line(argc, argv, desc), vm);
	po::notify(vm);
	if (vm.count("help") || vm.empty()) {
		std::cout << desc << std::endl;
		return 1;
	}

	const auto ns = vm["namespace"].as<std::string>();

	if (!fs::exists(input_json) || !fs::is_regular_file(input_json)) {
		throw std::runtime_error("File at " + input_json.string() + " does not exist.");
	} else {
		input_json = fs::absolute(input_json);
		std::cout << "Reading from " << input_json.string() << std::endl;
	}
	output_dir = fs::absolute(output_dir);
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
	siesta::beast::beast(input_json, output_dir, file);

	return 0;
} catch (const std::exception& e) {
	std::cerr << e.what() << std::endl;
	return -1;
}
