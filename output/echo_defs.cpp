// Automatically generated from ../tests/echo.json. Do not modify this file.
#include "echo_defs.hpp"
namespace js = ::boost::json;

namespace swagger {

void tag_invoke(js::value_from_tag, js::value& jv, const EchoResponse& v) {
	jv = {
		{ "message", v.message },
	};
}

EchoResponse tag_invoke(js::value_to_tag<EchoResponse>, const js::value& jv) {
	const auto& obj = jv.as_object();
	EchoResponse ret;
	ret.message = js::value_to<std::string>(obj.at("message"));
	return ret;
}

void tag_invoke(js::value_from_tag, js::value& jv, const Error& v) {
	jv = {
		{ "code", v.code },
		{ "message", v.message },
		{ "fields", v.fields },
	};
}

Error tag_invoke(js::value_to_tag<Error>, const js::value& jv) {
	const auto& obj = jv.as_object();
	Error ret;
	ret.code = js::value_to<int32_t>(obj.at("code"));
	ret.message = js::value_to<std::string>(obj.at("message"));
	ret.fields = js::value_to<std::string>(obj.at("fields"));
	return ret;
}

} // namespace swagger
