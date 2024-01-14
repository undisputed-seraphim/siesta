#pragma once

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class tokenized_string {
public:
	using string_view_type = std::string_view;
	using char_type = string_view_type::value_type;
	using size_type = string_view_type::size_type;

	tokenized_string(std::string_view sv, char_type sep)
		: _str(std::string(sv))
		, _sep(sep) {
		size_type pos = 0;
		_token_pos.clear();
		do {
			pos = sv.find_first_of(sep);
			if (pos == std::string_view::npos) {
				break;
			}
			_token_pos.push_back((_token_pos.empty() ? 0 : _token_pos.back()) + pos + 1);
			sv.remove_prefix(pos + 1);
		} while (!sv.empty());
	}

	bool match(std::string_view sv) {
		auto iter = _token_pos.begin();
		size_type pos = 0;
		do {
			pos = sv.find_first_of(_sep);
			if (pos == std::string_view::npos) {
				break;
			}
			uint16_t prev = (iter == _token_pos.begin()) ? 0 : *std::prev(iter);
			if (prev + (pos + 1) != (*iter)) {
				return false;
			}
			iter = std::next(iter);
			sv.remove_prefix(pos + 1);
		} while (!sv.empty());
		return true;
	}

private:
	std::string _str;
	std::vector<uint16_t> _token_pos;
	char_type _sep;
};
