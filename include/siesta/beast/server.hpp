// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/system_timer.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/core/buffers_generator.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/http/message_generator.hpp>
#include <boost/beast/http/parser.hpp>
#include <boost/beast/http/read.hpp>
#include <functional>
#include <memory>
#include <optional>
#include <queue>

namespace siesta::beast {

class ServerBase {
public:
	using request = ::boost::beast::http::request<::boost::beast::http::string_body>;
	using response = ::boost::beast::http::response<::boost::beast::http::string_body>;
	using protocol = ::boost::asio::ip::tcp;
	using ec_t = ::boost::system::error_code;
	using strand_type = ::boost::asio::strand<::boost::asio::io_context::executor_type>;
	using stream_type = ::boost::beast::basic_stream<protocol, strand_type>;

	struct Config {
		std::chrono::milliseconds read_timeout{std::chrono::hours{1}};
		std::chrono::milliseconds write_timeout{std::chrono::seconds{30}};
		std::uint64_t max_body_size{1024 * 1024};
	};

	class Session : public std::enable_shared_from_this<Session> {
	public:
		using Ptr = std::shared_ptr<Session>;
		Session(ServerBase&, stream_type, Config, uint64_t);
		~Session() noexcept;

		void run();
		uint64_t id() const { return _id; }

		template <bool isRequest, class Body, class Fields>
		void send(::boost::beast::http::message<isRequest, Body, Fields>&& msg) {
			response_queue_.push(::boost::beast::http::message_generator(std::move(msg)));
			if (!is_writing_) do_write();
		}

	protected:
		friend ServerBase;
		static constexpr std::size_t max_responses_ = 64;

		ServerBase& _parent;
		stream_type _stream;
		::boost::beast::flat_buffer _buffer;
		std::optional<::boost::beast::http::request_parser<::boost::beast::http::string_body>> parser_;
		Config _config;
		uint64_t _id;

		std::queue<::boost::beast::http::message_generator> response_queue_;
		bool is_writing_ = false;
		bool should_close_ = false;

		void do_read();
		void on_read(ec_t, std::size_t);
		void do_write();
		void do_close();
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
