#pragma once

#include <array>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/json.hpp>
#include <functional>
#include <queue>

#include <siesta/beast/format.hpp>

namespace siesta {

class ClientBase {
public:
	using request = ::boost::beast::http::request<::boost::beast::http::string_body>;
	using response = ::boost::beast::http::response<::boost::beast::http::string_body>;
	using protocol = ::boost::asio::ip::tcp;
	using error_code = ::boost::beast::error_code;

	struct Config {
		std::chrono::milliseconds connect_timeout;
		std::chrono::milliseconds write_timeout;
		std::chrono::milliseconds read_timeout;
	};

	ClientBase(::boost::asio::io_context& ctx)
		: _resolver(::boost::asio::make_strand(ctx))
		, _stream(::boost::asio::make_strand(ctx)) {}
	ClientBase(const ClientBase&) = delete;
	ClientBase(ClientBase&&) = default;
	virtual ~ClientBase() { stop(); }

	void start(const protocol::endpoint& endpoint) {
		_resolver.async_resolve(endpoint, std::bind_front(&ClientBase::on_resolve, this));
	}

	void stop() {
		_resolver.cancel();
		_stream.close();
	}

protected:
	::boost::asio::ip::tcp::resolver _resolver;
	::boost::beast::flat_buffer _buffer;
	::boost::beast::tcp_stream _stream;
	std::queue<::boost::beast::saved_handler> _pending_handlers;

	unsigned char _json_buffer[1024];

	void on_resolve(const ::boost::beast::error_code& ec, protocol::resolver::results_type results) {
		if (ec) {
			return;
		}
		_stream.expires_after(std::chrono::seconds{30});
		_stream.async_connect(results, std::bind_front(&ClientBase::on_connect, this));
	}

	void on_connect(const ::boost::beast::error_code& ec, protocol::resolver::endpoint_type endpoint) {
		if (ec) {
			return;
		}
		_stream.expires_after(std::chrono::seconds{30});

		auto pres = std::make_unique<response>();
		::boost::beast::http::async_read(
			_stream, _buffer, *pres, [this, ptr = std::move(pres)](const error_code& ec, size_t sz) mutable {
				this->on_read(std::move(ptr), ec, sz);
			});
	}

	void on_write(const error_code& ec, size_t) {
		if (ec) {
			return;
		}
	}

	void on_read(std::unique_ptr<response>&& pres, const error_code& ec, size_t) {
		auto pres_ = std::move(pres);
		if (ec) {
			return;
		}
		::boost::beast::http::async_read(
			_stream, _buffer, *pres_, [this, ptr = std::move(pres_)](const error_code& ec, size_t sz) mutable {
				this->on_read(std::move(ptr), ec, sz);
			});
	}
};

} // namespace siesta