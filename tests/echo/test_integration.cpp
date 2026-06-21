#include "client.hpp"
#include "server.hpp"

#include <boost/asio.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/json.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

namespace asio = boost::asio;
namespace http = boost::beast::http;

static constexpr uint16_t TEST_PORT = 19910;
static const auto TEST_ADDR = asio::ip::make_address("127.0.0.1");

static std::string extract_path_segment(std::string_view target, size_t index) {
	if (auto q = target.find('?'); q != std::string_view::npos)
		target = target.substr(0, q);
	size_t pos = 0;
	size_t seg = 0;
	while (pos < target.size()) {
		if (target[pos] == '/') { ++pos; continue; }
		auto end = target.find('/', pos);
		if (end == std::string_view::npos) end = target.size();
		if (seg == index) return std::string(target.substr(pos, end - pos));
		pos = end;
		++seg;
	}
	return {};
}

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

struct StubServer : Echo_API::Server {
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

	void post__echo(const request req, Session::Ptr session) override {
		auto& resp = session->get_response();
		resp.result(http::status::ok);
		resp.body() = req.body();
		resp.set(http::field::content_type, "application/json");
		resp.prepare_payload();
		session->write();
	}

	void get__echo__id(const request req, Session::Ptr session) override {
		auto id_str = extract_path_segment(req.target(), 1);
		auto& resp = session->get_response();
		resp.result(http::status::ok);
		resp.body() = "{\"message\":\"" + id_str + "\"}";
		resp.set(http::field::content_type, "application/json");
		resp.prepare_payload();
		session->write();
	}

	void delete__echo__id(const request req, Session::Ptr session) override {
		auto id_str = extract_path_segment(req.target(), 1);
		auto& resp = session->get_response();
		resp.result(http::status::ok);
		resp.body() = "{\"message\":\"deleted " + id_str + "\"}";
		resp.set(http::field::content_type, "application/json");
		resp.prepare_payload();
		session->write();
	}

	void get__items(const request req, Session::Ptr session) override {
		auto limit_str = extract_query_param(req.target(), "limit");
		auto status_str = extract_query_param(req.target(), "status");
		boost::json::object obj;
		obj["id"] = 1;
		obj["name"] = "test-item";
		if (!limit_str.empty())  obj["description"] = "limit=" + limit_str;
		if (!status_str.empty()) obj["description"] = "status=" + status_str;
		auto& resp = session->get_response();
		resp.result(http::status::ok);
		resp.body() = boost::json::serialize(obj);
		resp.set(http::field::content_type, "application/json");
		resp.prepare_payload();
		session->write();
	}

	void post__items(const request req, Session::Ptr session) override {
		auto& resp = session->get_response();
		resp.result(http::status::ok);
		resp.body() = req.body();
		resp.set(http::field::content_type, "application/json");
		resp.prepare_payload();
		session->write();
	}

	void get__items_search(const request req, Session::Ptr session) override {
		auto cat = extract_query_param(req.target(), "category");
		auto q = extract_query_param(req.target(), "q");
		auto& resp = session->get_response();
		resp.result(http::status::ok);
		resp.body() = "{\"message\":\"cat=" + cat + ",q=" + q + "\"}";
		resp.set(http::field::content_type, "application/json");
		resp.prepare_payload();
		session->write();
	}

	void get__items__itemId_tags__tagIndex(const request req, Session::Ptr session) override {
		auto itemId = extract_path_segment(req.target(), 1);
		auto tagIndex = extract_path_segment(req.target(), 3);
		auto& resp = session->get_response();
		resp.result(http::status::ok);
		resp.body() = "{\"message\":\"" + itemId + ":" + tagIndex + "\"}";
		resp.set(http::field::content_type, "application/json");
		resp.prepare_payload();
		session->write();
	}

