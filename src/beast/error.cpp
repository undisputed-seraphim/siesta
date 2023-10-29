#include <boost/beast/http/status.hpp>
#include <siesta/beast/error.hpp>
#include <system_error>

class http_category final : public std::error_category {
public:
	const char* name() const noexcept override { return "HTTP status codes"; }
	std::string message(int ev) const override {
		return std::string(::boost::beast::http::obsolete_reason(::boost::beast::http::int_to_status(ev)));
	}
};

const std::error_category& http_status_category() {
	static const http_category instance;
	return instance;
}

namespace std {

error_code make_error_code(::boost::beast::http::status status) {
	return error_code(static_cast<int>(status), http_status_category());
}

error_condition make_error_condition(::boost::beast::http::status status) {
	return error_condition(static_cast<int>(status), http_status_category());
}

} // namespace std