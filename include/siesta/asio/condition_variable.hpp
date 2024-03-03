// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/asio/basic_waitable_timer.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/system/detail/error_code.hpp>
#include <type_traits>

namespace siesta::asio {

template <typename Executor = ::boost::asio::any_io_executor>
class async_condition_variable {
public:
	using executor_type = Executor;
	using timer_type = ::boost::asio::steady_timer;
	using clock_type = timer_type::clock_type;
	using error_type = ::boost::system::error_code;

	template <typename ExecutionContext>
		requires(std::convertible_to<ExecutionContext&, ::boost::asio::execution_context&>)
	explicit async_condition_variable(ExecutionContext& ctx)
		: m_timer(ctx) {}

	explicit async_condition_variable(const executor_type& ex)
		: m_timer{ex, timer_type::time_point::max()} {}
	explicit async_condition_variable(executor_type&& ex)
		: m_timer{std::move(ex), timer_type::time_point::max()} {}
	async_condition_variable(const async_condition_variable&) = delete;
	async_condition_variable(async_condition_variable&&) noexcept = default;
	~async_condition_variable() noexcept = default;

	template <::boost::asio::completion_token_for<void(error_type)> CompletionToken>
	[[nodiscard]] auto async_wait(CompletionToken&& token) {
		auto initiation = [](::boost::asio::completion_handler_for<void(error_type)> auto&& handler,
							 timer_type& timer) {
			using CompletionHandler = typename std::decay_t<decltype(handler)>;
			struct wait_op {
				CompletionHandler handler;
				timer_type& timer;
				void operator()(const ::boost::system::error_code& ec) {
					if (ec == ::boost::asio::error::operation_aborted) {
						std::move(handler)(ec);
					} else {
						timer.async_wait(std::move(*this));
					}
				}

				using wait_exec_type = ::boost::asio::associated_executor_t<CompletionHandler, executor_type>;
				wait_exec_type get_executor() const noexcept {
					return ::boost::asio::get_associated_executor(handler, timer.get_executor());
				}

				using wait_alloc_type = ::boost::asio::associated_allocator_t<CompletionHandler, std::allocator<void>>;
				wait_alloc_type get_allocator() const noexcept {
					return ::boost::asio::get_associated_allocator(handler, std::allocator<void>{});
				}
			};
			timer.async_wait(wait_op{std::forward<CompletionHandler>(handler), timer});
		};
		return ::boost::asio::async_initiate<CompletionToken, void(error_type)>(initiation, token, std::ref(m_timer));
	}

	template <::boost::asio::completion_token_for<void(error_type)> CompletionToken, std::predicate Predicate>
	[[nodiscard]] auto async_wait(Predicate&& pred, CompletionToken&& token) {
		auto initiation = [](::boost::asio::completion_handler_for<void(error_type)> auto&& handler,
							 timer_type& timer,
							 Predicate&& pred) {
			using CompletionHandler = typename std::decay_t<decltype(handler)>;
			struct wait_op {
				CompletionHandler handler;
				timer_type& timer;
				Predicate pred;
				void operator()(::boost::system::error_code ec) {
					if (pred()) {
						std::move(handler)(ec);
					} else {
						timer.async_wait(std::move(*this));
					}
				}

				using wait_exec_type = ::boost::asio::associated_executor_t<CompletionHandler, executor_type>;
				wait_exec_type get_executor() const noexcept {
					return ::boost::asio::get_associated_executor(handler, timer.get_executor());
				}

				using wait_alloc_type = ::boost::asio::associated_allocator_t<CompletionHandler, std::allocator<void>>;
				wait_alloc_type get_allocator() const noexcept {
					return ::boost::asio::get_associated_allocator(handler, std::allocator<void>{});
				}
			};
			timer.async_wait(wait_op{std::forward<CompletionHandler>(handler), timer, std::forward<Predicate>(pred)});
		};
		return ::boost::asio::async_initiate<CompletionToken, void(error_type)>(
			initiation, token, std::ref(m_timer), std::forward<Predicate>(pred));
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
