#include "server.hpp"
#include "client.hpp"

#include <boost/asio.hpp>
#include <boost/asio/use_future.hpp>
#include <cstdlib>
#include <iostream>
#include <thread>

namespace asio = boost::asio;
namespace http = boost::beast::http;

struct PingServer : Ping::Server {
	using Ping::Server::Server;
	void get__ping(const request req, Session::Ptr session) override {
		auto resp = session->make_response(200, R"({"message":"pong"})");
		session->send(std::move(resp));
	}
};

int main() {
	auto addr = asio::ip::make_address("127.0.0.1");
	constexpr uint16_t port = 19999;

	asio::io_context srv_ctx;
	PingServer srv(srv_ctx);
	srv.start(addr, port);
	std::thread srv_thread([&] { srv_ctx.run(); });
	std::this_thread::sleep_for(std::chrono::milliseconds(50));

	asio::io_context ctx;
	auto client = std::make_shared<Ping::Client>(ctx);
	client->start(addr, port);
	ctx.run();

	auto future = client->get__ping(asio::use_future);
	ctx.restart();
	ctx.run();
	auto outcome = future.get();

	client->stop();
	srv.shutdown();
	srv_ctx.stop();
	srv_thread.join();

	if (!outcome.has_value()) {
		std::cerr << "FAIL: request failed\n";
		return 1;
	}
	std::cout << "OK: consumer smoke test passed\n";
	return 0;
}
