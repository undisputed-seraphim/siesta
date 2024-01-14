#include <boost/asio/async_result.hpp>
#include <boost/asio/execution_context.hpp>
#include <boost/asio/io_context.hpp>
// #include <catch2/catch.hpp>
#include <iostream>
#include <siesta/asio/queue.hpp>
#include <thread>
#include <type_traits>
#include <vector>

struct MovableObject {
	MovableObject() noexcept
		: m_value(0) {}
	MovableObject(int value) noexcept
		: m_value(value) {}
	MovableObject(const MovableObject&) = delete;
	MovableObject(MovableObject&& other) noexcept = default;
	~MovableObject() noexcept = default;
	operator int() const noexcept { return m_value; }

private:
	int m_value;
};

int main() {
	boost::asio::io_context ctx(4);

	siesta::asio::async_queue<MovableObject> queue(ctx);
	std::cout << "polling thread" << std::endl;
	auto poll = std::thread([&queue]() {
		bool stop = false;
		while (!stop) {
			queue.async_poll([&stop](boost::system::error_code ec, MovableObject i) {
				std::cout << int(i) << std::endl;
				if (i == 3)
					stop = true;
			});
		}
	});

	std::cout << "pushing threads" << std::endl;
	std::vector<std::thread> threads;
	for (int i = 0; i < 4; ++i) {
		// threads.push_back(std::thread([&]() { queue.async_emplace(i); }));
		threads.push_back(
			std::thread([&]() { queue.async_push(MovableObject(i), [](boost::system::error_code ec) {}); }));
	}

	std::cout << "joining push threads" << std::endl;
	for (auto& t : threads) {
		t.join();
	}

	std::cout << "joining poll thread" << std::endl;
	poll.join();

	std::cout << "Complete." << std::endl;
	return 0;
}