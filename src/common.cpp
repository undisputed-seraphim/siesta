// SPDX-License-Identifier: Apache-2.0
#include <siesta/common.hpp>
#include <ctime>

namespace siesta {

std::string rfc7231_date() {
	char buf[30];
	std::time_t t = std::time(nullptr);
	std::strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", std::gmtime(&t));
	return buf;
}

} // namespace siesta
