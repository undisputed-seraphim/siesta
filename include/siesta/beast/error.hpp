#pragma once

#include <boost/beast/http/status.hpp>
#include <iostream>
#include <system_error>

namespace std {

template <>
struct is_error_code_enum<::boost::beast::http::status> : public true_type {};

error_code make_error_code(::boost::beast::http::status status);
error_condition make_error_condition(::boost::beast::http::status status);

} // namespace std
