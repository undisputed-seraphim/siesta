#include "rpc_openrpc_stubs.hpp"
#include "client.hpp"

#include <boost/asio.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/json.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>
#include <memory>
#include <string>
#include <thread>

namespace asio = boost::asio;
namespace http = boost::beast::http;

static constexpr uint16_t TEST_PORT = 19931;
static const auto TEST_ADDR = asio::ip::make_address("127.0.0.1");

static asio::io_context g_server_ctx;
static std::unique_ptr<rpc_openrpc_testing::StubServer> g_server;
static std::thread g_server_thread;

struct ServerListener : Catch::EventListenerBase {
	using EventListenerBase::EventListenerBase;

	void testRunStarting(Catch::TestRunInfo const&) override {
		g_server = std::make_unique<rpc_openrpc_testing::StubServer>(g_server_ctx);
		g_server->start(TEST_ADDR, TEST_PORT);
		g_server_thread = std::thread([] { g_server_ctx.run(); });
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}

	void testRunEnded(Catch::TestRunStats const&) override {
		g_server_ctx.stop();
		if (g_server_thread.joinable()) g_server_thread.join();
		g_server.reset();
	}
};
CATCH_REGISTER_LISTENER(ServerListener)

static std::shared_ptr<PetStoreOpenRPC::Client> make_client(asio::io_context& ctx) {
	auto client = std::make_shared<PetStoreOpenRPC::Client>(ctx);
	client->start(TEST_ADDR, TEST_PORT);
	ctx.run();
	return client;
}

TEST_CASE("RPC create and get pet via OpenRPC", "[integration][rpc]") {
	asio::io_context ctx;
	auto client = make_client(ctx);

	PetStoreOpenRPC::Pet pet{"Fluffy", "cat"};
	auto future = client->CreatePet(pet, asio::use_future);
	ctx.restart();
	ctx.run();
	auto outcome = future.get();
	REQUIRE(outcome.has_value());
	auto jv = boost::json::parse(outcome.value().body());
	auto resp = boost::json::value_to<PetStoreOpenRPC::Pet>(jv);
	REQUIRE(resp.name == "created_:Fluffy");
	REQUIRE(resp.kind == "created_:cat");
}

TEST_CASE("RPC get pet returns found pet via OpenRPC", "[integration][rpc]") {
	asio::io_context ctx;
	auto client = make_client(ctx);

	PetStoreOpenRPC::GetPetRequest req{"Rex"};
	auto future = client->GetPet(req, asio::use_future);
	ctx.restart();
	ctx.run();
	auto outcome = future.get();
	REQUIRE(outcome.has_value());
	auto jv = boost::json::parse(outcome.value().body());
	auto resp = boost::json::value_to<PetStoreOpenRPC::Pet>(jv);
	REQUIRE(resp.name == "Rex");
	REQUIRE(resp.kind == "found_");
}
