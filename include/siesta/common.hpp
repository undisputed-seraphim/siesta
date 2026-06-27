// SPDX-License-Identifier: Apache-2.0
#pragma once
/// Types and functions shared across all siesta transport backends.

#include <boost/system/error_code.hpp>
#include <chrono>
#include <cstring>
#include <string>

namespace siesta {

struct RetryConfig {
	int max_attempts = 1;
	std::chrono::milliseconds initial_backoff{100};
	std::chrono::milliseconds max_backoff{5000};
	float backoff_multiplier = 2.0f;

	bool enabled() const { return max_attempts > 1; }
};

inline bool is_transient(const ::boost::system::error_code& ec) {
	if (std::strcmp(ec.category().name(), "beast.http") == 0) {
		return ec.value() >= 500 && ec.value() < 600;
	}
	return true;
}

std::string rfc7231_date();

} // namespace siesta
