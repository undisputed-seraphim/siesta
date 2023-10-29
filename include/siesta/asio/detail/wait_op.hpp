#pragma once

#include <boost/asio/async_result.hpp>
#include <boost/beast/core/async_base.hpp>
#include <concepts>

namespace siesta::asio::detail {

template <typename Handler, typename Timer>
class base_wait_op : public ::boost::beast::async_base<Handler, typename Timer::executor_type> {
public:
	using timer_type = Timer;

	template <typename H>
	base_wait_op(H&& h, timer_type& t)
		: ::boost::beast::async_base<Handler, typename timer_type::executor_type>(std::forward<H>(h), t.get_executor())
		, _timer{t} {}

protected:
	timer_type& _timer;
};

template <typename Handler, typename Timer>
class wait_op : public base_wait_op<Handler, Timer> {
public:
	template <typename H>
	wait_op(H&& h, Timer& t)
		: base_wait_op<Handler, Timer>(std::forward<H>(h), t) {
		this->_timer.async_wait(std::move(*this));
	}

	void operator()(boost::system::error_code ec) {
		if (ec == ::boost::asio::error::operation_aborted) {
			ec = {};
		}
		this->complete_now(ec);
	}
};

struct run_wait_op {
	template <typename Handler, typename Timer>
	void operator()(Handler&& h, Timer& t) {
		wait_op<typename std::decay<Handler>::type, Timer>(std::forward<Handler>(h), t);
	}
};

template <typename Handler, typename Timer, std::predicate Predicate>
class wait_pred_op : public base_wait_op<Handler, Timer> {
public:
	template <typename H, class P>
	wait_pred_op(H&& h, Timer& t, P&& p)
		: base_wait_op<Handler, Timer>(std::forward<H>(h), t)
		, pred{std::forward<P>(p)} {
		// must be posted such that there is no suspension point
		// between pred() == true and calling the completion handler
		::boost::asio::post(std::move(*this));
	}

	void operator()(boost::system::error_code ec = boost::system::error_code{}) {
		if (ec == ::boost::asio::error::operation_aborted) {
			ec = {};
		}
		if (ec || pred()) {
			this->complete_now(ec);
		} else {
			this->_timer.async_wait(std::move(*this));
		}
	}

protected:
	Predicate pred;
};

struct run_wait_pred_op {
	template <typename Handler, typename Timer, std::predicate Predicate>
	void operator()(Handler&& h, Timer t, Predicate&& pred) {
		wait_pred_op<typename std::decay<Handler>::type, Timer, typename std::decay<Predicate>::type>(
			std::forward<Handler>(h), t, std::forward<Predicate>(pred));
	}
};

} // namespace siesta::asio::detail
