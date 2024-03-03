// SPDX-License-Identifier: Apache-2.0
#include <boost/asio/connect.hpp>
#include <boost/beast/http/error.hpp>
#include <iostream>

#include <siesta/beast/client.hpp>

namespace siesta::beast {

void fail(std::string_view facility, ::boost::system::error_code ec) {
	std::cerr << facility << ": " << ec.to_string() << ' ' << ec.message() << std::endl;
}

ClientBase::ClientBase(::boost::asio::io_context& ctx, Config config)
	: _conf(std::move(config))
	, _strand(::boost::asio::make_strand(ctx))
	, _resolver(_strand)
	, _stream(_strand) {}

void ClientBase::start(const ::boost::asio::ip::address& address, uint16_t port) { start(protocol::endpoint(address, port)); }
void ClientBase::start(const protocol::endpoint& endpoint) {
	_resolver.async_resolve(endpoint, std::bind_front(&ClientBase::on_resolve, shared_from_this()));
}

void ClientBase::on_resolve(const error_type& ec, protocol::resolver::results_type results) {
	if (ec) {
		return fail("on_resolve", ec);
	}
	_stream.expires_after(_conf.connect_timeout);
	_stream.async_connect(results, std::bind_front(&ClientBase::on_connect, shared_from_this()));
}

void ClientBase::on_connect(const error_type& ec, protocol::resolver::endpoint_type endpoint) {
	if (ec) {
		return fail("on_connect", ec);
	}
}

} // namespace siesta::beast