#include "Frontend/Proto/ProtoFrontend.hpp"

#include "Frontend/Proto/Parser.hpp"
#include "Frontend/Proto/ProtoBridge.hpp"
#include "IR/RPCEndpointBridge.hpp"
#include "Support/Utils.hpp"
#include <fstream>
#include <iostream>
#include <sstream>

using codegen::sanitize;

namespace codegen {

bool ProtoFrontend::parse(const std::filesystem::path& input) {
	std::ifstream in(input);
	if (!in) {
		std::cerr << "Failed to open " << input << "\n";
		return false;
	}

	std::ostringstream oss;
	oss << in.rdbuf();
	auto source = oss.str();

	auto result = siesta::protobuf::parse_proto(source);
	if (!result.ok) {
		std::cerr << result.error << "\n";
		return false;
	}

	std::vector<rpc::RPCMethod> rpc_methods;
	driver::proto::convertFile(result.file, ast_, rpc_methods);
	rpcToEndpoints(rpc_methods, endpoints_);

	module_name_ = result.file.package;
	if (module_name_.empty()) {
		module_name_ = input.stem().string();
	}
	module_name_ = sanitize(std::string_view(module_name_));

	return true;
}

} // namespace codegen
