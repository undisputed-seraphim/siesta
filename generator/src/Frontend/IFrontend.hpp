#pragma once

#include "Frontend/AST.hpp"
#include "IR/EndpointIR.hpp"
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace codegen {

class IFrontend {
public:
	virtual ~IFrontend() = default;

	virtual bool parse(const std::filesystem::path& input) = 0;

	virtual const schema::NormalizedAST& ast() const = 0;
	virtual const std::vector<Endpoint>& endpoints() const = 0;
	virtual std::string module_name() const = 0;

	static std::unique_ptr<IFrontend> create(
		const std::filesystem::path& input,
		std::string_view format_hint = "");

protected:
	static std::unique_ptr<IFrontend> create_named(std::string_view name);
};

} // namespace codegen
