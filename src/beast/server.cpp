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

void ServerBase::shutdown() {
	ec_t ec;
	_shutting_down = true;
	_acceptor.close(ec);
}

void ServerBase::on_accept(const ec_t& ec, protocol::socket socket) {
	if (ec) {
		return fail("on_accept", ec);
	}
	stream_type tcp(asio::make_strand(*_ctx));
	tcp.socket().assign(protocol::v4(), socket.release());

	any_stream stream = _conf.ssl_ctx
		? any_stream{ssl_stream_type(std::move(tcp), *_conf.ssl_ctx)}
		: any_stream{std::move(tcp)};

	std::make_shared<Session>(*this, std::move(stream), _conf, _client_id++)->run();
	_acceptor.async_accept(asio::make_strand(*_ctx), [this](const ec_t& ec, protocol::socket socket) {
		on_accept(ec, std::move(socket));
	});
}

// Session

ServerBase::Session::Session(ServerBase& parent, any_stream stream, Config config, uint64_t id)
	: _parent(parent)
	, _stream(std::move(stream))
	, _config(std::move(config))
	, _id(id) {}

ServerBase::Session::~Session() noexcept { do_close(); }

void ServerBase::Session::run() {
	if (auto* ssl = std::get_if<ssl_stream_type>(&_stream)) {
		tcp_of(*ssl).expires_after(std::chrono::seconds{30});
		ssl->async_handshake(asio::ssl::stream_base::server,
			[self = shared_from_this()](ec_t ec) {
				if (ec) return fail("ssl_handshake", ec);
				self->do_read();
			});
	} else {
		asio::post(tcp_layer().get_executor(), [self = shared_from_this()] {
			self->do_read();
		});
	}
}

std::string ServerBase::Session::rfc7231_date() {
	char buf[30];
	std::time_t t = std::time(nullptr);
	std::strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", std::gmtime(&t));
	return buf;
}

void ServerBase::Session::do_read() {
	if (_parent._shutting_down) {
		do_close();
		return;
	}
	parser_.emplace();
	if (_config.max_body_size > 0)
		parser_->body_limit(_config.max_body_size);
	else
		parser_->body_limit(boost::none);
	with_stream([this](auto& s) {
		auto timeout = _config.idle_timeout > std::chrono::milliseconds::zero()
			? _config.idle_timeout : _config.read_timeout;
		if (timeout > std::chrono::milliseconds::zero())
			tcp_of(s).expires_after(timeout);
		http::async_read(s, _buffer, *parser_, [self = shared_from_this()](ec_t ec, std::size_t bytes) {
			self->on_read(ec, bytes);
		});
	});
}

void ServerBase::Session::on_read(ec_t ec, std::size_t) {
	if (ec == http::error::end_of_stream) {
		should_close_ = true;
		if (!is_writing_) do_close();
		return;
	}
	if (ec == http::error::body_limit) {
		unsigned ver = parser_->get().version();
		http::response<http::string_body> resp{http::status::payload_too_large, ver};
		resp.body() = R"({"error":"request body too large"})";
		resp.set(http::field::content_type, "application/json");
		resp.keep_alive(false);
		resp.prepare_payload();
		send(std::move(resp));
		parser_.reset();
		return;
	}
	if (ec) {
		return fail("on_read", ec);
	}

	auto req = parser_->release();
	parser_.reset();

	if (req.method() == http::verb::options && !_config.cors_origin.empty()) {
		http::response<http::string_body> resp{http::status::no_content, req.version()};
		resp.set(http::field::access_control_allow_methods, _config.cors_methods);
		resp.set(http::field::access_control_allow_headers, _config.cors_headers);
		resp.set(http::field::access_control_max_age, std::to_string(_config.cors_max_age));
		resp.keep_alive(req.keep_alive());
		resp.prepare_payload();
		send(std::move(resp));
		do_read();
		return;
	}

	if (response_queue_.size() >= max_responses_) {
		http::response<http::string_body> resp{http::status::too_many_requests, req.version()};
		resp.body() = R"({"error":"too many requests"})";
		resp.set(http::field::content_type, "application/json");
		resp.keep_alive(false);
		resp.prepare_payload();
		send(std::move(resp));
		return;
	}

	do_read();
	_parent.handle_request(std::move(req), shared_from_this());
}

void ServerBase::Session::do_write() {
	if (response_queue_.empty()) {
		is_writing_ = false;
		if (should_close_ || _parent._shutting_down) do_close();
		return;
	}
	is_writing_ = true;
	bool close = !response_queue_.front().keep_alive();
	with_stream([this, close](auto& s) {
		if (_config.write_timeout > std::chrono::milliseconds::zero())
			tcp_of(s).expires_after(_config.write_timeout);
		::boost::beast::async_write(s, std::move(response_queue_.front()),
			[self = shared_from_this(), close](ec_t ec, std::size_t) {
				self->response_queue_.pop();
				if (ec || close) {
					self->do_close();
					return;
				}
				self->do_write();
			});
	});
}

void ServerBase::Session::do_close() {
	tcp_layer().close();
}

namespace __detail {
std::size_t MapHash::operator()(const std::pair<std::string_view, boost::beast::http::verb>& v) const {
	const std::size_t _1 = std::hash<std::string_view>{}(v.first);
	const std::size_t _2 = std::hash<boost::beast::http::verb>{}(v.second);
	return _1 ^ (_2 + 0x9e3779b9 + (_1 << 6) + (_1 >> 2));
}
} // namespace __detail

} // namespace siesta::beast
