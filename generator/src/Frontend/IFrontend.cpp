#include "Frontend/IFrontend.hpp"

#include "Frontend/OpenAPI/OpenAPIFrontend.hpp"
#include "Frontend/OpenRPC/OpenRPCFrontend.hpp"
#include "Frontend/Proto/ProtoFrontend.hpp"
#include <iostream>

namespace codegen {

std::unique_ptr<IFrontend> IFrontend::create(
	const std::filesystem::path& input,
	std::string_view format_hint) {

	if (!format_hint.empty()) {
		auto f = create_named(format_hint);
		if (f && f->parse(input)) return f;
		return nullptr;
	}

	if (input.extension() == ".proto") {
		auto f = create_named("proto");
		if (f && f->parse(input)) return f;
		return nullptr;
	}

	for (auto name : {"openapi", "openrpc"}) {
		auto f = create_named(name);
		if (f && f->parse(input)) return f;
	}

	std::cerr << "Could not detect format of " << input << "\n";
	return nullptr;
}

std::unique_ptr<IFrontend> IFrontend::create_named(std::string_view name) {
	if (name == "openapi")  return std::make_unique<OpenAPIFrontend>();
	if (name == "openrpc")  return std::make_unique<OpenRPCFrontend>();
	if (name == "proto")    return std::make_unique<ProtoFrontend>();
	return nullptr;
}

} // namespace codegen
