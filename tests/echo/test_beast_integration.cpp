#include "echo_stubs.hpp"
#include "client.hpp"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/json.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>
#include <memory>
#include <siesta/beast/compression.hpp>
#include <siesta/beast/pool.hpp>
#include <string>
#include <string_view>
#include <thread>

namespace asio = boost::asio;
namespace http = boost::beast::http;

static constexpr uint16_t TEST_PORT = 19910;
static const auto TEST_ADDR = asio::ip::make_address("127.0.0.1");

struct StubServer : echo_testing::DefaultServer {
	using echo_testing::DefaultServer::DefaultServer;

	void get__echo__id(const request req, Session::Ptr session) override {
		auto id_str = echo_testing::extract_path_segment(req.target(), 1);
		reply_json(req, std::move(session), "{\"message\":\"" + id_str + "\"}");
	}

	void delete__echo__id(const request req, Session::Ptr session) override {
		auto id_str = echo_testing::extract_path_segment(req.target(), 1);
		reply_json(req, std::move(session), "{\"message\":\"deleted " + id_str + "\"}");
	}

	void get__items(const request req, Session::Ptr session) override {
		auto limit_str = echo_testing::extract_query_param(req.target(), "limit");
		auto status_str = echo_testing::extract_query_param(req.target(), "status");
		boost::json::object obj;
		obj["id"] = 1;
		obj["name"] = "test-item";
		if (!limit_str.empty())  obj["description"] = "limit=" + limit_str;
		if (!status_str.empty()) obj["description"] = "status=" + status_str;
		reply_json(req, std::move(session), boost::json::serialize(obj));
	}

	void get__items_search(const request req, Session::Ptr session) override {
		auto cat = echo_testing::extract_query_param(req.target(), "category");
		auto q = echo_testing::extract_query_param(req.target(), "q");
		reply_json(req, std::move(session), "{\"message\":\"cat=" + cat + ",q=" + q + "\"}");
	}

	void get__items__itemId_tags__tagIndex(const request req, Session::Ptr session) override {
		auto itemId = echo_testing::extract_path_segment(req.target(), 1);
		auto tagIndex = echo_testing::extract_path_segment(req.target(), 3);
		reply_json(req, std::move(session), "{\"message\":\"" + itemId + ":" + tagIndex + "\"}");
	}

