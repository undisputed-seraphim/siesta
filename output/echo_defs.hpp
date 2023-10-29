// Automatically generated from ../tests/echo.json. Do not modify this file.
#pragma once

#include <any>
#include <string>
#include <vector>
#include <boost/json.hpp>

using namespace std::literals;

namespace swagger {

struct EchoResponse {
	std::string message;
};

void tag_invoke(boost::json::value_from_tag, boost::json::value& jv, const EchoResponse& v);

EchoResponse tag_invoke(boost::json::value_to_tag<EchoResponse>, const boost::json::value& jv);

struct Error {
	int32_t code;
	std::string message;
	std::string fields;
};

void tag_invoke(boost::json::value_from_tag, boost::json::value& jv, const Error& v);

Error tag_invoke(boost::json::value_to_tag<Error>, const boost::json::value& jv);

// Success
using get__echo_200_body = EchoResponse;
// Error
using get__echo_default_body = Error;
} // namespace swagger
