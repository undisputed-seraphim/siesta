#pragma once
#include <boost/asio.hpp>
#include <boost/asio/associated_executor.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/json.hpp>
#include <fmt/format.h>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "echo_defs.hpp"
#include <siesta/beast/client.hpp>

namespace swagger {

class Client : public ::siesta::beast::ClientBase {
public:
	using ::siesta::beast::ClientBase::ClientBase;
	using ::siesta::beast::ClientBase::Config;
	using ::siesta::beast::ClientBase::shared_from_this;

	// Returns the 'message' to the caller
	auto echo(
		std::string headerParam,
		std::string message,
		::boost::asio::completion_token_for<void(outcome_type)> auto&& token) {
		constexpr std::string_view path = "/v1/echo?message={}";
		::boost::json::monotonic_resource json_rsc(_json_buffer.data(), _json_buffer.size());
		request_type req;
		req.target(fmt::format(path, message));
		req.method(::boost::beast::http::verb::get);
		req.set("headerParam", headerParam);
		return this->async_submit_request(std::move(req), token);
		// 200	 EchoResponse
		// default	 Error
	}

}; // class
} // namespace swagger
