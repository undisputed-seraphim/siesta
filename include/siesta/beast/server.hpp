#pragma once

#include <array>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/system_timer.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/http/read.hpp>
#include <functional>
#include <memory>

namespace siesta::beast {

class ServerBase {
public:
	using request = ::boost::beast::http::request<::boost::beast::http::string_body>;
	using response = ::boost::beast::http::response<::boost::beast::http::string_body>;
	using protocol = ::boost::asio::ip::tcp;
	using ec_t = ::boost::system::error_code;

	struct Config {};

	class Session : public std::enable_shared_from_this<Session> {
	public:
		using Ptr = std::shared_ptr<Session>;
		Session(ServerBase&, protocol::socket, Config, uint64_t);
		~Session() noexcept;

		void run();
		uint64_t id() const { return _id; }

		response& get_response() noexcept { return _response; }
		void write();

	protected:
		friend ServerBase;

		ServerBase& _parent;
		::boost::beast::tcp_stream _stream;
		::boost::beast::flat_buffer _buffer;
		request _request;
		response _response;
		Config _config;
		uint64_t _id;

		void do_read();
		void on_read(ec_t, std::size_t);
		void on_write(ec_t, std::size_t);
		void do_close();
	};

	ServerBase(boost::asio::io_context&, Config = {});

	void start(boost::asio::io_context&, const ::boost::asio::ip::address, uint16_t);
	void start(boost::asio::io_context&, const protocol::endpoint&);

	virtual void handle_request(const request, Session::Ptr) = 0;

protected:
	Config _conf;
	protocol::acceptor _acceptor;
	std::atomic<uint64_t> _client_id{0};

	void on_accept(boost::asio::io_context&, const ec_t&, protocol::socket);
};

namespace __detail {
struct MapHash {
	std::size_t operator()(const std::pair<std::string_view, boost::beast::http::verb>& v) const;
};
} // namespace __detail

} // namespace siesta::beast