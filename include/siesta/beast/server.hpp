#pragma once

#include <array>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/system_timer.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <functional>

namespace siesta {

class ServerBase {
public:
	using request = ::boost::beast::http::request<::boost::beast::http::string_body>;
	using response = ::boost::beast::http::response<::boost::beast::http::string_body>;
	using protocol = ::boost::asio::ip::tcp;
	using ec_t = ::boost::system::error_code;

	ServerBase(boost::asio::io_context& ctx)
		: _strand(ctx.get_executor())
		, _acceptor(ctx)
		, _on_error(&ServerBase::_default_on_error) {}

	void start(boost::asio::io_context& ctx, const protocol::endpoint& endpoint) {
		auto ec = ec_t{};
		_acceptor.open(endpoint.protocol(), ec);
		if (ec) {
			_on_error(ec, "acceptor::open");
			return;
		}
		_acceptor.set_option(protocol::socket::reuse_address(true), ec);
		_acceptor.bind(endpoint, ec);
		if (ec) {
			_on_error(ec, "acceptor::bind");
			return;
		}
		_acceptor.listen(64, ec);
		if (ec) {
			_on_error(ec, "acceptor::listen");
			return;
		}
		_acceptor.async_accept(ctx, std::bind_front(&std::remove_reference_t<decltype(*this)>::_on_accept, this));
	}

protected:
	boost::asio::strand<boost::asio::system_timer::executor_type> _strand;
	protocol::acceptor _acceptor;
	std::function<void(const ec_t&, std::string_view)> _on_error;

	void _on_accept(const ec_t& ec, protocol::socket socket) { ::boost::asio::post(_strand, ) }

	inline static void _default_on_error(const ec_t&, std::string_view str) {
		// no-op
		fprintf(stderr, "%s\n", str.data());
	}
};

} // namespace siesta