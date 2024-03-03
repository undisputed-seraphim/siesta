// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <optional>

using namespace std::literals;

namespace siesta {

template <typename MappedType, typename CharT, typename Alloc = std::allocator<CharT>>
class basic_node {
public:
	using char_type = CharT;
	using key_type = std::basic_string<char_type, std::char_traits<char_type>, Alloc>;
	using key_view_type = std::basic_string_view<char_type, std::char_traits<char_type>>;
	using mapped_type = typename std::decay_t<MappedType>;
	using value_type = std::pair<key_type, mapped_type>;
	using value_view_type = std::pair<key_view_type, std::reference_wrapper<const mapped_type>>;

	static constexpr auto npos = key_type::npos;
	static constexpr key_view_type wildcard = key_view_type("*");
	using wildcard_callback_t = std::function<void(key_view_type)>;
	using opt_mapped_ref_t = std::optional<std::reference_wrapper<mapped_type>>;
	using opt_mapped_cref_t = std::optional<std::reference_wrapper<const mapped_type>>;

	basic_node()
		: basic_node(std::string()) {}
	explicit basic_node(key_type&& key)
		: _key(std::move(key)), _value(std::nullopt) {}
	basic_node(key_view_type key)
		: _key(key_type(key)), _value(std::nullopt) {}
	basic_node(std::initializer_list<value_type>);
	~basic_node() noexcept = default;

	opt_mapped_ref_t at(key_view_type path) {
		const size_t pos = path.find_first_of('/');
		if (_key == path.substr(0, pos)) {
			if (pos == npos) {
				return _value.has_value() ? std::make_optional(std::ref(_value.value())) : std::nullopt;
			}
			path.remove_prefix(pos + 1);
			for (const auto& child : _children) {
				if (auto opt = child->at(path); opt.has_value()) {
					return opt;
				}
			}
		}
		return std::nullopt;
	}

	opt_mapped_cref_t const_at(key_view_type path) const {
		const size_t pos = path.find_first_of('/');
		if (_key == path.substr(0, pos)) {
			if (pos == npos) {
				return _value.has_value() ? std::make_optional(std::cref(_value.value())) : std::nullopt;
			}
			path.remove_prefix(pos + 1);
			for (const auto& child : _children) {
				if (auto opt = child->at(path); opt.has_value()) {
					return opt;
				}
			}
		}
		return std::nullopt;
	}

	opt_mapped_ref_t at_wildcard(key_view_type path, wildcard_callback_t callback) {
		const size_t pos = path.find_first_of('/');
		const key_view_type token = path.substr(0, pos);
		if (_key == token || _key == wildcard) {
			callback(token);
			if (pos == npos) {
				return _value.has_value() ? std::make_optional(std::ref(_value.value())) : std::nullopt;
			}
			path.remove_prefix(pos + 1);
			for (const auto& child : _children) {
				if (auto opt = child->at(path); opt.has_value()) {
					return opt;
				}
			}
		}
		return std::nullopt;
	}

	// Add or update a path, optionally setting a value for it.
	// Return value should be ignored, it is meaningful internally only
	bool insert(key_view_type path, std::optional<mapped_type> val = std::nullopt) {
		size_t pos = path.find_first_of('/');
		if (_key == path.substr(0, pos)) {
			if (pos == npos) {
				_value = std::move(val);
				return true;
			}
			path.remove_prefix(pos + 1);
			bool inserted = std::any_of(
				_children.begin(), _children.end(), [path, val](std::unique_ptr<basic_node>& n) { return n->insert(path, val); });
			if (inserted) {
				return true;
			}
			pos = path.find_first_of('/');
			return _children.emplace_back(std::make_unique<basic_node>(path.substr(0, pos)))->insert(path, std::move(val));
		}
		return false;
	}

	// Check if a path exists, regardless of content. Exact matches only.
	// Any intermediate path that matches the query will return true.
	bool contains(key_view_type path) const {
		const size_t pos = path.find_first_of('/');
		const key_view_type token = path.substr(0, pos);
		if (_key == path.substr(0, pos) || _key == wildcard) {
			if (pos == npos) {
				return true;
			}
			path.remove_prefix(pos + 1);
			return std::any_of(_children.begin(), _children.end(), [path](const std::unique_ptr<basic_node>& n) {
				return n->contains(path);
			});
		}
		return false;
	}

	// Check if a path exists, and that path contains a mapped object.
	bool contains(key_view_type path, const mapped_type& val) const {
		auto res = const_at(path);
		return res.has_value() ? (res.value().get() == val) : false;
	}

	bool wildcard_contains(key_view_type path, const mapped_type& val) const {
		auto res = at_wildcard(path);
		return res.has_value() ? (res.value().get() == val) : false;
	}

	// Counts the total number of leaf nodes only.
	// There is always at least 1 (self).
	size_t size() const noexcept {
		size_t ret = 0;
		// std::cout << "this: " << _key << std::endl;
		for (const auto& child : _children) {
			ret += child->size();
		}
		return (ret == 0) ? 1 : ret;
	}

	void sort() {
		std::sort(_children.begin(), _children.end());
		for (auto& child : _children) {
			child.sort();
		}
	}

	const key_type& key_token() const noexcept { return _key; }

	bool operator<(const basic_node& other) const noexcept {
		return std::lexicographical_compare(_key.begin(), _key.end(), other._key.begin(), other._key.end());
	}

private:
	key_type _key;
	std::optional<mapped_type> _value;
	std::vector<std::unique_ptr<basic_node>> _children;
};

template <typename MappedType, typename CharT, typename Alloc>
basic_node<MappedType, CharT, Alloc>::basic_node(std::initializer_list<value_type> init) : _key("") {
	for (auto&& [key, value] : init) {
		this->insert(std::move(key), std::move(value));
	}
}

template <typename MappedType>
using node = basic_node<MappedType, char>;

template <typename MappedType>
using wnode = basic_node<MappedType, wchar_t>;

} // namespace siesta