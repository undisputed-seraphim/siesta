// SPDX-License-Identifier: Apache-2.0
#include "beast.hpp"

#include "openapi3.hpp"

#include "beast/v3/beastv3.hpp"

namespace fs = std::filesystem;
using namespace std::literals;

namespace siesta::beast {

std::string clean_path_string(std::string_view original) {
	std::string copy;
	copy.reserve(original.size());
	bool is_braced = false;
	bool is_colon = false;
	for (const char c : original) {
		if (c == '{') {
			is_braced = true;
		}
		if (c == '}') {
			is_braced = false;
		}
		if (c == ':' && !is_colon) {
			copy.push_back('{');
			is_colon = true;
			is_braced = true;
			continue;
		}
		if (c == '/' && is_colon) {
			copy.push_back('}');
			is_colon = false;
			is_braced = false;
		}
		if ((!is_braced || c == '{') && (!is_colon)) {
			copy.push_back(c);
		}
	}
	if (is_colon) {
		copy.push_back('}');
	}
	return copy;
}

void beast(const fs::path& input, const fs::path& output, const openapi::OpenAPI& file) {
	switch (file.MajorVersion()) {
	case 3: {
		const auto& v3file = static_cast<const openapi::v3::OpenAPIv3&>(file);
		openapi::v3::PrintStructDefinitions(v3file, input, output);
		v3::V3Printer printer(v3file, input.stem().string());
		printer.print_server(output);
		printer.print_client(output);
		return;
	}
	}
	throw std::runtime_error("Invalid OpenAPI version.");
}

} // namespace siesta::beast