	void put__items__id(const request req, Session::Ptr session) override {
		auto id_str = echo_testing::extract_path_segment(req.target(), 1);
		auto jv = boost::json::parse(req.body());
		auto item = boost::json::value_to<Echo_API::Item>(jv);
		item.description = "updated:" + id_str;
		reply_json(req, std::move(session), boost::json::serialize(boost::json::value_from(item)));
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

TEST_CASE("echo basic", "[integration][beast]") {
	asio::io_context ctx;
	auto client = make_client(ctx);
	auto resp = call_echo(client, ctx, "hello");
	REQUIRE(resp.message == "hello");
}

// ── POST echo body round-trip ───────────────────────────────────

TEST_CASE("POST echo body round-trip", "[integration][beast]") {
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

TEST_CASE("GET with path param", "[integration][beast]") {
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

TEST_CASE("DELETE verb dispatches", "[integration][beast]") {
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

TEST_CASE("multi-verb same path dispatches correctly", "[integration][beast]") {
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

TEST_CASE("POST Item all fields round-trip", "[integration][beast]") {
	asio::io_context ctx;
	auto client = make_client(ctx);

	Echo_API::Item item{};
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
	REQUIRE(resp.name == "processed:widget");
	REQUIRE(resp.description == "a useful thing");
	REQUIRE(resp.tags.size() == 3);
	REQUIRE(resp.tags[0] == "alpha");
	REQUIRE(resp.tags[1] == "beta");
	REQUIRE(resp.tags[2] == "server-added");
	REQUIRE(resp.status == Echo_API::ItemStatus::active);
}

// ── POST Item required-only ─────────────────────────────────────

TEST_CASE("POST Item required-only fields", "[integration][beast]") {
	asio::io_context ctx;
	auto client = make_client(ctx);

	Echo_API::Item item{};
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
	REQUIRE(resp.name == "processed:minimal");
}

// ── GET items with optional query params ────────────────────────

TEST_CASE("GET items with limit query param", "[integration][beast]") {
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

TEST_CASE("GET items with both query params", "[integration][beast]") {
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

TEST_CASE("enum inactive round-trip", "[integration][beast]") {
	asio::io_context ctx;
	auto client = make_client(ctx);

	Echo_API::Item item{};
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

TEST_CASE("enum archived round-trip", "[integration][beast]") {
	asio::io_context ctx;
	auto client = make_client(ctx);

	Echo_API::Item item{};
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

TEST_CASE("server returns 404 for unknown path", "[integration][beast]") {
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

TEST_CASE("PUT item with body and path param", "[integration][beast]") {
	asio::io_context ctx;
	auto client = make_client(ctx);

	Echo_API::Item item{};
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

TEST_CASE("GET with multiple path params", "[integration][beast]") {
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

TEST_CASE("GET with required int and string query params", "[integration][beast]") {
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

// ── allOf inheritance round-trip ────────────────────────────────

TEST_CASE("POST DetailedItem allOf all fields round-trip", "[integration][beast]") {
	asio::io_context ctx;
	auto client = make_client(ctx);

	Echo_API::DetailedItem di{};
	di.id = 100;
	di.name = "detailed-widget";
	di.description = "base desc";
	di.tags = {"x", "y", "z"};
	di.status = Echo_API::ItemStatus::inactive;
	di.detail = "extra info";
	di.rating = 4.5;

	auto future = client->post__items_detailed(di, asio::use_future);
	ctx.restart();
	ctx.run();
	auto outcome = future.get();
	REQUIRE(outcome.has_value());
	auto jv = boost::json::parse(outcome.value().body());
	auto resp = boost::json::value_to<Echo_API::DetailedItem>(jv);
	REQUIRE(resp.id == 100);
	REQUIRE(resp.name == "detailed-widget");
	REQUIRE(resp.description == "base desc");
	REQUIRE(resp.tags.size() == 3);
	REQUIRE(resp.status == Echo_API::ItemStatus::inactive);
	REQUIRE(resp.detail == "processed:extra info");
	REQUIRE(resp.rating == 5.5);
}

TEST_CASE("POST DetailedItem allOf required-only fields", "[integration][beast]") {
	asio::io_context ctx;
	auto client = make_client(ctx);

	Echo_API::DetailedItem di{};
	di.id = 200;
	di.name = "minimal-detailed";
	di.detail = "required-detail";

	auto future = client->post__items_detailed(di, asio::use_future);
	ctx.restart();
	ctx.run();
	auto outcome = future.get();
	REQUIRE(outcome.has_value());
	auto jv = boost::json::parse(outcome.value().body());
	auto resp = boost::json::value_to<Echo_API::DetailedItem>(jv);
	REQUIRE(resp.id == 200);
	REQUIRE(resp.name == "minimal-detailed");
	REQUIRE(resp.detail == "processed:required-detail");
	REQUIRE(resp.tags.empty());
}

// ── oneOf variant round-trip ────────────────────────────────────

TEST_CASE("POST Outcome variant with EchoResponse alternative", "[integration][beast]") {
	asio::io_context ctx;
	auto client = make_client(ctx);

	Echo_API::Outcome body = Echo_API::EchoResponse{"variant-msg"};
	auto future = client->post__outcome(body, asio::use_future);
	ctx.restart();
	ctx.run();
	auto outcome = future.get();
	REQUIRE(outcome.has_value());
	auto jv = boost::json::parse(outcome.value().body());
	auto resp = boost::json::value_to<Echo_API::Outcome>(jv);
	REQUIRE(std::holds_alternative<Echo_API::EchoResponse>(resp));
	REQUIRE(std::get<Echo_API::EchoResponse>(resp).message == "visited:variant-msg");
}

TEST_CASE("POST Outcome variant with Error serialization", "[integration][beast]") {
	asio::io_context ctx;
	auto client = make_client(ctx);

	Echo_API::Error err{};
	err.code = 42;
	err.message = "something failed";
	err.fields = "field1";
	Echo_API::Outcome body = err;

	auto future = client->post__outcome(body, asio::use_future);
	ctx.restart();
	ctx.run();
	auto outcome = future.get();
	REQUIRE(outcome.has_value());
	auto jv = boost::json::parse(outcome.value().body());
	auto resp = boost::json::value_to<Echo_API::Outcome>(jv);
	REQUIRE(std::holds_alternative<Echo_API::EchoResponse>(resp));
	REQUIRE(std::get<Echo_API::EchoResponse>(resp).message == "visited:something failed");
}

// ── Graceful shutdown ───────────────────────────────────────────

TEST_CASE("graceful shutdown drains and stops", "[integration][beast]") {
	static constexpr uint16_t SHUTDOWN_PORT = 19911;

	asio::io_context srv_ctx;
	echo_testing::DefaultServer srv(srv_ctx);
	srv.start(TEST_ADDR, SHUTDOWN_PORT);
	std::thread srv_thread([&] { srv_ctx.run(); });
	std::this_thread::sleep_for(std::chrono::milliseconds(30));

	asio::io_context ctx;
	auto client = std::make_shared<Echo_API::Client>(ctx);
	client->start(TEST_ADDR, SHUTDOWN_PORT);
	ctx.run();
	auto resp = call_echo(client, ctx, "before-shutdown");
	REQUIRE(resp.message == "before-shutdown");
	client->stop();

	srv.shutdown();
	srv_thread.join();
}

TEST_CASE("no new connections after shutdown", "[integration][beast]") {
	static constexpr uint16_t SHUTDOWN_PORT2 = 19912;

	asio::io_context srv_ctx;
	echo_testing::DefaultServer srv(srv_ctx);
	srv.start(TEST_ADDR, SHUTDOWN_PORT2);
	std::thread srv_thread([&] { srv_ctx.run(); });
	std::this_thread::sleep_for(std::chrono::milliseconds(30));

	srv.shutdown();
	srv_thread.join();

	boost::system::error_code ec;
	asio::ip::tcp::socket sock(srv_ctx);
	sock.connect(asio::ip::tcp::endpoint(TEST_ADDR, SHUTDOWN_PORT2), ec);
	REQUIRE(ec);
}

// ── Body size limits ────────────────────────────────────────────

TEST_CASE("oversized body returns 413", "[integration][beast]") {
	static constexpr uint16_t LIMIT_PORT = 19913;

	asio::io_context srv_ctx;
	siesta::beast::ServerBase::Config conf;
	conf.max_body_size = 100;
	conf.read_timeout = std::chrono::milliseconds::zero();
	conf.write_timeout = std::chrono::milliseconds::zero();
	conf.idle_timeout = std::chrono::milliseconds::zero();
	echo_testing::DefaultServer srv(srv_ctx, conf);
	srv.start(TEST_ADDR, LIMIT_PORT);
	std::thread srv_thread([&] { srv_ctx.run(); });
	std::this_thread::sleep_for(std::chrono::milliseconds(30));

	asio::ip::tcp::socket sock(srv_ctx);
	sock.connect(asio::ip::tcp::endpoint(TEST_ADDR, LIMIT_PORT));

	http::request<http::string_body> req{http::verb::post, "/echo", 11};
	req.set(http::field::host, "localhost");
	req.set(http::field::content_type, "application/json");
	req.body() = std::string(200, 'x');
	req.prepare_payload();
	http::write(sock, req);

	boost::beast::flat_buffer buffer;
	http::response<http::string_body> resp;
	http::read(sock, buffer, resp);

	REQUIRE(resp.result() == http::status::payload_too_large);

	sock.close();
	srv.shutdown();
	srv_thread.join();
}

TEST_CASE("body under limit succeeds", "[integration][beast]") {
	static constexpr uint16_t LIMIT_PORT2 = 19914;

	asio::io_context srv_ctx;
	siesta::beast::ServerBase::Config conf;
	conf.max_body_size = 1024;
	conf.read_timeout = std::chrono::milliseconds::zero();
	conf.write_timeout = std::chrono::milliseconds::zero();
	conf.idle_timeout = std::chrono::milliseconds::zero();
	echo_testing::DefaultServer srv(srv_ctx, conf);
	srv.start(TEST_ADDR, LIMIT_PORT2);
	std::thread srv_thread([&] { srv_ctx.run(); });
	std::this_thread::sleep_for(std::chrono::milliseconds(30));

	asio::io_context ctx;
	auto client = std::make_shared<Echo_API::Client>(ctx);
	client->start(TEST_ADDR, LIMIT_PORT2);
	ctx.run();

	Echo_API::EchoResponse body;
	body.message = "small";
	auto future = client->post__echo(body, asio::use_future);
	ctx.restart();
	ctx.run();
	auto outcome = future.get();
	REQUIRE(outcome.has_value());

	client->stop();
	srv.shutdown();
	srv_thread.join();
}

// ── Idle connection timeout ─────────────────────────────────────

TEST_CASE("idle connection times out", "[integration][beast]") {
	static constexpr uint16_t IDLE_PORT = 19915;

	asio::io_context srv_ctx;
	siesta::beast::ServerBase::Config conf;
	conf.idle_timeout = std::chrono::milliseconds(200);
	conf.write_timeout = std::chrono::milliseconds::zero();
	echo_testing::DefaultServer srv(srv_ctx, conf);
	srv.start(TEST_ADDR, IDLE_PORT);
	std::thread srv_thread([&] { srv_ctx.run(); });
	std::this_thread::sleep_for(std::chrono::milliseconds(30));

	asio::ip::tcp::socket sock(srv_ctx);
	sock.connect(asio::ip::tcp::endpoint(TEST_ADDR, IDLE_PORT));

	http::request<http::string_body> req{http::verb::get, "/echo?message=hi", 11};
	req.set(http::field::host, "localhost");
	req.prepare_payload();
	http::write(sock, req);

	boost::beast::flat_buffer buffer;
	http::response<http::string_body> resp;
	http::read(sock, buffer, resp);
	REQUIRE(resp.result() == http::status::ok);

	std::this_thread::sleep_for(std::chrono::milliseconds(400));

	http::request<http::string_body> req2{http::verb::get, "/echo?message=hi", 11};
	req2.set(http::field::host, "localhost");
	req2.prepare_payload();

	boost::system::error_code ec;
	http::write(sock, req2, ec);
	if (!ec) {
		http::response<http::string_body> resp2;
		http::read(sock, buffer, resp2, ec);
	}
	REQUIRE(ec);

	sock.close();
	srv.shutdown();
	srv_thread.join();
}

// ── CORS + Common response headers ─────────────────────────────

TEST_CASE("CORS preflight returns allow headers", "[integration][beast]") {
	static constexpr uint16_t CORS_PORT = 19916;

	asio::io_context srv_ctx;
	siesta::beast::ServerBase::Config conf;
	conf.idle_timeout = std::chrono::milliseconds::zero();
	conf.write_timeout = std::chrono::milliseconds::zero();
	conf.cors_origin = "*";
	echo_testing::DefaultServer srv(srv_ctx, conf);
	srv.start(TEST_ADDR, CORS_PORT);
	std::thread srv_thread([&] { srv_ctx.run(); });
	std::this_thread::sleep_for(std::chrono::milliseconds(30));

	asio::ip::tcp::socket sock(srv_ctx);
	sock.connect(asio::ip::tcp::endpoint(TEST_ADDR, CORS_PORT));

	http::request<http::string_body> req{http::verb::options, "/echo", 11};
	req.set(http::field::host, "localhost");
	req.set(http::field::origin, "http://example.com");
	req.prepare_payload();
	http::write(sock, req);

	boost::beast::flat_buffer buffer;
	http::response<http::string_body> resp;
	http::read(sock, buffer, resp);

	REQUIRE(resp.result() == http::status::no_content);
	REQUIRE(resp[http::field::access_control_allow_origin] == "*");
	REQUIRE(!resp[http::field::access_control_allow_methods].empty());
	REQUIRE(!resp[http::field::access_control_allow_headers].empty());

	sock.close();
	srv.shutdown();
	srv_thread.join();
}

TEST_CASE("responses include Date Server and CORS headers", "[integration][beast]") {
	static constexpr uint16_t HDR_PORT = 19917;

	asio::io_context srv_ctx;
	siesta::beast::ServerBase::Config conf;
	conf.idle_timeout = std::chrono::milliseconds::zero();
	conf.write_timeout = std::chrono::milliseconds::zero();
	conf.cors_origin = "*";
	conf.server_name = "test-siesta";
	echo_testing::DefaultServer srv(srv_ctx, conf);
	srv.start(TEST_ADDR, HDR_PORT);
	std::thread srv_thread([&] { srv_ctx.run(); });
	std::this_thread::sleep_for(std::chrono::milliseconds(30));

	asio::ip::tcp::socket sock(srv_ctx);
	sock.connect(asio::ip::tcp::endpoint(TEST_ADDR, HDR_PORT));

	http::request<http::string_body> req{http::verb::get, "/echo?message=hi", 11};
	req.set(http::field::host, "localhost");
	req.prepare_payload();
	http::write(sock, req);

	boost::beast::flat_buffer buffer;
	http::response<http::string_body> resp;
	http::read(sock, buffer, resp);

	REQUIRE(resp.result() == http::status::ok);
	REQUIRE(resp[http::field::server] == "test-siesta");
	REQUIRE(!resp[http::field::date].empty());
	REQUIRE(resp[http::field::access_control_allow_origin] == "*");

	sock.close();
	srv.shutdown();
	srv_thread.join();
}

// ── TLS ─────────────────────────────────────────────────────────

TEST_CASE("TLS echo round-trip", "[integration][beast][tls]") {
	static constexpr uint16_t TLS_PORT = 19918;

	boost::asio::ssl::context srv_ssl(boost::asio::ssl::context::tls_server);
	srv_ssl.use_certificate_chain_file(SIESTA_TEST_CERT_DIR "/server.pem");
	srv_ssl.use_private_key_file(SIESTA_TEST_CERT_DIR "/server.key", boost::asio::ssl::context::pem);

	asio::io_context srv_ctx;
	siesta::beast::ServerBase::Config conf;
	conf.ssl_ctx = &srv_ssl;
	conf.idle_timeout = std::chrono::milliseconds::zero();
	conf.write_timeout = std::chrono::milliseconds::zero();
	echo_testing::DefaultServer srv(srv_ctx, conf);
	srv.start(TEST_ADDR, TLS_PORT);
	std::thread srv_thread([&] { srv_ctx.run(); });
	std::this_thread::sleep_for(std::chrono::milliseconds(30));

	boost::asio::ssl::context cli_ssl(boost::asio::ssl::context::tls_client);
	cli_ssl.load_verify_file(SIESTA_TEST_CERT_DIR "/server.pem");

	asio::io_context ctx;
	siesta::beast::ClientBase::Config cli_conf;
	cli_conf.ssl_ctx = &cli_ssl;
	auto client = std::make_shared<Echo_API::Client>(ctx, cli_conf);
	REQUIRE(client->is_tls());
	client->start(TEST_ADDR, TLS_PORT);
	ctx.run();

	Echo_API::EchoResponse body;
	body.message = "encrypted";
	auto future = client->post__echo(body, asio::use_future);
	ctx.restart();
	ctx.run();
	auto outcome = future.get();
	REQUIRE(outcome.has_value());

	auto jv = boost::json::parse(outcome.value().body());
	auto result = boost::json::value_to<Echo_API::EchoResponse>(jv);
	REQUIRE(result.message == "encrypted");

	client->stop();
	srv.shutdown();
	srv_thread.join();
}

TEST_CASE("plain client on TLS server fails", "[integration][beast][tls]") {
	static constexpr uint16_t TLS_PORT2 = 19919;

	boost::asio::ssl::context srv_ssl(boost::asio::ssl::context::tls_server);
	srv_ssl.use_certificate_chain_file(SIESTA_TEST_CERT_DIR "/server.pem");
	srv_ssl.use_private_key_file(SIESTA_TEST_CERT_DIR "/server.key", boost::asio::ssl::context::pem);

	asio::io_context srv_ctx;
	siesta::beast::ServerBase::Config conf;
	conf.ssl_ctx = &srv_ssl;
	conf.idle_timeout = std::chrono::milliseconds::zero();
	conf.write_timeout = std::chrono::milliseconds::zero();
	echo_testing::DefaultServer srv(srv_ctx, conf);
	srv.start(TEST_ADDR, TLS_PORT2);
	std::thread srv_thread([&] { srv_ctx.run(); });
	std::this_thread::sleep_for(std::chrono::milliseconds(30));

	asio::ip::tcp::socket sock(srv_ctx);
	sock.connect(asio::ip::tcp::endpoint(TEST_ADDR, TLS_PORT2));

	http::request<http::string_body> req{http::verb::get, "/echo?message=hi", 11};
	req.set(http::field::host, "localhost");
	req.prepare_payload();

	boost::system::error_code ec;
	http::write(sock, req, ec);
	if (!ec) {
		boost::beast::flat_buffer buffer;
		http::response<http::string_body> resp;
		http::read(sock, buffer, resp, ec);
	}
	REQUIRE(ec);

	sock.close();
	srv.shutdown();
	srv_thread.join();
}

// ── Connection pool ─────────────────────────────────────────────

TEST_CASE("connection pool round-robin", "[integration][beast]") {
	asio::io_context ctx;
	siesta::beast::ClientPool<Echo_API::Client> pool(ctx, 4);
	pool.start(TEST_ADDR, TEST_PORT);
	ctx.run();

	for (int i = 0; i < 8; i++) {
		auto msg = "pool_" + std::to_string(i);
		auto future = pool.next()->get__echo(msg, std::nullopt, asio::use_future);
		ctx.restart();
		ctx.run();
		auto outcome = future.get();
		REQUIRE(outcome.has_value());
		auto jv = boost::json::parse(outcome.value().body());
		auto result = boost::json::value_to<Echo_API::EchoResponse>(jv);
		REQUIRE(result.message == msg);
	}

	pool.stop();
}

// ── HEAD request ────────────────────────────────────────────────

TEST_CASE("HEAD returns headers without body", "[integration][beast]") {
	asio::io_context ctx;
	asio::ip::tcp::socket s(ctx);
	s.connect(asio::ip::tcp::endpoint(TEST_ADDR, TEST_PORT));

	http::request<http::string_body> req{http::verb::head, "/echo?message=headtest", 11};
	req.set(http::field::host, "localhost");
	req.prepare_payload();
	http::write(s, req);

	boost::beast::flat_buffer buffer;
	http::response_parser<http::string_body> parser;
	parser.skip(true);
	http::read(s, buffer, parser);
	auto resp = parser.release();

	REQUIRE(resp.result() == http::status::ok);
	REQUIRE(resp.body().empty());
	auto cl = resp[http::field::content_length];
	REQUIRE(!cl.empty());
	REQUIRE(std::stoi(std::string(cl)) > 0);

	s.close();
}

// ── Gzip compression ────────────────────────────────────────────

static std::string gzip_decompress(std::string_view input) {
	z_stream zs{};
	inflateInit2(&zs, 15 + 16);
	zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
	zs.avail_in = static_cast<uInt>(input.size());
	std::string output;
	char buf[4096];
	int ret;
	do {
		zs.next_out = reinterpret_cast<Bytef*>(buf);
		zs.avail_out = sizeof(buf);
		ret = inflate(&zs, Z_NO_FLUSH);
		output.append(buf, sizeof(buf) - zs.avail_out);
	} while (ret == Z_OK);
	inflateEnd(&zs);
	return output;
}

TEST_CASE("gzip compression when Accept-Encoding set", "[integration][beast]") {
	static constexpr uint16_t GZIP_PORT = 19920;

	asio::io_context srv_ctx;
	siesta::beast::ServerBase::Config conf;
	conf.idle_timeout = std::chrono::milliseconds::zero();
	conf.write_timeout = std::chrono::milliseconds::zero();
	conf.compress = siesta::beast::gzip_compress;
	echo_testing::DefaultServer srv(srv_ctx, conf);
	srv.start(TEST_ADDR, GZIP_PORT);
	std::thread srv_thread([&] { srv_ctx.run(); });
	std::this_thread::sleep_for(std::chrono::milliseconds(30));

	asio::ip::tcp::socket sock(srv_ctx);
	sock.connect(asio::ip::tcp::endpoint(TEST_ADDR, GZIP_PORT));

	http::request<http::string_body> req{http::verb::get, "/echo?message=compressed", 11};
	req.set(http::field::host, "localhost");
	req.set(http::field::accept_encoding, "gzip");
	req.prepare_payload();
	http::write(sock, req);

	boost::beast::flat_buffer buffer;
	http::response<http::string_body> resp;
	http::read(sock, buffer, resp);

	REQUIRE(resp.result() == http::status::ok);
	REQUIRE(resp[http::field::content_encoding] == "gzip");

	auto body = gzip_decompress(resp.body());
	auto jv = boost::json::parse(body);
	auto result = boost::json::value_to<Echo_API::EchoResponse>(jv);
	REQUIRE(result.message == "compressed");

	sock.close();
	srv.shutdown();
	srv_thread.join();
}

TEST_CASE("no compression without Accept-Encoding", "[integration][beast]") {
	static constexpr uint16_t GZIP_PORT2 = 19921;

	asio::io_context srv_ctx;
	siesta::beast::ServerBase::Config conf;
	conf.idle_timeout = std::chrono::milliseconds::zero();
	conf.write_timeout = std::chrono::milliseconds::zero();
	conf.compress = siesta::beast::gzip_compress;
	echo_testing::DefaultServer srv(srv_ctx, conf);
	srv.start(TEST_ADDR, GZIP_PORT2);
	std::thread srv_thread([&] { srv_ctx.run(); });
	std::this_thread::sleep_for(std::chrono::milliseconds(30));

	asio::ip::tcp::socket sock(srv_ctx);
	sock.connect(asio::ip::tcp::endpoint(TEST_ADDR, GZIP_PORT2));

	http::request<http::string_body> req{http::verb::get, "/echo?message=plain", 11};
	req.set(http::field::host, "localhost");
	req.prepare_payload();
	http::write(sock, req);

	boost::beast::flat_buffer buffer;
	http::response<http::string_body> resp;
	http::read(sock, buffer, resp);

	REQUIRE(resp.result() == http::status::ok);
	REQUIRE(resp[http::field::content_encoding].empty());

	auto jv = boost::json::parse(resp.body());
	auto result = boost::json::value_to<Echo_API::EchoResponse>(jv);
	REQUIRE(result.message == "plain");

	sock.close();
	srv.shutdown();
	srv_thread.join();
}
