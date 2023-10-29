#pragma once

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/associated_executor.hpp>
#include <boost/asio/async_result.hpp>
#include <queue>
#include <type_traits>

#include "condition_variable.hpp"

namespace siesta::asio {

template <typename T, typename Executor = ::boost::asio::any_io_executor>
class async_queue {
public:
	static_assert(std::is_move_constructible_v<T>, "T must be move-constructible.");
	static_assert(std::is_move_assignable_v<T>, "T must be move-assignable.");

	using executor_type = Executor;
	using value_type = typename std::queue<T>::value_type;
	using size_type = typename std::queue<T>::size_type;

	explicit async_queue(const executor_type& e) : _acv(e) {}
	explicit async_queue(executor_type&& e) : _acv(std::move(e)) {}
	async_queue(const async_queue&) = delete;
	async_queue(async_queue&&) noexcept = default;
	~async_queue() noexcept = default;

	auto async_poll(::boost::asio::completion_token_for<void(value_type)> auto&& token) {
		auto initiate = [this](::boost::asio::completion_handler_for<void(value_type)> auto&& handler) {
			auto executor = ::boost::asio::get_associated_executor(handler);
			_acv.async_wait([this]() { return !_queue.empty(); });
		};
		return ::boost::asio::async_initiate<decltype(token), void(value_type)>(initiate, token);
	}

	void async_push(value_type&& value) {
		::boost::asio::post([this, value = std::move(value)]() {
			_queue.push(std::move(value));
		});
	}

private:
	async_condition_variable<> _acv;
	std::queue<value_type> _queue;
};

} // namespace siesta::asio