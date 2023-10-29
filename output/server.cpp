#include <boost/json/fwd.hpp>
#include <boost/json/serialize.hpp>
#include <iostream>

#include "echo_defs.hpp"
#include "echo_server.hpp"

namespace json = ::boost::json;
namespace http = ::boost::beast::http;

class ServerImpl : public swagger::Server {
public:
	// Inherit all constructors
	using swagger::Server::Server;

	void echo(const request req, Session::Ptr session) override {
		std::cout << req.target() << std::endl;
		for (const auto& field : req) {
			std::cout << "field: " << field.name() << " value:" << field.value() << std::endl;
		}

		swagger::EchoResponse echo;
		echo.message = "response msg";
		response& res = session->get_response();
		res = response{http::status::ok, req.version()};
		res.set(http::field::server, "1.0");
		res.set(http::field::content_type, "application/json");
		res.body().assign(json::serialize(json::value_from(echo)));
		res.prepare_payload();
		session->write();
	}
};

int main(int argc, char* argv[]) try {
	const auto address = boost::asio::ip::address::from_string("127.0.0.1");
	::boost::asio::io_context ctx;
	auto guard = ::boost::asio::make_work_guard(ctx);
	std::thread t([&ctx]() { ctx.run(); });

	ServerImpl server(ctx);
	server.start(ctx, ServerImpl::protocol::endpoint(address, 8080));

	guard.reset();
	t.join();
} catch (const std::exception& e) {
	std::cerr << "Caught exception " << e.what() << std::endl;
}