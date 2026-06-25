#pragma once

#include "AST.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace siesta::protobuf {

struct ParseResult {
	ProtoFile file;
	bool ok = true;
	std::string error;
};

ParseResult parse_proto(std::string_view source);

} // namespace siesta::protobuf
