#pragma once

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/associated_allocator.hpp>
#include <boost/asio/associated_executor.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/compose.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/execution_context.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/system/detail/errc.hpp>
#include <boost/system/detail/error_category.hpp>
#include <boost/system/detail/error_code.hpp>
#include <boost/system/errc.hpp>
#include <boost/system/result.hpp>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <system_error>
#include <type_traits>

namespace siesta::asio {

template <typename T>
	requires(std::is_move_constructible_v<T>)
class async_queue {
public:
	using value_type = typename std::decay_t<T>;
	using size_type = typename std::queue<value_type>::size_type;
	using timer_type = ::boost::asio::steady_timer;
	using executor_type = ::boost::asio::steady_timer::executor_type;
	using error_type = ::boost::system::error_code;

	template <typename ExecutionContext>
		requires(std::convertible_to<ExecutionContext&, ::boost::asio::execution_context&>)
	explicit async_queue(ExecutionContext& ctx)
		: _timer(::boost::asio::make_strand(ctx), timer_type::time_point::max()) {}

	explicit async_queue(const executor_type& e)
		: _timer(e, timer_type::time_point::max()) {}
	explicit async_queue(executor_type&& e)
		: _timer(std::move(e), timer_type::time_point::max()) {}
	async_queue(const async_queue&) = delete;
	async_queue(async_queue&&) noexcept = default;
	~async_queue() noexcept = default;

	// Asynchronously pushes a value into the queue.
	// Error code will return boost::system::errc::not_enough_memory if an exception is thrown from the queue.
	template <::boost::asio::completion_token_for<void(error_type)> CompletionToken>
	auto async_push(value_type&& value, CompletionToken&& token) {
		using Self = typename std::decay_t<decltype(*this)>;
		auto initiation =
			[](::boost::asio::completion_handler_for<void(error_type)> auto&& handler, Self& self, value_type&& value) {
				error_type ec = {};
				try {
					self._queue.push(std::forward<value_type>(value));
					self._timer.cancel_one();
				} catch (const std::system_error& e) {
					ec = e.code();
				} catch (...) {
					ec = ::boost::system::errc::make_error_code(::boost::system::errc::not_enough_memory);
				}
				std::move(handler)(ec);
			};
		return ::boost::asio::async_initiate<CompletionToken, void(error_type)>(
			initiation, token, *this, std::forward<value_type>(value));
	}

	template <::boost::asio::completion_token_for<void(error_type)> CompletionToken, typename... Args>
	auto async_emplace(CompletionToken&& token, Args&&... args) {
		using Self = typename std::decay_t<decltype(*this)>;
		auto initiation = [... args = std::forward<Args>(args)](
							  ::boost::asio::completion_handler_for<void()> auto&& handler, Self& self) {
			error_type ec = {};
			try {
				self._queue.emplace(std::forward<Args>(args)...);
				self._timer.cancel_one();
			} catch (const std::system_error& e) {
				ec = e.code();
			} catch (...) {
				ec = ::boost::system::errc::make_error_code(::boost::system::errc::not_enough_memory);
			}
			std::move(handler)(ec);
		};
		return ::boost::asio::async_initiate<CompletionToken, void(error_type)>(initiation, token, *this);
	}

	// You MUST check the value of the error code, as the returned value may be nonsense
	// if the error is non-zero.
	template <::boost::asio::completion_token_for<void(error_type, value_type)> CompletionToken>
	auto async_poll(CompletionToken&& token) {
		using Self = typename std::decay_t<decltype(*this)>;
		auto init = [](::boost::asio::completion_handler_for<void(error_type, value_type)> auto&& handler, Self& self) {
			using Handler = typename std::decay_t<decltype(handler)>;
			struct wait_op {
				Self& self;
				Handler handler;
				boost::asio::executor_work_guard<executor_type> io_work;
				void operator()(::boost::system::error_code ec) {
					if (ec != ::boost::asio::error::operation_aborted) {
						self._timer.async_wait(std::move(*this));
						return;
					}
					if (self._queue.empty()) {
						self._timer.async_wait(std::move(*this));
						return;
					}
					auto executor = ::boost::asio::get_associated_executor(handler, self._timer.get_executor());
					value_type value = std::move(self._queue.front());
					self._queue.pop();
					io_work.reset();
					std::move(handler)(ec, std::move(value));
				}

				using wait_exec_type = ::boost::asio::associated_executor_t<Handler, executor_type>;
				wait_exec_type get_executor() const noexcept {
					return ::boost::asio::get_associated_executor(handler, self._timer.get_executor());
				}

				using wait_alloc_type = ::boost::asio::associated_allocator_t<Handler, std::allocator<void>>;
				wait_alloc_type get_allocator() const noexcept {
					return ::boost::asio::get_associated_allocator(handler, std::allocator<void>{});
				}
			};
			self._timer.async_wait(wait_op{
				self, std::forward<Handler>(handler), ::boost::asio::make_work_guard(self._timer.get_executor())});
		};
		return ::boost::asio::async_initiate<CompletionToken, void(error_type, value_type)>(init, token, *this);
	}

private:
	timer_type _timer;
	std::queue<value_type> _queue;
};

template <typename T>
	requires(std::is_move_constructible_v<T>)
class simple_blocking_queue {
public:
	using value_type = typename std::decay_t<T>;

	simple_blocking_queue(const simple_blocking_queue&) = delete;
	simple_blocking_queue(simple_blocking_queue&&) = default;

	template <typename... Args>
	void emplace(Args&&... args) {
		std::lock_guard<std::mutex> lock(_mutex);
		_queue.emplace(std::forward<Args>(args)...);
		_cv.notify_one();
	}

	void push(value_type&& value) {
		std::lock_guard<std::mutex> lock(_mutex);
		_queue.push(std::forward<value_type>(value));
		_cv.notify_one();
	}

	[[nodiscard]] value_type poll() {
		std::unique_lock<std::mutex> lock(_mutex, std::defer_lock);
		_cv.wait(lock, [this]() { return !_queue.empty(); });
		value_type value = std::move(_queue.front());
		_queue.pop();
		return value;
	}

	void pop() {
		std::lock_guard<std::mutex> lock(_mutex);
		_queue.pop();
	}

	bool empty() const noexcept { return _queue.empty(); }

	std::size_t size() const noexcept { return _queue.size(); }

private:
	std::queue<value_type> _queue;
	std::condition_variable _cv;
	std::mutex _mutex;
};

} // namespace siesta::asio