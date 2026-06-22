// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <boost/asio/compose.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/system_timer.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/core/buffers_generator.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/http/message_generator.hpp>
#include <boost/beast/http/parser.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/ssl.hpp>
#include <concepts>
#include <ctime>
#include <functional>
#include <memory>
#include <optional>
#include <queue>
#include <string>
#include <variant>

namespace siesta::beast {

class ServerBase {
public:
	using request = ::boost::beast::http::request<::boost::beast::http::string_body>;
	using response = ::boost::beast::http::response<::boost::beast::http::string_body>;
	using protocol = ::boost::asio::ip::tcp;
	using ec_t = ::boost::system::error_code;
	using strand_type = ::boost::asio::strand<::boost::asio::io_context::executor_type>;
	using stream_type = ::boost::beast::basic_stream<protocol, strand_type>;
	using ssl_stream_type = ::boost::beast::ssl_stream<stream_type>;
	using any_stream = std::variant<stream_type, ssl_stream_type>;

	struct Config {
		std::chrono::milliseconds read_timeout{std::chrono::hours{1}};
		std::chrono::milliseconds write_timeout{std::chrono::seconds{30}};
		std::chrono::milliseconds idle_timeout{std::chrono::seconds{60}};
		std::uint64_t max_body_size{1024 * 1024};
		std::string server_name{"siesta"};
		std::string cors_origin;
		std::string cors_methods{"GET, POST, PUT, DELETE, PATCH, OPTIONS"};
		std::string cors_headers{"Content-Type, Authorization"};
		uint32_t cors_max_age{86400};
		::boost::asio::ssl::context* ssl_ctx = nullptr;
	};

	class Session : public std::enable_shared_from_this<Session> {
	public:
		using Ptr = std::shared_ptr<Session>;
		Session(ServerBase&, any_stream, Config, uint64_t);
		~Session() noexcept;

		void run();
		uint64_t id() const { return _id; }
		bool is_tls() const { return std::holds_alternative<ssl_stream_type>(_stream); }

		template <class Body, class Fields>
		void send(::boost::beast::http::response<Body, Fields>&& msg) {
			if (!_config.server_name.empty())
				msg.set(::boost::beast::http::field::server, _config.server_name);
			msg.set(::boost::beast::http::field::date, rfc7231_date());
			if (!_config.cors_origin.empty())
				msg.set(::boost::beast::http::field::access_control_allow_origin, _config.cors_origin);
			response_queue_.push(::boost::beast::http::message_generator(std::move(msg)));
			if (state_ == State::reading) {
				state_ = State::active;
				do_write();
			}
		}

	protected:
		friend ServerBase;
		static constexpr std::size_t max_responses_ = 64;

		enum class State : uint8_t { handshaking, reading, active, draining, closing, closed };

		ServerBase& _parent;
		any_stream _stream;
		::boost::beast::flat_buffer _buffer;
		std::optional<::boost::beast::http::request_parser<::boost::beast::http::string_body>> parser_;
		Config _config;
		uint64_t _id;

		std::queue<::boost::beast::http::message_generator> response_queue_;
		State state_ = State::reading;

		static stream_type& tcp_of(stream_type& s) { return s; }
		static stream_type& tcp_of(ssl_stream_type& s) { return s.next_layer(); }

		stream_type& tcp_layer() {
			return std::visit([](auto& s) -> stream_type& { return tcp_of(s); }, _stream);
		}

		template <typename F>
			requires std::invocable<F, stream_type&>
			      && std::invocable<F, ssl_stream_type&>
		decltype(auto) with_stream(F&& f) {
			return std::visit(std::forward<F>(f), _stream);
		}

		void start_read_loop();
		void on_read_done(ec_t);
		void do_write();
		void do_close();
		static std::string rfc7231_date();
	};

	ServerBase(boost::asio::io_context&);
	ServerBase(boost::asio::io_context&, Config);

	void start(const ::boost::asio::ip::address, uint16_t);
	void start(const protocol::endpoint&);
	void shutdown();

	virtual void handle_request(const request, Session::Ptr) = 0;

protected:
	Config _conf;
	boost::asio::io_context* _ctx{nullptr};
	protocol::acceptor _acceptor;
	std::atomic<uint64_t> _client_id{0};
	std::atomic<bool> _shutting_down{false};

	void on_accept(const ec_t&, protocol::socket);
};

namespace __detail {
struct MapHash {
	std::size_t operator()(const std::pair<std::string_view, boost::beast::http::verb>& v) const;
};
} // namespace __detail

} // namespace siesta::beast
