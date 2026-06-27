#include "server.hpp"
#include "client.hpp"

#include <boost/asio.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/beast/http.hpp>
#include <boost/json.hpp>
#include <boost/json/monotonic_resource.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>
#include <memory>
#include <string>
#include <thread>

namespace asio = boost::asio;

static constexpr uint16_t TEST_PORT = 19931;
static const auto TEST_ADDR = asio::ip::make_address("127.0.0.1");

namespace {

inline void reply_json(PetStoreOpenRPC::Server::Session::Ptr s, const std::string& body) {
	auto resp = s->make_response(200, body);
	s->send(std::move(resp));
}

struct StubServer : PetStoreOpenRPC::Server {
	using PetStoreOpenRPC::Server::Server;

	void CreatePet(const request req, Session::Ptr s) override {
		auto sp = s->json_storage();
		auto jv = boost::json::parse(req.body(), sp);
		auto pet = boost::json::value_to<PetStoreOpenRPC::Pet>(jv);
		pet.name = "created_:" + pet.name;
		pet.kind = "created_:" + pet.kind;
		reply_json(std::move(s), boost::json::serialize(boost::json::value_from(pet, sp)));
	}

	void GetPet(const request req, Session::Ptr s) override {
		auto sp = s->json_storage();
		auto jv = boost::json::parse(req.body(), sp);
		auto req_pet = boost::json::value_to<PetStoreOpenRPC::GetPetRequest>(jv);
		PetStoreOpenRPC::Pet pet;
		pet.name = req_pet.name;
		pet.kind = "found_";
		reply_json(std::move(s), boost::json::serialize(boost::json::value_from(pet, sp)));
	}
};

} // anonymous namespace

static asio::io_context g_server_ctx;
static std::unique_ptr<StubServer> g_server;
static std::thread g_server_thread;

struct ServerListener : Catch::EventListenerBase {
	using EventListenerBase::EventListenerBase;

	void testRunStarting(Catch::TestRunInfo const&) override {
		g_server = std::make_unique<StubServer>(g_server_ctx);
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

TEST_CASE("RPC create pet via OpenRPC", "[integration][rpc]") {
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

TEST_CASE("RPC get pet via OpenRPC", "[integration][rpc]") {
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
