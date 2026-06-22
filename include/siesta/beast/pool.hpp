// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <vector>

#include <siesta/beast/client.hpp>

namespace siesta::beast {

template <typename Client>
class ClientPool {
public:
	ClientPool(::boost::asio::io_context& ctx, std::size_t pool_size,
		ClientBase::Config conf = {})
	{
		clients_.reserve(pool_size);
		for (std::size_t i = 0; i < pool_size; ++i)
			clients_.push_back(std::make_shared<Client>(ctx, conf));
	}

	void start(const ::boost::asio::ip::address& addr, uint16_t port) {
		for (auto& c : clients_) c->start(addr, port);
	}

	void stop() {
		for (auto& c : clients_) c->stop();
	}

	std::shared_ptr<Client>& next() {
		auto idx = next_.fetch_add(1, std::memory_order_relaxed) % clients_.size();
		return clients_[idx];
	}

	std::size_t size() const { return clients_.size(); }

private:
	std::vector<std::shared_ptr<Client>> clients_;
	std::atomic<std::size_t> next_{0};
};

} // namespace siesta::beast
