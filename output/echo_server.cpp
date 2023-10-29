#include <boost/json.hpp>
#include <fmt/format.h>

#include "echo_server.hpp"

namespace asio = ::boost::asio;
namespace http = ::boost::beast::http;
namespace json = ::boost::json;
using namespace std::literals;
using request  = ::boost::beast::http::request<::boost::beast::http::string_body>;
using response = ::boost::beast::http::response<::boost::beast::http::string_body>;

namespace swagger {

const std::unordered_map<std::pair<std::string_view, http::verb>, Server::fnptr_t, ::siesta::beast::__detail::MapHash> g_pathmap = {
	{{"/v1/echo"sv, http::verb::get}, &Server::echo},
};

void Server::handle_request(const request req, Session::Ptr session) {
	fnptr_t fnptr = g_pathmap.at({req.target(), req.method()});
	(this->*fnptr)(req, std::move(session));
}

} // namespace swagger
