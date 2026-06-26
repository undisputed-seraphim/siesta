// SPDX-License-Identifier: Apache-2.0
#include "Support/Filenames.hpp"
#include "Backend/Beast/BeastServerGen.hpp"
#include "Backend/Beast/ServerEmitter.hpp"
#include <fstream>

namespace codegen {

void BeastServerGenerator::operator()(const CodegenArgs& args, const std::filesystem::path& output_dir) {
	if (!args.endpoints || args.endpoints->empty()) {
		return;
	}

	const auto& endpoints = *args.endpoints;
	std::filesystem::create_directories(output_dir);

	{
		auto hpp_path = output_dir / filenames::SERVER_HPP;
		std::ofstream out(hpp_path);
		if (out) {
			emitServerHpp(out, endpoints, args.ns);
		}
	}
	{
		auto cpp_path = output_dir / filenames::SERVER_CPP;
		std::ofstream out(cpp_path);
		if (out) {
			emitServerCpp(out, endpoints, args.ns);
		}
	}
}

} // namespace codegen
