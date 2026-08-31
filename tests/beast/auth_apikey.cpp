#include "server.hpp"
#include "client.hpp"

#include <boost/asio.hpp>
#include <boost/asio/use_future.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>
#include <memory>
#include <siesta/common.hpp>
#include <string>
#include <thread>

namespace asio = boost::asio;

static constexpr uint16_t TEST_PORT = 19981;
static const auto TEST_ADDR = asio::ip::make_address("127.0.0.1");

struct StubServer : ApiKey_Auth_API::Server {
	using ApiKey_Auth_API::Server::Server;

	void get__echo(const request req, Session::Ptr s) override {
		auto it = req.base().find("X-API-Key");
		if (it == req.base().end() || std::string(it->value()) != "correct-key") {
			s->send(s->make_error_response(siesta::Error{
				siesta::ErrorCode::UNAUTHENTICATED, "invalid api key"}));
			return;
		}
		auto resp = s->make_response(200, R"({"message":"hello"})");
		s->send(std::move(resp));
	}
};

static asio::io_context g_server_ctx;
static std::unique_ptr<StubServer> g_server;
static std::thread g_server_thread;

struct ServerListener : Catch::EventListenerBase {
	using EventListenerBase::EventListenerBase;
	void testRunStarting(Catch::TestRunInfo const&) override {
		g_server = std::make_unique<StubServer>(g_server_ctx);
		g_server->start(TEST_ADDR, TEST_PORT);
		g_server_thread = std::thread([]{ g_server_ctx.run(); });
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
	void testRunEnded(Catch::TestRunStats const&) override {
		g_server->shutdown();
		g_server_ctx.stop();
		g_server_thread.join();
		g_server.reset();
		g_server_ctx.restart();
	}
};

CATCH_REGISTER_LISTENER(ServerListener)

TEST_CASE("apikey correct key auth", "[integration][auth]") {
	asio::io_context ctx;
	auto client = std::make_shared<ApiKey_Auth_API::Client>(ctx, "correct-key");
	client->start(TEST_ADDR, TEST_PORT);
	ctx.run();

	auto future = client->get__echo(std::nullopt, asio::use_future);
	ctx.restart();
	ctx.run();
	auto outcome = future.get();
	REQUIRE(outcome.has_value());
	REQUIRE(outcome.value().result_int() == 200);
}

TEST_CASE("apikey wrong key returns 401", "[integration][auth]") {
	asio::io_context ctx;
	auto client = std::make_shared<ApiKey_Auth_API::Client>(ctx, "wrong-key");
	client->start(TEST_ADDR, TEST_PORT);
	ctx.run();

	auto future = client->get__echo(std::nullopt, asio::use_future);
	ctx.restart();
	ctx.run();
	auto outcome = future.get();
	REQUIRE(outcome.has_error());
	REQUIRE(outcome.error().value() == 401);
	REQUIRE(client->last_error().has_value());
	REQUIRE(client->last_error()->code == siesta::ErrorCode::UNAUTHENTICATED);
}
