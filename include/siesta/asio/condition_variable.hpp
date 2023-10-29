#pragma once

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/asio/basic_waitable_timer.hpp>
#include <boost/asio/is_executor.hpp>

#include "detail/wait_op.hpp"

namespace siesta::asio {

template <typename Executor = ::boost::asio::any_io_executor>
class async_condition_variable {
public:
	using executor_type = Executor;
	using clock_type = std::chrono::steady_clock;
	using timer_type =
		::boost::asio::basic_waitable_timer<clock_type, ::boost::asio::wait_traits<clock_type>, Executor>;

	explicit async_condition_variable(const executor_type& ex)
		: m_timer{ex, timer_type::time_point::max()} {}
	explicit async_condition_variable(executor_type&& ex)
		: m_timer{std::move(ex), timer_type::time_point::max()} {}
	async_condition_variable(const async_condition_variable&) = delete;
	async_condition_variable(async_condition_variable&&) noexcept = default;
	~async_condition_variable() noexcept = default;

	template <::boost::asio::completion_token_for<bool()> CompletionToken>
	[[nodiscard]] auto async_wait(CompletionToken&& token) {
		return ::boost::asio::async_initiate<CompletionToken, void(boost::system::error_code)>(
			detail::run_wait_op{}, token, m_timer);
	}

	template <std::predicate Predicate, ::boost::asio::completion_token_for<void()> CompletionToken>
	[[nodiscard]] auto async_wait(Predicate&& pred, CompletionToken&& token) {
		return ::boost::asio::async_initiate<CompletionToken, void(boost::system::error_code)>(
			detail::run_wait_pred_op{}, token, m_timer, std::forward<Predicate>(pred));
	}

	void notify_one() { m_timer.cancel_one(); }

	void notify_all() { m_timer.cancel(); }

	executor_type get_executor() { return m_timer.get_executor(); }

	async_condition_variable& operator=(const async_condition_variable&) = delete;
	async_condition_variable& operator=(async_condition_variable&&) = default;

private:
	timer_type m_timer;
};

} // namespace siesta::asio
