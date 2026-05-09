// SPDX-License-Identifier: Apache-2.0
// Test server binary — concrete openapi::Server implementation.
// Usage: ./test_server [host] [port]
// Default: ./test_server 127.0.0.1 9900

#include "server.hpp"

#include <boost/asio.hpp>
#include <boost/beast/http.hpp>
#include <csignal>
#include <cstdio>
#include <iostream>
#include <string>
#include <string_view>

#ifdef ENABLE_PROFILER
#include <gperftools/profiler.h>
#endif

namespace asio = ::boost::asio;
namespace http = ::boost::beast::http;

static std::string url_decode(std::string_view sv) {
	std::string result;
	result.reserve(sv.size());
	for (size_t i = 0; i < sv.size(); ++i) {
		if (sv[i] == '%' && i + 2 < sv.size()) {
			char hi = sv[i + 1];
			char lo = sv[i + 2];
			auto hex = [](char c) -> int {
				if (c >= '0' && c <= '9') return c - '0';
				if (c >= 'A' && c <= 'F') return c - 'A' + 10;
				if (c >= 'a' && c <= 'f') return c - 'a' + 10;
				return -1;
			};
			int h = hex(hi), l = hex(lo);
			if (h >= 0 && l >= 0) {
				result += static_cast<char>((h << 4) | l);
				i += 2;
				continue;
			}
		}
		result += sv[i];
	}
	return result;
}

struct EchoServer : Echo_API::Server {
	using Echo_API::Server::Server;

	void get__echo(const request req, Session::Ptr session) override {
		std::string_view target = req.target();
		std::string_view value;
		if (auto pos = target.find("message="); pos != std::string_view::npos) {
			value = target.substr(pos + 8);
			if (auto amp = value.find('&'); amp != std::string_view::npos)
				value = value.substr(0, amp);
		}

		auto& resp = session->get_response();
		resp.result(http::status::ok);
		resp.body() = "{\"message\":\"" + url_decode(value) + "\"}";
		resp.set(http::field::content_type, "application/json");
		resp.prepare_payload();
		session->write();
	}
};

#ifdef ENABLE_PROFILER
static volatile sig_atomic_t profiler_running = 1;

static void handle_signal(int) {
	if (!profiler_running) return;
	ProfilerFlush();
	ProfilerStop();
	profiler_running = 0;
	_exit(0);
}
#endif

int main(int argc, char* argv[]) {
	std::string host = argc > 1 ? argv[1] : "127.0.0.1";
	uint16_t port = argc > 2 ? static_cast<uint16_t>(std::stoi(argv[2])) : 9900;

#ifdef ENABLE_PROFILER
	const char* profile_path = getenv("CPUPROFILE");
	if (profile_path) {
		ProfilerStart(profile_path);
		std::signal(SIGINT, handle_signal);
		std::signal(SIGTERM, handle_signal);
	}
#endif

	asio::io_context ctx;
	EchoServer server(ctx);
	server.start(asio::ip::make_address(host), port);
	std::cout << "echo-server listening on " << host << ":" << port << std::endl;
	ctx.run();

#ifdef ENABLE_PROFILER
	if (profile_path && profiler_running)
		ProfilerStop();
#endif
	return 0;
}
