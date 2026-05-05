// SPDX-License-Identifier: Apache-2.0
#include <boost/beast/http/write.hpp>
#include <iostream>
#include <siesta/beast/server.hpp>

namespace asio = ::boost::asio;
namespace http = ::boost::beast::http;

namespace siesta::beast {

static void fail(std::string_view facility, ::boost::system::error_code ec) {
	std::cerr << facility << ": " << ec.to_string() << ' ' << ec.message() << std::endl;
}

ServerBase::ServerBase(asio::io_context& ctx)
	: _ctx(&ctx)
	, _acceptor(asio::make_strand(ctx)) {}

ServerBase::ServerBase(asio::io_context& ctx, Config config)
	: _conf(std::move(config))
	, _ctx(&ctx)
	, _acceptor(asio::make_strand(ctx)) {}

void ServerBase::start(const asio::ip::address address, uint16_t port) {
	start(protocol::endpoint(address, port));
}

void ServerBase::start(const protocol::endpoint& endpoint) {
	auto ec = ec_t{};
	_acceptor.open(endpoint.protocol(), ec);
	if (ec) {
		return fail("acceptor::open", ec);
	}
	_acceptor.set_option(protocol::socket::reuse_address(true), ec);
	_acceptor.bind(endpoint, ec);
	if (ec) {
		return fail("acceptor::bind", ec);
	}
	_acceptor.listen(64, ec);
	if (ec) {
		return fail("acceptor::listen", ec);
	}
	_acceptor.async_accept(asio::make_strand(*_ctx), [this](const ec_t& ec, protocol::socket socket) {
		on_accept(ec, std::move(socket));
	});
}

void ServerBase::on_accept(const ec_t& ec, protocol::socket socket) {
	if (ec) {
		return fail("on_accept", ec);
	}
	std::make_shared<Session>(*this, std::move(socket), _conf, _client_id++)->run();
	_acceptor.async_accept(asio::make_strand(*_ctx), [this](const ec_t& ec, protocol::socket socket) {
		on_accept(ec, std::move(socket));
	});
}

// Session

ServerBase::Session::Session(ServerBase& parent, protocol::socket socket, Config config, uint64_t id)
	: _parent(parent)
	, _stream(std::move(socket))
	, _config(std::move(config))
	, _id(id) {}

ServerBase::Session::~Session() noexcept { do_close(); }

void ServerBase::Session::run() {
	asio::post(_stream.get_executor(), [self = shared_from_this()] {
		self->do_read();
	});
}

void ServerBase::Session::write() {
	_response.prepare_payload();
	_stream.expires_after(_config.write_timeout);
	http::async_write(_stream, _response, [self = shared_from_this()](ec_t ec, std::size_t bytes) {
		self->on_write(ec, bytes);
	});
}

void ServerBase::Session::do_read() {
	_request = {};
	_stream.expires_after(_config.read_timeout);
	http::async_read(_stream, _buffer, _request, [self = shared_from_this()](ec_t ec, std::size_t bytes) {
		self->on_read(ec, bytes);
	});
}

void ServerBase::Session::on_read(ec_t ec, std::size_t bytes) {
	if (ec == http::error::end_of_stream) {
		return do_close();
	}
	if (ec) {
		return fail("on_read", ec);
	}
	_parent.handle_request(std::move(_request), shared_from_this());
}

void ServerBase::Session::on_write(ec_t ec, std::size_t bytes) {
	if (ec) {
		return fail("on_write", ec);
	}
	do_read();
}

void ServerBase::Session::do_close() {
	ec_t ec;
	_stream.close();
}

namespace __detail {
std::size_t MapHash::operator()(const std::pair<std::string_view, boost::beast::http::verb>& v) const {
	const std::size_t _1 = std::hash<std::string_view>{}(v.first);
	const std::size_t _2 = std::hash<boost::beast::http::verb>{}(v.second);
	return _1 ^ (_2 + 0x9e3779b9 + (_1 << 6) + (_1 >> 2));
}
} // namespace __detail

} // namespace siesta::beast
