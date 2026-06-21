#include "echo_stubs.hpp"

#include <boost/asio.hpp>
#include <csignal>
#include <cstdio>
#include <iostream>
#include <string>

#ifdef ENABLE_PROFILER
#include <gperftools/profiler.h>
#endif

namespace asio = ::boost::asio;

struct EchoServer : echo_testing::DefaultServer {
	using echo_testing::DefaultServer::DefaultServer;

	void get__echo(const request req, Session::Ptr session) override {
		auto msg = echo_testing::url_decode(
			echo_testing::extract_query_param(req.target(), "message"));
		reply_json(std::move(session), "{\"message\":\"" + msg + "\"}");
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
	siesta::beast::ServerBase::Config conf;
	conf.read_timeout = std::chrono::milliseconds::zero();
	conf.write_timeout = std::chrono::milliseconds::zero();
	EchoServer server(ctx, conf);
	server.start(asio::ip::make_address(host), port);
	std::cout << "echo-server listening on " << host << ":" << port << std::endl;
	ctx.run();

#ifdef ENABLE_PROFILER
	if (profile_path && profiler_running)
		ProfilerStop();
#endif
	return 0;
}
