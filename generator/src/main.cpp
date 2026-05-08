// SPDX-License-Identifier: Apache-2.0
#include <boost/program_options.hpp>
#include <filesystem>
#include <iostream>

#include "openapi3_codegen.hpp"
#include "openapi.hpp"
#include "openapi3.hpp"
#include "util.hpp"

namespace fs = std::filesystem;
namespace po = ::boost::program_options;

int main(int argc, char* argv[]) try {
	std::setlocale(LC_ALL, "en_US.UTF-8");

	fs::path input_json;
	fs::path output_dir;
	std::string mode_str = "both";
	std::string backend = "beast";
	bool python = true;
	bool no_python = false;
	bool print_module_names = false;

	po::options_description desc;
	auto opts = desc.add_options();
	opts("input,i", po::value<fs::path>(&input_json), "Path to input JSON file.");
	opts("output,o", po::value<fs::path>(&output_dir), "Path to output directory.");
	opts("mode,m", po::value<std::string>(&mode_str)->default_value("both"),
		 "Generation mode: client, server, or both (default).");
	opts("backend,b", po::value<std::string>(&backend)->default_value("beast"),
		 "HTTP backend to target (default: beast).");
	opts("no-python", po::bool_switch(&no_python),
		 "Skip generating Python nanobind modules.");
	opts("print-module-names", po::bool_switch(&print_module_names),
		 "Print client and server module names to stdout and exit.");
	opts("help,h", "Print this help message.");
	po::variables_map vm;
	po::store(po::parse_command_line(argc, argv, desc), vm);
	po::notify(vm);
	if (vm.count("help")) {
		std::cout << desc << std::endl;
		return 1;
	}

	if (no_python) python = false;

	using codegen::sanitize;

	if (print_module_names) {
		if (input_json.empty()) {
			std::cerr << "Error: --input is required with --print-module-names\n";
			return 1;
		}
		if (!fs::exists(input_json) || !fs::is_regular_file(input_json)) {
			std::cerr << "File at " << input_json.string() << " does not exist.\n";
			return 1;
		}
		openapi::OpenAPI file;
		if (!file.Load(input_json.string())) {
			std::cerr << "Failed to load " << input_json.string() << "\n";
			return 1;
		}
		const auto& spec = static_cast<const openapi::v3::OpenAPIv3&>(file);
		std::string title = std::string(spec.info().title());
		std::string mn = sanitize(std::string_view(title));
		if (mn.empty()) mn = "siesta_bindings";
		std::cout << "client=" << mn << "\n";
		std::cout << "server=" << mn << "_server\n";
		return 0;
	}

	using openapi::v3::codegen::GenMode;
	GenMode gen_mode = GenMode::both;
	if (mode_str == "client")
		gen_mode = GenMode::client;
	else if (mode_str == "server")
		gen_mode = GenMode::server;
	else if (mode_str != "both") {
		std::cerr << "Invalid mode '" << mode_str << "'. Use client, server, or both.\n";
		return 1;
	}

	if (!fs::exists(input_json) || !fs::is_regular_file(input_json)) {
		throw std::runtime_error("File at " + input_json.string() + " does not exist.");
	} else {
		input_json = fs::absolute(input_json);
		std::cout << "Reading from " << input_json.string() << '\n';
	}
	output_dir = fs::absolute(output_dir);
	if (!fs::exists(output_dir)) {
		fs::create_directory(output_dir);
	}
	if (!fs::is_directory(output_dir)) {
		throw std::runtime_error("Output path " + output_dir.string() + " is not a directory.");
	} else {
		std::cout << "Writing to " << output_dir.string() << '\n';
	}

	if (!openapi::v3::codegen::generateFromOpenAPI(input_json, output_dir, gen_mode, python, backend)) {
		return -1;
	}

	return 0;
} catch (const std::exception& e) {
	std::cerr << e.what() << std::endl;
	return -1;
}
