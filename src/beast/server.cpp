#include <boost/beast/http/write.hpp>
#include <iostream>
#include <siesta/beast/server.hpp>

namespace asio = ::boost::asio;
namespace http = ::boost::beast::http;

namespace siesta::beast {

void fail(std::string_view facility, ::boost::system::error_code ec) {
	std::cerr << facility << ": " << ec.to_string() << ' ' << ec.message() << std::endl;
}

ServerBase::ServerBase(asio::io_context& ctx, Config config)
	: _conf(std::move(config))
	, _acceptor(asio::make_strand(ctx)) {}

void ServerBase::start(asio::io_context& ctx, const asio::ip::address address, uint16_t port) {
	start(ctx, protocol::endpoint(address, port));
}

void ServerBase::start(asio::io_context& ctx, const protocol::endpoint& endpoint) {
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
	_acceptor.async_accept(asio::make_strand(ctx), [this, &ctx](const ec_t& ec, protocol::socket socket) {
		on_accept(ctx, ec, std::move(socket));
	});
}

void ServerBase::on_accept(asio::io_context& ctx, const ec_t& ec, protocol::socket socket) {
	if (ec) {
		return fail("on_accept", ec);
	}
	std::make_shared<Session>(*this, std::move(socket), _conf, _client_id++)->run();
	_acceptor.async_accept(asio::make_strand(ctx), [this, &ctx](const ec_t& ec, protocol::socket socket) {
		on_accept(ctx, ec, std::move(socket));
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
	asio::dispatch(_stream.get_executor(), std::bind_front(&Session::do_read, shared_from_this()));
}

void ServerBase::Session::write() {
	_response.prepare_payload();
	http::async_write(_stream, _response, std::bind_front(&ServerBase::Session::on_write, shared_from_this()));
}

void ServerBase::Session::do_read() {
	_request = {};
	_stream.expires_after(std::chrono::hours{1});
	http::async_read(
		_stream, _buffer, _request, std::bind_front(&Session::on_read, shared_from_this()));
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
	_stream.socket().shutdown(protocol::socket::shutdown_send, ec);
}

namespace __detail {
std::size_t MapHash::operator()(const std::pair<std::string_view, boost::beast::http::verb>& v) const {
	const std::size_t _1 = std::hash<std::string_view>{}(v.first);
	const std::size_t _2 = std::hash<boost::beast::http::verb>{}(v.second);
	return _1 ^ (_2 << _1);
}
} // namespace __detail

} // namespace siesta::beast