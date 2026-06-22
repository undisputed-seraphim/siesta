#include "echo_stubs.hpp"
#include "client.hpp"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/http.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <memory>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace asio = boost::asio;
namespace http = boost::beast::http;
using bench_clock = std::chrono::steady_clock;
using tcp = asio::ip::tcp;

static asio::io_context* g_server_ctx = nullptr;
static asio::io_context* g_client_ctx = nullptr;

static void sigint_handler(int) {
	if (g_client_ctx) g_client_ctx->stop();
	if (g_server_ctx) g_server_ctx->stop();
}

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

struct PipelinedRunner : std::enable_shared_from_this<PipelinedRunner> {
	tcp::socket socket;
	boost::beast::flat_buffer buffer;
	http::request<http::string_body> req;
	http::response<http::string_body> resp;

	std::queue<bench_clock::time_point> send_times;
	std::vector<double> latencies;

	int to_send;
	int to_recv;
	int in_flight = 0;
	int depth;
	std::function<void()> on_done;

	PipelinedRunner(asio::io_context& ctx, int requests, int pipeline_depth)
		: socket(ctx), to_send(requests), to_recv(requests), depth(pipeline_depth) {
		latencies.reserve(requests);
		req.method(http::verb::get);
		req.target("/echo?message=benchmark_pipeline");
		req.version(11);
		req.set(http::field::host, "localhost");
	}

	void start() {
		do_send();
		do_recv();
	}

	void do_send() {
		if (to_send <= 0) return;
		if (in_flight >= depth) return;
		--to_send;
		++in_flight;
		send_times.push(bench_clock::now());
		http::async_write(socket, req,
			[self = shared_from_this()](boost::system::error_code ec, std::size_t) {
				if (ec) return;
				self->do_send();
			});
	}

