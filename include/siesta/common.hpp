// SPDX-License-Identifier: Apache-2.0
#pragma once
/// Types and functions shared across all siesta transport backends.

#include <boost/json.hpp>
#include <boost/system/error_code.hpp>
#include <chrono>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>

namespace siesta {

struct RetryConfig {
	int max_attempts = 1;
	std::chrono::milliseconds initial_backoff{100};
	std::chrono::milliseconds max_backoff{5000};
	float backoff_multiplier = 2.0f;

	bool enabled() const { return max_attempts > 1; }
};

// ── Structured error model (gRPC-compatible codes) ─────────────

enum class ErrorCode : int {
	OK                  = 0,
	CANCELLED           = 1,
	UNKNOWN             = 2,
	INVALID_ARGUMENT    = 3,
	DEADLINE_EXCEEDED   = 4,
	NOT_FOUND           = 5,
	ALREADY_EXISTS      = 6,
	PERMISSION_DENIED   = 7,
	RESOURCE_EXHAUSTED  = 8,
	FAILED_PRECONDITION = 9,
	ABORTED             = 10,
	OUT_OF_RANGE        = 11,
	UNIMPLEMENTED       = 12,
	INTERNAL            = 13,
	UNAVAILABLE         = 14,
	DATA_LOSS           = 15,
	UNAUTHENTICATED     = 16,
};

struct Error {
	ErrorCode code = ErrorCode::UNKNOWN;
	std::string message;
};

inline const Error OK{ErrorCode::OK};

inline int http_status_for(ErrorCode code) {
	switch (code) {
	case ErrorCode::OK:                  return 200;
	case ErrorCode::CANCELLED:           return 499;
	case ErrorCode::UNKNOWN:             return 500;
	case ErrorCode::INVALID_ARGUMENT:    return 400;
	case ErrorCode::DEADLINE_EXCEEDED:   return 504;
	case ErrorCode::NOT_FOUND:           return 404;
	case ErrorCode::ALREADY_EXISTS:      return 409;
	case ErrorCode::PERMISSION_DENIED:   return 403;
	case ErrorCode::RESOURCE_EXHAUSTED:  return 429;
	case ErrorCode::FAILED_PRECONDITION: return 400;
	case ErrorCode::ABORTED:             return 409;
	case ErrorCode::OUT_OF_RANGE:        return 400;
	case ErrorCode::UNIMPLEMENTED:       return 501;
	case ErrorCode::INTERNAL:            return 500;
	case ErrorCode::UNAVAILABLE:         return 503;
	case ErrorCode::DATA_LOSS:           return 500;
	case ErrorCode::UNAUTHENTICATED:     return 401;
	default:                                return 500;
	}
}

inline std::string serialize_error(const Error& e) {
	boost::json::object obj;
	obj["code"]    = static_cast<int>(e.code);
	obj["message"] = e.message;
	return boost::json::serialize(obj);
}

inline std::optional<Error> parse_error(std::string_view body) {
	try {
		auto jv = boost::json::parse(body);
		auto& obj = jv.as_object();
		Error e;
		e.code    = static_cast<ErrorCode>(obj.at("code").as_int64());
		e.message = std::string(boost::json::value_to<std::string>(obj.at("message")));
		return e;
	} catch (...) {
		return std::nullopt;
	}
}

inline bool is_transient(const ::boost::system::error_code& ec) {
	if (std::strcmp(ec.category().name(), "beast.http") == 0) {
		return ec.value() >= 500 && ec.value() < 600;
	}
	return true;
}

std::string rfc7231_date();

} // namespace siesta
