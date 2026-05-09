#include "client.hpp"

#include <boost/asio/use_future.hpp>
#include <boost/json.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>

namespace {

std::string echo_host() {
	const char* h = std::getenv("ECHO_HOST");
	return h ? h : "127.0.0.1";
}
uint16_t echo_port() {
	const char* p = std::getenv("ECHO_PORT");
	return p ? static_cast<uint16_t>(std::stoi(p)) : 9910;
}

Echo_API::EchoResponse call_echo(std::shared_ptr<Echo_API::Client> client,
                            boost::asio::io_context& ctx,
                            const std::string& msg,
                            std::optional<std::string> header = std::nullopt) {
	auto future = client->get__echo(msg, header, boost::asio::use_future);
	ctx.restart();
	ctx.run();
	auto outcome = future.get();
	REQUIRE(outcome.has_value());
	auto jv = boost::json::parse(outcome.value().body());
	return boost::json::value_to<Echo_API::EchoResponse>(jv);
}

} // namespace

TEST_CASE("echo basic", "[integration]") {
	boost::asio::io_context ctx;
	auto client = std::make_shared<Echo_API::Client>(ctx);
	client->start(boost::asio::ip::make_address(echo_host()), echo_port());
	ctx.run();
	auto resp = call_echo(client, ctx, "hello");
	REQUIRE(resp.message == "hello");
}

TEST_CASE("echo empty message", "[integration]") {
	boost::asio::io_context ctx;
	auto client = std::make_shared<Echo_API::Client>(ctx);
	client->start(boost::asio::ip::make_address(echo_host()), echo_port());
	ctx.run();
	auto resp = call_echo(client, ctx, "");
	REQUIRE(resp.message == "");
}

TEST_CASE("echo special chars", "[integration]") {
	boost::asio::io_context ctx;
	auto client = std::make_shared<Echo_API::Client>(ctx);
	client->start(boost::asio::ip::make_address(echo_host()), echo_port());
	ctx.run();
	auto resp = call_echo(client, ctx, "hello world");
	REQUIRE(resp.message == "hello world");
}

TEST_CASE("echo with header param", "[integration]") {
	boost::asio::io_context ctx;
	auto client = std::make_shared<Echo_API::Client>(ctx);
	client->start(boost::asio::ip::make_address(echo_host()), echo_port());
	ctx.run();
	auto resp = call_echo(client, ctx, "test", std::string("custom-value"));
	REQUIRE(resp.message == "test");
}
