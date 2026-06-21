#include "echo_stubs.hpp"
#include "client.hpp"

#include <boost/asio.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace asio = boost::asio;
using bench_clock = std::chrono::steady_clock;

struct Runner : std::enable_shared_from_this<Runner> {
	std::shared_ptr<Echo_API::Client> client;
	int remaining;
	std::vector<double> latencies;
	bench_clock::time_point req_start;
	std::function<void()> on_done;

	void start() {
		latencies.reserve(remaining);
		send_next();
	}

	void send_next() {
		if (remaining <= 0) {
			if (on_done) on_done();
			return;
		}
		--remaining;
		req_start = bench_clock::now();
		client->get__echo("benchmark_payload", std::nullopt,
			[self = shared_from_this()](auto) {
				auto us = std::chrono::duration_cast<std::chrono::microseconds>(
					bench_clock::now() - self->req_start).count();
				self->latencies.push_back(static_cast<double>(us));
				self->send_next();
			});
	}
};

static std::string format_latency(double us) {
	char buf[32];
	if (us < 1000.0)
		std::snprintf(buf, sizeof(buf), "%.0f us", us);
	else if (us < 1'000'000.0)
		std::snprintf(buf, sizeof(buf), "%.1f ms", us / 1000.0);
	else
		std::snprintf(buf, sizeof(buf), "%.3f s", us / 1'000'000.0);
	return buf;
}

static std::string format_rps(double rps) {
	char buf[32];
	if (rps >= 1'000'000.0)
		std::snprintf(buf, sizeof(buf), "%.2fM", rps / 1'000'000.0);
	else if (rps >= 1'000.0)
		std::snprintf(buf, sizeof(buf), "%.1fk", rps / 1'000.0);
	else
		std::snprintf(buf, sizeof(buf), "%.0f", rps);
	return buf;
}

int main(int argc, char* argv[]) {
	int total_requests = 100'000;
	int concurrency = 1;
	int warmup = 100;
	uint16_t port = 19920;
	std::string host;
	bool external = false;

	for (int i = 1; i < argc; i++) {
		std::string arg = argv[i];
		if ((arg == "--requests" || arg == "-n") && i + 1 < argc)
			total_requests = std::stoi(argv[++i]);
		else if ((arg == "--concurrency" || arg == "-c") && i + 1 < argc)
			concurrency = std::stoi(argv[++i]);
		else if (arg == "--warmup" && i + 1 < argc)
			warmup = std::stoi(argv[++i]);
		else if (arg == "--port" && i + 1 < argc)
			port = static_cast<uint16_t>(std::stoi(argv[++i]));
		else if (arg == "--host" && i + 1 < argc) {
			host = argv[++i];
			external = true;
		} else if (arg == "--help" || arg == "-h") {
			std::cout << "Usage: echo_beast_benchmark [OPTIONS]\n"
			          << "  -n, --requests N     Total requests (default: 100000)\n"
			          << "  -c, --concurrency C  Persistent connections (default: 1)\n"
			          << "      --warmup N       Warmup requests per connection (default: 100)\n"
			          << "      --host H         Connect to external server (skip embedded)\n"
			          << "      --port P         Server port (default: 19920)\n";
			return 0;
		}
	}

	auto addr = asio::ip::make_address(external ? host : "127.0.0.1");

	asio::io_context server_ctx;
	std::unique_ptr<echo_testing::DefaultServer> server;
	std::thread server_thread;

	if (!external) {
		server = std::make_unique<echo_testing::DefaultServer>(server_ctx);
		server->start(addr, port);
		server_thread = std::thread([&] { server_ctx.run(); });
		std::this_thread::sleep_for(std::chrono::milliseconds(30));
	}

	asio::io_context client_ctx;
	std::vector<std::shared_ptr<Echo_API::Client>> clients;
	for (int i = 0; i < concurrency; i++) {
		auto c = std::make_shared<Echo_API::Client>(client_ctx);
		c->start(addr, port);
		clients.push_back(c);
	}
	client_ctx.run();

	if (warmup > 0) {
		std::cerr << "  warmup (" << warmup * concurrency << " requests) ... " << std::flush;
		std::atomic<int> warm_done{0};
		client_ctx.restart();
		std::vector<std::shared_ptr<Runner>> warm_runners;
		for (int i = 0; i < concurrency; i++) {
			auto r = std::make_shared<Runner>();
			r->client = clients[i];
			r->remaining = warmup;
			r->on_done = [&] { if (++warm_done == concurrency) client_ctx.stop(); };
			warm_runners.push_back(r);
		}
		for (auto& r : warm_runners) asio::post(client_ctx, [r] { r->start(); });
		client_ctx.run();
		std::cerr << "done\n";
	}

	int per_client = total_requests / concurrency;
	int remainder = total_requests % concurrency;

	std::cerr << "  running " << total_requests << " requests over "
	          << concurrency << " connection" << (concurrency > 1 ? "s" : "")
	          << (external ? " (external " + host + ":" + std::to_string(port) + ")" : "")
	          << " ... " << std::flush;

	std::atomic<int> done_count{0};
	client_ctx.restart();
	std::vector<std::shared_ptr<Runner>> runners;
	for (int i = 0; i < concurrency; i++) {
		auto r = std::make_shared<Runner>();
		r->client = clients[i];
		r->remaining = per_client + (i < remainder ? 1 : 0);
		r->on_done = [&] { if (++done_count == concurrency) client_ctx.stop(); };
		runners.push_back(r);
	}

	auto t_start = bench_clock::now();
	for (auto& r : runners) asio::post(client_ctx, [r] { r->start(); });
	client_ctx.run();
	auto t_end = bench_clock::now();
	double wall_s = std::chrono::duration<double>(t_end - t_start).count();

	std::cerr << "done\n\n";

	std::vector<double> all_latencies;
	for (auto& r : runners)
		all_latencies.insert(all_latencies.end(), r->latencies.begin(), r->latencies.end());
	std::sort(all_latencies.begin(), all_latencies.end());

	int ok = static_cast<int>(all_latencies.size());
	double rps = ok / wall_s;

	auto pct = [&](double p) -> double {
		if (all_latencies.empty()) return 0;
		size_t idx = static_cast<size_t>(all_latencies.size() * p);
		if (idx >= all_latencies.size()) idx = all_latencies.size() - 1;
		return all_latencies[idx];
	};

	std::cout << "Beast Benchmark\n";
	std::cout << "════════════════════════════════════════\n";
	std::cout << "  Requests:     " << std::setw(8) << ok << "\n";
	std::cout << "  Connections:  " << std::setw(8) << concurrency << "\n";
	std::cout << "  Wall time:    " << std::setw(7) << std::fixed << std::setprecision(2) << wall_s << " s\n";
	std::cout << "  Throughput:   " << std::setw(8) << format_rps(rps) << " req/s\n";
	std::cout << "  ──────────────────────────────────────\n";
	std::cout << "  Latency p50:  " << std::setw(8) << format_latency(pct(0.50)) << "\n";
	std::cout << "  Latency p95:  " << std::setw(8) << format_latency(pct(0.95)) << "\n";
	std::cout << "  Latency p99:  " << std::setw(8) << format_latency(pct(0.99)) << "\n";
	std::cout << "  Latency max:  " << std::setw(8) << format_latency(pct(1.0)) << "\n";
	std::cout << "════════════════════════════════════════\n";

	if (!external) {
		server_ctx.stop();
		server_thread.join();
	}
	return 0;
}
