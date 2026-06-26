// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <boost/asio/compose.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/json.hpp>
#include <boost/json/monotonic_resource.hpp>
#include <boost/outcome/std_outcome.hpp>
#include <cstring>
#include <functional>
#include <memory>
#include <variant>

#include <siesta/beast/error.hpp>
#include <siesta/format.hpp>

namespace siesta::beast {

struct RetryConfig {
	int max_attempts = 1;                         // 1 = single attempt, no retry
	std::chrono::milliseconds initial_backoff{100};
	std::chrono::milliseconds max_backoff{5000};
	float backoff_multiplier = 2.0f;

	bool enabled() const { return max_attempts > 1; }
};

class ClientBase : public std::enable_shared_from_this<ClientBase> {
public:
	using request_type = ::boost::beast::http::request<::boost::beast::http::string_body>;
	using response_type = ::boost::beast::http::response<::boost::beast::http::string_body>;
	using protocol = ::boost::asio::ip::tcp;
	using outcome_type = ::boost::outcome_v2::std_outcome<response_type>;
	using error_type = ::boost::system::error_code;
	using strand_type = ::boost::asio::strand<::boost::asio::io_context::executor_type>;
	using stream_type = ::boost::beast::basic_stream<protocol, strand_type>;
	using ssl_stream_type = ::boost::beast::ssl_stream<stream_type>;
	using any_stream = std::variant<stream_type, ssl_stream_type>;

	struct Config {
		std::chrono::milliseconds connect_timeout;
		std::chrono::milliseconds write_timeout;
		std::chrono::milliseconds read_timeout;
		::boost::asio::ssl::context* ssl_ctx = nullptr;

		Config()
			: connect_timeout(1000)
			, write_timeout(1000)
			, read_timeout(1000) {}
	};

	ClientBase(::boost::asio::io_context&, Config = Config());
	ClientBase(const ClientBase&) = delete;
	ClientBase(ClientBase&&) = default;
	virtual ~ClientBase() { stop(); }

	void start(const ::boost::asio::ip::address&, uint16_t);
	void start(const protocol::endpoint&);

	::boost::asio::io_context& context() { return _ctx; }
	bool is_tls() const { return std::holds_alternative<ssl_stream_type>(_stream); }

	// Per-request JSON arena — shared between request serialization
	// and response parsing. Reset at start of async_submit_request.
	// Do not hold references across async boundaries.
	::boost::json::storage_ptr json_storage() {
		return _json_pool_;
	}

	void stop() {
		_resolver.cancel();
		if (auto* ssl = std::get_if<ssl_stream_type>(&_stream)) {
			error_type ec;
			ssl->shutdown(ec);
		}
		tcp_layer().close();
	}

	void set_retry(RetryConfig r) { _retry = std::move(r); }
	const RetryConfig& retry() const { return _retry; }

protected:
	Config _conf;
	::boost::asio::io_context& _ctx;
	::boost::asio::strand<::boost::asio::io_context::executor_type> _strand;
	protocol::resolver _resolver;
	::boost::beast::flat_buffer _buffer;
	any_stream _stream;
	request_type _request;
	response_type _response;

	std::string _host_value;
	RetryConfig _retry;

	::boost::json::storage_ptr _json_pool_{
		::boost::json::make_shared_resource<::boost::json::monotonic_resource>(4096)};

	static stream_type& tcp_of(stream_type& s) { return s; }
	static stream_type& tcp_of(ssl_stream_type& s) { return s.next_layer(); }

	stream_type& tcp_layer() {
		return std::visit([](auto& s) -> stream_type& { return tcp_of(s); }, _stream);
	}

	template <typename F>
	decltype(auto) with_stream(F&& f) {
		return std::visit(std::forward<F>(f), _stream);
	}

	void on_resolve(const error_type&, protocol::resolver::results_type);
	void on_connect(const error_type&, protocol::resolver::endpoint_type);

	template <typename T>
		requires ::boost::json::has_value_to<T>::value
	void extract_object(response_type& resp, T& t) {
		t = T(::boost::json::value_to<T>(
			::boost::json::parse(resp.body(), _json_pool_)));
	}

	template <typename Stream, ::boost::asio::completion_token_for<void(outcome_type)> CompletionToken>
	auto do_submit(Stream& stream, CompletionToken&& token) {
		return ::boost::asio::async_compose<CompletionToken, void(outcome_type)>(
			[this, &stream, lifetime = shared_from_this(), state = 0](
				auto& self, ::boost::system::error_code error = {}, std::size_t bytes = 0) mutable -> void {
				namespace http = ::boost::beast::http;
				if (error) {
					self.complete(error);
					return;
				}
				switch (state) {
				case 0: {
					state = 1;
					if (_conf.write_timeout > std::chrono::milliseconds::zero())
						tcp_of(stream).expires_after(_conf.write_timeout);
					http::async_write(stream, _request, std::move(self));
					return;
				}
				case 1: {
					_response = {};
					state = 2;
					if (_conf.read_timeout > std::chrono::milliseconds::zero())
						tcp_of(stream).expires_after(_conf.read_timeout);
					http::async_read(stream, _buffer, _response, std::move(self));
					return;
				}
				default:
					break;
				}
				const auto http_status_code = this->_response.result();
				if (http::to_status_class(http_status_code) == http::status_class::successful) {
					self.complete(std::move(this->_response));
				} else {
					self.complete(std::make_error_code(http_status_code));
				}
				state = 0;
			},
			token);
	}

	template <::boost::asio::completion_token_for<void(outcome_type)> CompletionToken>
	auto async_submit_request(request_type req, CompletionToken&& token) {
		_request = std::move(req);
		_request.set(::boost::beast::http::field::host, _host_value);
		_json_pool_ = ::boost::json::make_shared_resource<
			::boost::json::monotonic_resource>(4096);
		return with_stream([&](auto& s) {
			return do_submit(s, std::forward<CompletionToken>(token));
		});
	}
};

/// Returns true if this error is likely transient (caller may retry).
/// Connection errors, timeouts, DNS failures, and HTTP 5xx are transient.
/// HTTP 4xx, other protocol errors, and invalid arguments are fatal.
inline bool is_transient(const ::boost::system::error_code& ec) {
	if (std::strcmp(ec.category().name(), "beast.http") == 0) {
		return ec.value() >= 500 && ec.value() < 600;
	}
	return true;
}

} // namespace siesta::beast
