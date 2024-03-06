// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <array>
#include <ostream>
#include <string_view>

#include "util.hpp"

using namespace std::literals;

void ltrim(std::string_view& s) {
	while (std::isspace(s.front())) {
		s.remove_prefix(1);
	}
}

void rtrim(std::string_view& s) {
	while (std::isspace(s.back())) {
		s.remove_suffix(1);
	}
}

void trim(std::string_view& s) {
	ltrim(s);
	rtrim(s);
}

bool compare_ignore_case(std::string_view l, std::string_view r) noexcept {
	if (l.size() == r.size()) {
		return std::equal(l.begin(), l.end(), r.begin(), r.end(), [](char a, char b) -> bool {
			return std::tolower(a) == std::tolower(b);
		});
	}
	return false;
}

void sanitize(std::string& input) {
	// Replace characters with underscore.
	std::replace_if(
		input.begin(),
		input.end(),
		[](char c) -> bool {
			constexpr auto chars = std::array{'/', '-', '.', ':', '+', ' ', '(', ')', '@'};
			return std::any_of(chars.begin(), chars.end(), [c](char d) { return c == d; });
		},
		'_');

	// Reserved keywords in C++.
	constexpr auto reserved = std::array{
		"operator"sv,
		"long"sv,
		"short"sv,
		"public"sv,
		"protected"sv,
		"private"sv,
		"default"sv,
		"delete"sv,
		"namespace"sv,
	};
	if (std::any_of(reserved.begin(), reserved.end(), [&input](const std::string_view& kw) { return input == kw; })) {
		input.push_back('_');
	}

	// Names cannot start with a number.
	if (std::isdigit(input[0])) {
		input.insert(0, 1, '_');
	}
}

std::string sanitize(std::string_view input) {
	std::string ret(input);
	sanitize(ret);
	return ret;
}

void write_multiline_comment(std::ostream& out, std::string_view comment, std::string_view indent) {
	while (!comment.empty()) {
		trim(comment);
		auto br = comment.find_first_of('\n');
		out << indent << "// " << comment.substr(0, br) << '\n';
		if (br == std::string_view::npos)
			break;
		comment.remove_prefix(br);
	}
}

// TODO
std::string transform_url_to_function_signature(std::string_view url) {
	std::string result;
	result.reserve(url.size());
	// Count number of parameters by counting brace pairs and single colons.
	int num_params = 0;
	std::vector<std::string> parameters;
	while (!url.empty()) {
		auto brace = url.find_first_of('{');
		if (brace != std::string_view::npos) {
		}
		auto colon = url.find_first_of(':');
	}
	return result;
}

void decompose_http_query(std::string_view raw, std::function<void(std::string_view, std::string_view)>&& kv_cb) {
	do {
		const size_t q_split = raw.find_first_of('&');
		auto kv = raw.substr(0, q_split);
		const size_t kv_split = kv.find_first_of('=');
		kv_cb(kv.substr(0, kv_split), kv.substr(kv_split + 1, std::string_view::npos));
		raw.remove_prefix(q_split == std::string_view::npos ? raw.size() : (q_split + 1));
	} while (!raw.empty());
}
