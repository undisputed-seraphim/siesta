#pragma once

#include <atomic>
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/associated_allocator.hpp>
#include <boost/asio/associated_executor.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/asio/basic_waitable_timer.hpp>
#include <boost/asio/bind_allocator.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/is_executor.hpp>
#include <cstddef>
#include <semaphore>

#include "detail/wait_op.hpp"

namespace siesta::asio {

template <std::ptrdiff_t LeastMaxValue, typename Executor = ::boost::asio::any_io_executor>
class async_counting_semaphore {
public:
	using executor_type = Executor;
	using clock_type = std::chrono::steady_clock;
	using timer_type =
		::boost::asio::basic_waitable_timer<clock_type, ::boost::asio::wait_traits<clock_type>, Executor>;
	static constexpr std::ptrdiff_t max = LeastMaxValue;

	explicit async_counting_semaphore(const executor_type& ex)
		: m_timer{ex, timer_type::time_point::max()} {}
	explicit async_counting_semaphore(executor_type&& ex)
		: m_timer{std::move(ex), timer_type::time_point::max()} {}
	async_counting_semaphore(const async_counting_semaphore&) = delete;
	async_counting_semaphore(async_counting_semaphore&&) noexcept = default;
	~async_counting_semaphore() noexcept = default;

	template <::boost::asio::completion_token_for<bool()> CompletionToken>
	[[nodiscard]] auto async_acquire(CompletionToken&& token) {
		namespace asio = ::boost::asio;
		auto wait_op = [](asio::completion_handler_for<void(boost::system::error_code)> auto&& handler,
						  std::predicate auto&& pred) {
			using handler_type = std::decay_t<decltype(handler)>;
			using predicate_type = std::decay_t<decltype(pred)>;
			using executor_type = asio::associated_executor_t<handler_type>;
			using allocator_type = asio::associated_allocator<handler_type>;

			auto work_guard = asio::make_work_guard(handler);
			struct wait_op_handler final {
				timer_type& _timer;
				predicate_type& _pred;

				asio::executor_work_guard<asio::any_io_executor> _io_work;

				handler_type _handler;

				void operator()(boost::system::error_code ec) {
					if (ec == asio::error::operation_aborted) {
						ec = {};
					}
					if (ec || _pred()) {
						asio::post(get_executor(), asio::bind_allocator(get_allocator(), _handler));
					} else {
						_timer.async_wait(std::move(*this));
					}
				}

				executor_type get_executor() const noexcept { return asio::get_associated_executor(_handler); }
				allocator_type get_allocator() const noexcept { return asio::get_associated_allocator(_handler); }
			};

			auto executor = asio::get_associated_executor(handler);
			detail::wait_pred_op<handler_type, timer_type, predicate_type>(
				std::forward<handler_type>(handler), m_timer, std::forward<predicate_type>(pred));
			asio::dispatch(asio::bind_executor(executor, std::forward<handler_type>(handler)));
		};

		auto try_acquire = [this]() { return m_sem.try_acquire(); };
		return asio::async_initiate<CompletionToken, void(boost::system::error_code)>(
			detail::run_wait_pred_op{}, token, &m_timer, try_acquire);
	}

	void release() {
		m_sem.release();
		m_timer.cancel_one();
	}

private:
	timer_type m_timer;
	std::counting_semaphore<max> m_sem;
};

} // namespace siesta::asio