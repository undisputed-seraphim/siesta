#pragma once

#include <charconv>
#include <functional>
#include <string_view>
#include <type_traits>

void ltrim(std::string_view& s);

void rtrim(std::string_view& s);

void trim(std::string_view& s);

bool compare_ignore_case(std::string_view l, std::string_view r) noexcept;

// Modifies a string to produce words that can be used as variable names in C++.
// This includes replacing hyphens with underscores, editing names that begin with a number,
// and modifying names that are C++ keywords.
void sanitize(std::string& input);
std::string sanitize(std::string_view input);

void write_multiline_comment(std::ostream& out, std::string_view comment, std::string_view indent = "");

std::string transform_url_to_function_signature(std::string_view);

void decompose_http_query(std::string_view raw, std::function<void(std::string_view, std::string_view)>&& kv_cb);
