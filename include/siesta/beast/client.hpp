// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <boost/asio/compose.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/json.hpp>
#include <boost/outcome/std_outcome.hpp>
#include <functional>
#include <memory>

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

	::boost::asio::io_context& context() { return _ctx; }

	void stop() {
		_resolver.cancel();
		_stream.close();
	}

protected:
	Config _conf;
	::boost::asio::io_context& _ctx;
	::boost::asio::strand<::boost::asio::io_context::executor_type> _strand;
	protocol::resolver _resolver;
	::boost::beast::flat_buffer _buffer;
	::boost::beast::tcp_stream _stream;
	request_type _request;
	response_type _response;

	std::string _host_value;

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

	template <::boost::asio::completion_token_for<void(outcome_type)> CompletionToken>
	auto async_submit_request(request_type req, CompletionToken&& token) {
		_request = std::move(req);
		_request.set(::boost::beast::http::field::host, _host_value);
		return ::boost::asio::async_compose<CompletionToken, void(outcome_type)>(
			[this, lifetime = shared_from_this(), state = 0](
				auto& self, ::boost::system::error_code error = {}, std::size_t bytes = 0) mutable -> void {
				namespace http = ::boost::beast::http;
				if (error) {
					self.complete(std::basic_outcome_failure_exception_from_error(error));
					return;
				}
				switch (state) {
				case 0: { // send
					state = 1;
					_stream.expires_after(_conf.write_timeout);
					http::async_write(_stream, _request, std::move(self));
					return;
				}
				case 1: { // recv
					_response = {};
					state = 2;
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
				state = 0;
			},
			token);
	}
};

} // namespace siesta::beast
