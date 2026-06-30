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
#include <boost/beast/websocket.hpp>
#include <boost/json.hpp>
#include <boost/json/monotonic_resource.hpp>
#include <boost/outcome/std_outcome.hpp>
#include <cstring>
#include <functional>
#include <memory>
#include <variant>

#include <siesta/beast/error.hpp>
#include <siesta/common.hpp>
#include <siesta/format.hpp>

namespace siesta::beast {

using siesta::RetryConfig;
using siesta::is_transient;

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

	// Structured error from the last non-2xx response, if the server
	// sent a JSON error body using the siesta error model.
	const std::optional<siesta::Error>& last_error() const { return _last_error; }

	// Cancel any in-flight request. Closes the TCP connection, causing
	// pending async_submit_request calls to complete with an error.
	void cancel() {
		tcp_layer().close();
	}

	// WebSocket — creates stream from TCP layer on first call.
	// Must be called after start() and before any WS I/O.
	::boost::beast::websocket::stream<stream_type&>& websocket() {
		if (!_ws) {
			_ws = std::make_unique<::boost::beast::websocket::stream<stream_type&>>(
				tcp_layer());
		}
		return *_ws;
	}

	// Perform client-side WebSocket upgrade handshake.
	// Sends HTTP upgrade request, waits for 101 Switching Protocols.
	template <::boost::asio::completion_token_for<void(outcome_type)> CompletionToken>
	auto ws_upgrade(std::string_view target, CompletionToken&& token) {
		return ::boost::asio::async_compose<CompletionToken, void(outcome_type)>(
			[this, target = std::string(target), state = 0](
				auto& self, ::boost::system::error_code error = {}, std::size_t = 0) mutable -> void {
				if (error) {
					self.complete(error);
					return;
				}
				switch (state) {
				case 0: {
					state = 1;
					_ws->async_handshake(_host_value, target, std::move(self));
					return;
				}
				default: {
					self.complete(::boost::system::error_code{});
					return;
				}
				}
			},
			token);
	}

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
	std::optional<siesta::Error> _last_error;
	std::unique_ptr<::boost::beast::websocket::stream<stream_type&>> _ws;

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
				_last_error.reset();
				self.complete(std::move(this->_response));
			} else {
				_last_error = siesta::parse_error(this->_response.body());
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

} // namespace siesta::beast