	void put__items__id(const request req, Session::Ptr session) override {
		auto id_str = extract_path_segment(req.target(), 1);
		auto jv = boost::json::parse(req.body());
		auto item = boost::json::value_to<Echo_API::Item>(jv);
		item.description = "updated:" + id_str;
		auto& resp = session->get_response();
		resp.result(http::status::ok);
		resp.body() = boost::json::serialize(boost::json::value_from(item));
		resp.set(http::field::content_type, "application/json");
		resp.prepare_payload();
		session->write();
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

static Echo_API::EchoResponse call_echo(
	std::shared_ptr<Echo_API::Client> client, asio::io_context& ctx,
	const std::string& msg, std::optional<std::string> header = std::nullopt) {
	auto future = client->get__echo(msg, header, asio::use_future);
	ctx.restart();
	ctx.run();
	auto outcome = future.get();
	REQUIRE(outcome.has_value());
	auto jv = boost::json::parse(outcome.value().body());
	return boost::json::value_to<Echo_API::EchoResponse>(jv);
}

static std::shared_ptr<Echo_API::Client> make_client(asio::io_context& ctx) {
	auto client = std::make_shared<Echo_API::Client>(ctx);
	client->start(TEST_ADDR, TEST_PORT);
	ctx.run();
	return client;
}

struct RawTestClient : Echo_API::Client {
	using Echo_API::Client::Client;
	using Echo_API::Client::async_submit_request;
};

// ── Existing echo tests (sanity) ────────────────────────────────

TEST_CASE("echo basic", "[integration]") {
	asio::io_context ctx;
	auto client = make_client(ctx);
	auto resp = call_echo(client, ctx, "hello");
	REQUIRE(resp.message == "hello");
}

// ── POST echo body round-trip ───────────────────────────────────

TEST_CASE("POST echo body round-trip", "[integration]") {
	asio::io_context ctx;
	auto client = make_client(ctx);

	Echo_API::EchoResponse body;
	body.message = "round-trip";
	auto future = client->post__echo(body, asio::use_future);
	ctx.restart();
	ctx.run();
	auto outcome = future.get();
	REQUIRE(outcome.has_value());
	auto jv = boost::json::parse(outcome.value().body());
	auto resp = boost::json::value_to<Echo_API::EchoResponse>(jv);
	REQUIRE(resp.message == "round-trip");
}

// ── Path parameter ──────────────────────────────────────────────

TEST_CASE("GET with path param", "[integration]") {
	asio::io_context ctx;
	auto client = make_client(ctx);

	auto future = client->get__echo__id(42, asio::use_future);
	ctx.restart();
	ctx.run();
	auto outcome = future.get();
	REQUIRE(outcome.has_value());
	auto jv = boost::json::parse(outcome.value().body());
	auto resp = boost::json::value_to<Echo_API::EchoResponse>(jv);
	REQUIRE(resp.message == "42");
}

// ── DELETE verb ─────────────────────────────────────────────────

TEST_CASE("DELETE verb dispatches", "[integration]") {
	asio::io_context ctx;
	auto client = make_client(ctx);

	auto future = client->delete__echo__id(99, asio::use_future);
	ctx.restart();
	ctx.run();
	auto outcome = future.get();
	REQUIRE(outcome.has_value());
	REQUIRE(outcome.value().result() == http::status::ok);
	auto jv = boost::json::parse(outcome.value().body());
	auto resp = boost::json::value_to<Echo_API::EchoResponse>(jv);
	REQUIRE(resp.message == "deleted 99");
}

// ── Multi-verb same path ────────────────────────────────────────

TEST_CASE("multi-verb same path dispatches correctly", "[integration]") {
	asio::io_context ctx;
	auto client = make_client(ctx);

	auto f1 = client->get__echo__id(7, asio::use_future);
	ctx.restart();
	ctx.run();
	auto o1 = f1.get();
	REQUIRE(o1.has_value());
	auto r1 = boost::json::value_to<Echo_API::EchoResponse>(boost::json::parse(o1.value().body()));
	REQUIRE(r1.message == "7");

	auto f2 = client->delete__echo__id(7, asio::use_future);
	ctx.restart();
	ctx.run();
	auto o2 = f2.get();
	REQUIRE(o2.has_value());
	auto r2 = boost::json::value_to<Echo_API::EchoResponse>(boost::json::parse(o2.value().body()));
	REQUIRE(r2.message == "deleted 7");
}

// ── POST Item full struct round-trip ────────────────────────────

TEST_CASE("POST Item all fields round-trip", "[integration]") {
	asio::io_context ctx;
	auto client = make_client(ctx);

	Echo_API::Item item;
	item.id = 1;
	item.name = "widget";
	item.description = "a useful thing";
	item.tags = {"alpha", "beta"};
	item.status = Echo_API::ItemStatus::active;

	auto future = client->post__items(item, asio::use_future);
	ctx.restart();
	ctx.run();
	auto outcome = future.get();
	REQUIRE(outcome.has_value());
	auto jv = boost::json::parse(outcome.value().body());
	auto resp = boost::json::value_to<Echo_API::Item>(jv);
	REQUIRE(resp.id == 1);
	REQUIRE(resp.name == "widget");
	REQUIRE(resp.description == "a useful thing");
	REQUIRE(resp.tags.size() == 2);
	REQUIRE(resp.tags[0] == "alpha");
	REQUIRE(resp.tags[1] == "beta");
	REQUIRE(resp.status == Echo_API::ItemStatus::active);
}

// ── POST Item required-only ─────────────────────────────────────

TEST_CASE("POST Item required-only fields", "[integration]") {
	asio::io_context ctx;
	auto client = make_client(ctx);

	Echo_API::Item item;
	item.id = 2;
	item.name = "minimal";

	auto future = client->post__items(item, asio::use_future);
	ctx.restart();
	ctx.run();
	auto outcome = future.get();
	REQUIRE(outcome.has_value());
	auto jv = boost::json::parse(outcome.value().body());
	auto resp = boost::json::value_to<Echo_API::Item>(jv);
	REQUIRE(resp.id == 2);
	REQUIRE(resp.name == "minimal");
}

// ── GET items with optional query params ────────────────────────

TEST_CASE("GET items with limit query param", "[integration]") {
	asio::io_context ctx;
	auto client = make_client(ctx);

	auto future = client->get__items(10, std::nullopt, asio::use_future);
	ctx.restart();
	ctx.run();
	auto outcome = future.get();
	REQUIRE(outcome.has_value());
	REQUIRE(outcome.value().result() == http::status::ok);
	auto jv = boost::json::parse(outcome.value().body());
	auto resp = boost::json::value_to<Echo_API::Item>(jv);
	REQUIRE(resp.description == "limit=10");
}

TEST_CASE("GET items with both query params", "[integration]") {
	asio::io_context ctx;
	auto client = make_client(ctx);

	auto future = client->get__items(5, std::string("active"), asio::use_future);
	ctx.restart();
	ctx.run();
	auto outcome = future.get();
	REQUIRE(outcome.has_value());
	REQUIRE(outcome.value().result() == http::status::ok);
}

// ── Enum values round-trip ──────────────────────────────────────

TEST_CASE("enum inactive round-trip", "[integration]") {
	asio::io_context ctx;
	auto client = make_client(ctx);

	Echo_API::Item item;
	item.id = 10;
	item.name = "test";
	item.status = Echo_API::ItemStatus::inactive;

	auto future = client->post__items(item, asio::use_future);
	ctx.restart();
	ctx.run();
	auto outcome = future.get();
	REQUIRE(outcome.has_value());
	auto resp = boost::json::value_to<Echo_API::Item>(boost::json::parse(outcome.value().body()));
	REQUIRE(resp.status == Echo_API::ItemStatus::inactive);
}

TEST_CASE("enum archived round-trip", "[integration]") {
	asio::io_context ctx;
	auto client = make_client(ctx);

	Echo_API::Item item;
	item.id = 11;
	item.name = "test";
	item.status = Echo_API::ItemStatus::archived;

	auto future = client->post__items(item, asio::use_future);
	ctx.restart();
	ctx.run();
	auto outcome = future.get();
	REQUIRE(outcome.has_value());
	auto resp = boost::json::value_to<Echo_API::Item>(boost::json::parse(outcome.value().body()));
	REQUIRE(resp.status == Echo_API::ItemStatus::archived);
}

// ── Server 404 fallback ─────────────────────────────────────────

TEST_CASE("server returns 404 for unknown path", "[integration]") {
	asio::io_context ctx;
	auto client = std::make_shared<RawTestClient>(ctx);
	client->start(TEST_ADDR, TEST_PORT);
	ctx.run();

	boost::beast::http::request<boost::beast::http::string_body> req;
	req.method(http::verb::get);
	req.target("/nonexistent");
	auto future = client->async_submit_request(std::move(req), asio::use_future);
	ctx.restart();
	ctx.run();
	auto outcome = future.get();
	REQUIRE_FALSE(outcome.has_value());
	REQUIRE(outcome.error().value() == 404);
}

// ── PUT with body + path param ──────────────────────────────────

TEST_CASE("PUT item with body and path param", "[integration]") {
	asio::io_context ctx;
	auto client = make_client(ctx);

	Echo_API::Item item;
	item.id = 42;
	item.name = "updated-widget";
	item.tags = {"x"};

	auto future = client->put__items__id(item, 42, asio::use_future);
	ctx.restart();
	ctx.run();
	auto outcome = future.get();
	REQUIRE(outcome.has_value());
	REQUIRE(outcome.value().result() == http::status::ok);
	auto resp = boost::json::value_to<Echo_API::Item>(boost::json::parse(outcome.value().body()));
	REQUIRE(resp.id == 42);
	REQUIRE(resp.name == "updated-widget");
	REQUIRE(resp.description == "updated:42");
	REQUIRE(resp.tags.size() == 1);
}

// ── Multiple path params ────────────────────────────────────────

TEST_CASE("GET with multiple path params", "[integration]") {
	asio::io_context ctx;
	auto client = make_client(ctx);

	auto future = client->get__items__itemId_tags__tagIndex(100, 2, asio::use_future);
	ctx.restart();
	ctx.run();
	auto outcome = future.get();
	REQUIRE(outcome.has_value());
	auto resp = boost::json::value_to<Echo_API::EchoResponse>(boost::json::parse(outcome.value().body()));
	REQUIRE(resp.message == "100:2");
}

// ── Required int query param ────────────────────────────────────

TEST_CASE("GET with required int and string query params", "[integration]") {
	asio::io_context ctx;
	auto client = make_client(ctx);

	auto future = client->get__items_search(7, "widgets", asio::use_future);
	ctx.restart();
	ctx.run();
	auto outcome = future.get();
	REQUIRE(outcome.has_value());
	auto resp = boost::json::value_to<Echo_API::EchoResponse>(boost::json::parse(outcome.value().body()));
	REQUIRE(resp.message == "cat=7,q=widgets");
}
