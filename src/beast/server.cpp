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

bool ServerBase::run_interceptors(request_context& ctx) {
	for (const auto& f : _interceptors) {
		if (!f(ctx)) return false;
	}
	return true;
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

ServerBase::Session::~Session() noexcept {
	ws_.reset();
	ec_t ec;
	tcp_layer().socket().close(ec);
}

void ServerBase::Session::run() {
	if (auto* ssl = std::get_if<ssl_stream_type>(&_stream)) {
		state_ = State::handshaking;
		tcp_of(*ssl).expires_after(std::chrono::seconds{30});
		ssl->async_handshake(asio::ssl::stream_base::server,
			[self = shared_from_this()](ec_t ec) {
				if (ec) return fail("ssl_handshake", ec);
				self->start_read_loop();
			});
	} else {
		asio::post(tcp_layer().get_executor(), [self = shared_from_this()] {
			self->start_read_loop();
		});
	}
}

std::string ServerBase::Session::rfc7231_date() {
	return siesta::rfc7231_date();
}

void ServerBase::Session::start_read_loop() {
	state_ = State::reading;
	with_stream([this](auto& s) {
		enum class Phase : uint8_t { init, awaiting_read };
		auto done = [self = shared_from_this()](ec_t ec) { self->on_read_done(ec); };
		asio::async_compose<decltype(done), void(ec_t)>(
			[this, &s, phase = Phase::init]
			(auto& self, ec_t ec = {}, std::size_t = 0) mutable {
				switch (phase) {
				case Phase::awaiting_read:
					if (ec == http::error::end_of_stream
						|| ec == asio::ssl::error::stream_truncated) {
						self.complete(ec);
						return;
					}
					if (ec == http::error::body_limit) {
						unsigned ver = parser_->get().version();
						http::response<http::string_body> resp{
							http::status::payload_too_large, ver};
						resp.body() = R"({"error":"request body too large"})";
						resp.set(http::field::content_type, "application/json");
						resp.keep_alive(false);
						resp.prepare_payload();
						send(std::move(resp));
						parser_.reset();
						self.complete(ec);
						return;
					}
					if (ec) {
						self.complete(ec);
						return;
					}
					{
						auto req = parser_->release();
						parser_.reset();

						accepts_gzip_ = req[http::field::accept_encoding].find("gzip")
							!= std::string_view::npos;
						head_request_ = (req.method() == http::verb::head);

						if (req.method() == http::verb::options
							&& !_config.cors_origin.empty()) {
							http::response<http::string_body> resp{
								http::status::no_content, req.version()};
							resp.set(http::field::access_control_allow_methods,
								_config.cors_methods);
							resp.set(http::field::access_control_allow_headers,
								_config.cors_headers);
							resp.set(http::field::access_control_max_age,
								std::to_string(_config.cors_max_age));
							resp.keep_alive(req.keep_alive());
							resp.prepare_payload();
							send(std::move(resp));
						} else if (response_queue_.size() >= max_responses_) {
							http::response<http::string_body> resp{
								http::status::too_many_requests, req.version()};
							resp.body() = R"({"error":"too many requests"})";
							resp.set(http::field::content_type, "application/json");
							resp.keep_alive(false);
							resp.prepare_payload();
							send(std::move(resp));
							self.complete({});
							return;
						} else {
							_parent.handle_request(
								std::move(req), shared_from_this());
						}
					}
					[[fallthrough]];

				case Phase::init:
					json_pool_ = ::boost::json::make_shared_resource<
						::boost::json::monotonic_resource>(4096);
					if (_parent._shutting_down || ws_) {
						self.complete({});
						return;
					}
					parser_.emplace();
					if (_config.max_body_size > 0)
						parser_->body_limit(_config.max_body_size);
					else
						parser_->body_limit(boost::none);
					{
						auto timeout =
							_config.idle_timeout > std::chrono::milliseconds::zero()
							? _config.idle_timeout : _config.read_timeout;
						if (timeout > std::chrono::milliseconds::zero())
							tcp_of(s).expires_after(timeout);
					}
					phase = Phase::awaiting_read;
					http::async_read(s, _buffer, *parser_, std::move(self));
					return;
				}
			},
			done, s);
	});
}

void ServerBase::Session::on_read_done(ec_t) {
	if (ws_) return;
	if (state_ == State::active)
		state_ = State::draining;
	else
		do_close();
}

void ServerBase::Session::do_write() {
	if (response_queue_.empty()) {
		if (state_ == State::active)
			state_ = State::reading;
		else if (state_ == State::draining || _parent._shutting_down)
			do_close();
		return;
	}
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
	if (state_ == State::closing) return;
	state_ = State::closing;

	if (ws_) {
		ws_->async_close(::boost::beast::websocket::close_code::normal,
			[self = shared_from_this()](ec_t ec) {
				self->tcp_layer().close();
				self->state_ = State::closed;
			});
	} else if (auto* ssl = std::get_if<ssl_stream_type>(&_stream)) {
		tcp_of(*ssl).expires_after(std::chrono::seconds{30});
		ssl->async_shutdown([self = shared_from_this()](ec_t ec) {
			self->tcp_layer().close();
			self->state_ = State::closed;
		});
	} else {
		tcp_layer().close();
		state_ = State::closed;
	}
}

namespace __detail {
std::size_t MapHash::operator()(const std::pair<std::string_view, boost::beast::http::verb>& v) const {
	const std::size_t _1 = std::hash<std::string_view>{}(v.first);
	const std::size_t _2 = std::hash<boost::beast::http::verb>{}(v.second);
	return _1 ^ (_2 + 0x9e3779b9 + (_1 << 6) + (_1 >> 2));
}
} // namespace __detail

} // namespace siesta::beast
