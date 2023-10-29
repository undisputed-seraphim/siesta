#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/use_future.hpp>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

#include "echo_client.hpp"
#include "echo_defs.hpp"

namespace asio = ::boost::asio;

int main(int argc, char* argv[]) try {
	const auto address = asio::ip::address::from_string("127.0.0.1");
	asio::io_context ctx;
	auto guard = asio::make_work_guard(ctx);
	auto t = std::thread([&ctx]() { ctx.run(); });
	auto client = std::make_shared<swagger::Client>(ctx);
	client->start(address, 8080);
	std::this_thread::sleep_for(std::chrono::seconds{1});

	for (int i = 0; i < 10; ++i) {
		auto fut = client->echo("text/html", "test_message", ::boost::asio::use_future);
		std::this_thread::sleep_for(std::chrono::seconds{1});
		std::cout << fut.get().value().body() << std::endl;
	}

	guard.reset();
	t.join();
} catch (const std::exception& e) {
	std::cerr << "Caught exception " << e.what() << std::endl;
}