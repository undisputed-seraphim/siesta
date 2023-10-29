#pragma once
#include <boost/asio.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "echo_defs.hpp"
#include <siesta/beast/server.hpp>

namespace swagger {

class Server : public ::siesta::beast::ServerBase {
public:
	using ::siesta::beast::ServerBase::Config;
	using ::siesta::beast::ServerBase::ServerBase;
	using ::siesta::beast::ServerBase::Session;

	// Function pointer type of a request endpoint.
	using fnptr_t = void (Server::*)(const Server::request, Server::Session::Ptr);

	void handle_request(const request, Session::Ptr) final;

	// Returns the 'message' to the caller
	// \param[in] headerParam (header) 
	// \param[in] message (query) 
	virtual void echo(const request, Session::Ptr) = 0;

}; // class
} // namespace swagger
