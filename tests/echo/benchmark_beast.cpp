#include "client.hpp"
#include "server.hpp"

#include <boost/asio.hpp>
#include <boost/beast/http.hpp>
#include <boost/json.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace asio = boost::asio;
namespace http = boost::beast::http;
using bench_clock = std::chrono::steady_clock;

static constexpr uint16_t DEFAULT_PORT = 19920;
static const auto LOCALHOST = asio::ip::make_address("127.0.0.1");

static std::string extract_query_param(std::string_view target, std::string_view key) {
	auto q = target.find('?');
	if (q == std::string_view::npos) return {};
	auto qs = target.substr(q + 1);
	std::string needle(key);
	needle += '=';
	auto pos = qs.find(needle);
	if (pos == std::string_view::npos) return {};
	auto val_start = pos + needle.size();
	auto amp = qs.find('&', val_start);
	if (amp == std::string_view::npos) amp = qs.size();
	return std::string(qs.substr(val_start, amp - val_start));
}

struct BenchServer : Echo_API::Server {
	using Echo_API::Server::Server;

	void get__echo(const request req, Session::Ptr session) override {
		auto msg = extract_query_param(req.target(), "message");
		auto& resp = session->get_response();
		resp.result(http::status::ok);
		resp.body() = "{\"message\":\"" + msg + "\"}";
		resp.set(http::field::content_type, "application/json");
		resp.prepare_payload();
		session->write();
	}
	void post__echo(const request, Session::Ptr s) override { s->get_response().result(http::status::ok); s->write(); }
	void get__echo__id(const request, Session::Ptr s) override { s->get_response().result(http::status::ok); s->write(); }
	void delete__echo__id(const request, Session::Ptr s) override { s->get_response().result(http::status::ok); s->write(); }
	void get__items(const request, Session::Ptr s) override { s->get_response().result(http::status::ok); s->write(); }
	void post__items(const request, Session::Ptr s) override { s->get_response().result(http::status::ok); s->write(); }
	void get__items_search(const request, Session::Ptr s) override { s->get_response().result(http::status::ok); s->write(); }
	void get__items__itemId_tags__tagIndex(const request, Session::Ptr s) override { s->get_response().result(http::status::ok); s->write(); }
	void put__items__id(const request, Session::Ptr s) override { s->get_response().result(http::status::ok); s->write(); }
	void post__items_detailed(const request, Session::Ptr s) override { s->get_response().result(http::status::ok); s->write(); }
	void post__outcome(const request, Session::Ptr s) override { s->get_response().result(http::status::ok); s->write(); }
};

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
	uint16_t port = DEFAULT_PORT;

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
		else if (arg == "--help" || arg == "-h") {
			std::cout << "Usage: echo_beast_benchmark [OPTIONS]\n"
			          << "  -n, --requests N     Total requests (default: 100000)\n"
			          << "  -c, --concurrency C  Persistent connections (default: 1)\n"
			          << "      --warmup N       Warmup requests per connection (default: 100)\n"
			          << "      --port P         Server port (default: 19920)\n";
			return 0;
		}
	}

	int per_client = total_requests / concurrency;
	int remainder = total_requests % concurrency;

	asio::io_context server_ctx;
	BenchServer server(server_ctx);
	server.start(LOCALHOST, port);
	std::thread server_thread([&] { server_ctx.run(); });
	std::this_thread::sleep_for(std::chrono::milliseconds(30));

	asio::io_context client_ctx;

	std::vector<std::shared_ptr<Echo_API::Client>> clients;
	for (int i = 0; i < concurrency; i++) {
		auto c = std::make_shared<Echo_API::Client>(client_ctx);
		c->start(LOCALHOST, port);
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

	std::cerr << "  running " << total_requests << " requests over "
	          << concurrency << " connection" << (concurrency > 1 ? "s" : "")
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
	for (auto& r : runners) {
		all_latencies.insert(all_latencies.end(), r->latencies.begin(), r->latencies.end());
	}
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

	server_ctx.stop();
	server_thread.join();
	return 0;
}
