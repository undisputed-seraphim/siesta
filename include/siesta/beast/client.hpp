#pragma once

#include <boost/asio/async_result.hpp>
#include <boost/asio/compose.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/json.hpp>
#include <boost/outcome/std_outcome.hpp>
#include <functional>
#include <memory>

#include <siesta/asio/queue.hpp>
#include <siesta/beast/error.hpp>
#include <siesta/format.hpp>

namespace siesta::beast {

class ClientBase : public std::enable_shared_from_this<ClientBase> {
public:
	using request_type = ::boost::beast::http::request<::boost::beast::http::string_body>;
	using response_type = ::boost::beast::http::response<::boost::beast::http::string_body>;
	using protocol = ::boost::asio::ip::tcp;
	using outcome_type = ::boost::outcome_v2::std_outcome<response_type>;
	using error_type = ::boost::system::error_code;

	struct Config {
		std::chrono::milliseconds connect_timeout;
		std::chrono::milliseconds write_timeout;
		std::chrono::milliseconds read_timeout;

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

	void stop() {
		_resolver.cancel();
		_stream.close();
	}

protected:
	Config _conf;
	::boost::asio::strand<::boost::asio::io_context::executor_type> _strand;
	protocol::resolver _resolver;
	::boost::beast::flat_buffer _buffer;
	::boost::beast::tcp_stream _stream;
	request_type _request;
	response_type _response;

	std::array<unsigned char, 1024 + 256 + 128> _json_buffer;

	void on_resolve(const error_type&, protocol::resolver::results_type);
	void on_connect(const error_type&, protocol::resolver::endpoint_type);

	template <typename T>
		requires ::boost::json::has_value_to<T>::value
	void extract_object(response_type& resp, T& t) {
		namespace json = ::boost::json;
		json::monotonic_resource json_rsc(_json_buffer.data(), _json_buffer.size());
		t = T(json::value_to<T>(json::parse(resp.body(), &json_rsc)));
	}

	enum { send, recv, done } _state;

	template <::boost::asio::completion_token_for<void(outcome_type)> CompletionToken>
	auto async_submit_request(request_type req, CompletionToken&& token) {
		return ::boost::asio::async_compose<CompletionToken, void(outcome_type)>(
			[this, lifetime = shared_from_this(), req = std::forward<request_type>(req)](
				auto& self, ::boost::system::error_code error = {}, std::size_t bytes = 0) -> void {
				namespace http = ::boost::beast::http;
				if (error) {
					self.complete(std::basic_outcome_failure_exception_from_error(error));
					return;
				}
				switch (_state) {
				case send: {
					_state = recv;
					_stream.expires_after(_conf.write_timeout);
					http::async_write(_stream, req, std::move(self));
					return;
				}
				case recv: {
					_response = {};
					_state = done;
					_stream.expires_after(_conf.read_timeout);
					http::async_read(_stream, _buffer, _response, std::move(self));
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
				_state = send;
			},
			token);
	}

	template <::boost::asio::completion_token_for<void(outcome_type)> CompletionToken>
	auto async_result(CompletionToken&& token) {
		namespace asio = ::boost::asio;
		namespace http = ::boost::beast::http;
		return http::async_read(
			_stream,
			_buffer,
			_response,
			[self = shared_from_this(),
			 token = std::forward<CompletionToken>(token)](error_type error, std::size_t bytes) mutable {
				if (error) {
					asio::dispatch(std::bind(token, std::basic_outcome_failure_exception_from_error(error)));
				}
				const auto http_status_code = self->_response.result();
				if (http::to_status_class(http_status_code) != http::status_class::successful) {
					asio::dispatch(std::bind(token, std::make_error_code(http_status_code)));
				}
				asio::dispatch(token, std::move(self->_response));
			});
	}

	template <::boost::asio::completion_token_for<void(outcome_type)> CompletionToken>
	auto async_request(CompletionToken&& token) {
		namespace asio = ::boost::asio;
		return ::boost::beast::http::async_write(
			_stream,
			_request,
			[self = shared_from_this(),
			 token = std::forward<CompletionToken>(token)](error_type error, std::size_t bytes) mutable {
				if (error) {
					asio::dispatch(std::bind(token, std::basic_outcome_failure_exception_from_error(error)));
				}
				auto initiate = [self =
									 std::move(self)](asio::completion_handler_for<void(outcome_type)> auto&& handler) {
					self->async_result<CompletionToken>(handler);
				};
				return asio::async_initiate<CompletionToken, void(outcome_type)>(initiate, token);
			});
	}
};

} // namespace siesta::beast