	void do_recv() {
		if (to_recv <= 0) {
			if (on_done) on_done();
			return;
		}
		resp = {};
		http::async_read(socket, buffer, resp,
			[self = shared_from_this()](boost::system::error_code ec, std::size_t) {
				if (ec) return;
				auto t0 = self->send_times.front();
				self->send_times.pop();
				auto us = std::chrono::duration_cast<std::chrono::microseconds>(
					bench_clock::now() - t0).count();
				self->latencies.push_back(static_cast<double>(us));
				--self->to_recv;
				--self->in_flight;
				self->do_send();
				self->do_recv();
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

static void print_results(const std::vector<double>& all_latencies, double wall_s,
                          int concurrency, int pipeline_depth) {
	int ok = static_cast<int>(all_latencies.size());
	double rps = ok / wall_s;

	auto pct = [&](double p) -> double {
		if (all_latencies.empty()) return 0;
		size_t idx = static_cast<size_t>(all_latencies.size() * p);
		if (idx >= all_latencies.size()) idx = all_latencies.size() - 1;
		return all_latencies[idx];
	};

	std::cout << "Beast Benchmark";
	if (pipeline_depth > 0) std::cout << " (pipeline depth: " << pipeline_depth << ")";
	std::cout << "\n";
	std::cout << "════════════════════════════════════════\n";
	std::cout << "  Requests:     " << std::setw(8) << ok << "\n";
	std::cout << "  Connections:  " << std::setw(8) << concurrency << "\n";
	if (pipeline_depth > 0)
		std::cout << "  Pipeline:     " << std::setw(8) << pipeline_depth << "\n";
	std::cout << "  Wall time:    " << std::setw(7) << std::fixed << std::setprecision(2) << wall_s << " s\n";
	std::cout << "  Throughput:   " << std::setw(8) << format_rps(rps) << " req/s\n";
	std::cout << "  ──────────────────────────────────────\n";
	std::cout << "  Latency p50:  " << std::setw(8) << format_latency(pct(0.50)) << "\n";
	std::cout << "  Latency p95:  " << std::setw(8) << format_latency(pct(0.95)) << "\n";
	std::cout << "  Latency p99:  " << std::setw(8) << format_latency(pct(0.99)) << "\n";
	std::cout << "  Latency max:  " << std::setw(8) << format_latency(pct(1.0)) << "\n";
	std::cout << "════════════════════════════════════════\n";
}

int main(int argc, char* argv[]) {
	int total_requests = 100'000;
	int concurrency = 1;
	int warmup = 100;
	int pipeline_depth = 0;
	uint16_t port = 19920;
	std::string host;
	bool external = false;
	bool use_tls = false;

	for (int i = 1; i < argc; i++) {
		std::string arg = argv[i];
		if ((arg == "--requests" || arg == "-n") && i + 1 < argc)
			total_requests = std::stoi(argv[++i]);
		else if ((arg == "--concurrency" || arg == "-c") && i + 1 < argc)
			concurrency = std::stoi(argv[++i]);
		else if (arg == "--warmup" && i + 1 < argc)
			warmup = std::stoi(argv[++i]);
		else if (arg == "--pipeline" && i + 1 < argc)
			pipeline_depth = std::stoi(argv[++i]);
		else if (arg == "--port" && i + 1 < argc)
			port = static_cast<uint16_t>(std::stoi(argv[++i]));
		else if (arg == "--host" && i + 1 < argc) {
			host = argv[++i];
			external = true;
		} else if (arg == "--tls") {
			use_tls = true;
		} else if (arg == "--help" || arg == "-h") {
			std::cout << "Usage: echo_beast_benchmark [OPTIONS]\n"
			          << "  -n, --requests N     Total requests (default: 100000)\n"
			          << "  -c, --concurrency C  Persistent connections (default: 1)\n"
			          << "      --pipeline N     Pipeline depth per connection (0=sequential)\n"
			          << "      --warmup N       Warmup requests per connection (default: 100)\n"
			          << "      --host H         Connect to external server (skip embedded)\n"
			          << "      --port P         Server port (default: 19920)\n"
			          << "      --tls            Enable TLS (sequential mode only)\n";
			return 0;
		}
	}

	auto addr = asio::ip::make_address(external ? host : "127.0.0.1");
	tcp::endpoint endpoint(addr, port);

	if (use_tls && pipeline_depth > 0) {
		std::cerr << "error: --tls is not supported with --pipeline\n";
		return 1;
	}

	std::unique_ptr<asio::ssl::context> srv_ssl, cli_ssl;
	if (use_tls) {
		srv_ssl = std::make_unique<asio::ssl::context>(asio::ssl::context::tls_server);
		srv_ssl->use_certificate_chain_file(SIESTA_TEST_CERT_DIR "/server.pem");
		srv_ssl->use_private_key_file(SIESTA_TEST_CERT_DIR "/server.key", asio::ssl::context::pem);
		cli_ssl = std::make_unique<asio::ssl::context>(asio::ssl::context::tls_client);
		cli_ssl->load_verify_file(SIESTA_TEST_CERT_DIR "/server.pem");
	}

	asio::io_context server_ctx;
	std::unique_ptr<echo_testing::DefaultServer> server;
	std::thread server_thread;

	if (!external) {
		siesta::beast::ServerBase::Config srv_conf;
		srv_conf.read_timeout = std::chrono::milliseconds::zero();
		srv_conf.write_timeout = std::chrono::milliseconds::zero();
		srv_conf.idle_timeout = std::chrono::milliseconds::zero();
		if (use_tls) srv_conf.ssl_ctx = srv_ssl.get();
		server = std::make_unique<echo_testing::DefaultServer>(server_ctx, srv_conf);
		server->start(addr, port);
		server_thread = std::thread([&] { server_ctx.run(); });
		std::this_thread::sleep_for(std::chrono::milliseconds(30));
	}

	asio::io_context client_ctx;
	g_server_ctx = &server_ctx;
	g_client_ctx = &client_ctx;
	std::signal(SIGINT, sigint_handler);
	std::signal(SIGTERM, sigint_handler);

	if (pipeline_depth > 0) {
		std::vector<std::shared_ptr<PipelinedRunner>> runners;
		int per_conn = total_requests / concurrency;
		int remainder = total_requests % concurrency;

		for (int i = 0; i < concurrency; i++) {
			int n = per_conn + (i < remainder ? 1 : 0);
			auto r = std::make_shared<PipelinedRunner>(client_ctx, n, pipeline_depth);
			r->socket.connect(endpoint);
			runners.push_back(r);
		}

		if (warmup > 0) {
			std::cerr << "  warmup (" << warmup * concurrency << " requests) ... " << std::flush;
			std::atomic<int> warm_done{0};
			std::vector<std::shared_ptr<PipelinedRunner>> warm_runners;
			for (int i = 0; i < concurrency; i++) {
				auto r = std::make_shared<PipelinedRunner>(client_ctx, warmup, pipeline_depth);
				r->socket.connect(endpoint);
				r->on_done = [&] { if (++warm_done == concurrency) client_ctx.stop(); };
				warm_runners.push_back(r);
			}
			for (auto& r : warm_runners) asio::post(client_ctx, [r] { r->start(); });
			client_ctx.run();
			client_ctx.restart();
			for (auto& r : warm_runners) r->socket.close();
			std::cerr << "done\n";
		}

		std::cerr << "  running " << total_requests << " requests over "
		          << concurrency << " connection" << (concurrency > 1 ? "s" : "")
		          << " (pipeline " << pipeline_depth << ")"
		          << (external ? " external " + host + ":" + std::to_string(port) : "")
		          << " ... " << std::flush;

		std::atomic<int> done_count{0};
		for (auto& r : runners) {
			r->on_done = [&] { if (++done_count == concurrency) client_ctx.stop(); };
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
		print_results(all_latencies, wall_s, concurrency, pipeline_depth);

		for (auto& r : runners) r->socket.close();
	} else {
		siesta::beast::ClientBase::Config cli_conf;
		cli_conf.read_timeout = std::chrono::milliseconds::zero();
		cli_conf.write_timeout = std::chrono::milliseconds::zero();
		if (use_tls) cli_conf.ssl_ctx = cli_ssl.get();

		std::vector<std::shared_ptr<Echo_API::Client>> clients;
		for (int i = 0; i < concurrency; i++) {
			auto c = std::make_shared<Echo_API::Client>(client_ctx, cli_conf);
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
		print_results(all_latencies, wall_s, concurrency, pipeline_depth);
	}

	if (!external) {
		server_ctx.stop();
		server_thread.join();
	}
	g_server_ctx = nullptr;
	g_client_ctx = nullptr;
	return 0;
}
