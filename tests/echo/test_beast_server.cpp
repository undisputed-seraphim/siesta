#include "echo_stubs.hpp"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
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
		reply_json(req, std::move(session), "{\"message\":\"" + msg + "\"}");
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
	std::string host = "127.0.0.1";
	uint16_t port = 9900;
	bool use_tls = false;

	int pos = 0;
	for (int i = 1; i < argc; i++) {
		std::string arg = argv[i];
		if (arg == "--tls") { use_tls = true; continue; }
		if (pos == 0) host = arg;
		else if (pos == 1) port = static_cast<uint16_t>(std::stoi(arg));
		++pos;
	}

#ifdef ENABLE_PROFILER
	const char* profile_path = getenv("CPUPROFILE");
	if (profile_path) {
		ProfilerStart(profile_path);
		std::signal(SIGINT, handle_signal);
		std::signal(SIGTERM, handle_signal);
	}
#endif

	asio::io_context ctx;
	std::unique_ptr<asio::ssl::context> ssl;
	if (use_tls) {
		ssl = std::make_unique<asio::ssl::context>(asio::ssl::context::tls_server);
		ssl->use_certificate_chain_file(SIESTA_TEST_CERT_DIR "/server.pem");
		ssl->use_private_key_file(SIESTA_TEST_CERT_DIR "/server.key", asio::ssl::context::pem);
	}

	siesta::beast::ServerBase::Config conf;
	conf.read_timeout = std::chrono::milliseconds::zero();
	conf.write_timeout = std::chrono::milliseconds::zero();
	conf.idle_timeout = std::chrono::milliseconds::zero();
	if (use_tls) conf.ssl_ctx = ssl.get();
	EchoServer server(ctx, conf);
	server.start(asio::ip::make_address(host), port);
	std::cout << "echo-server listening on " << host << ":" << port
	          << (use_tls ? " (TLS)" : "") << std::endl;
	ctx.run();

#ifdef ENABLE_PROFILER
	if (profile_path && profiler_running)
		ProfilerStop();
#endif
	return 0;
}
