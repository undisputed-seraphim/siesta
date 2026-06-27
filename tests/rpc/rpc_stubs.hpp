#pragma once

#include "server.hpp"

#include <boost/beast/http.hpp>
#include <boost/json.hpp>
#include <boost/json/monotonic_resource.hpp>
#include <memory>
#include <string>

namespace http = boost::beast::http;

namespace rpc_testing {

inline void reply_json(const petstore::Server::request& req,
                        petstore::Server::Session::Ptr session,
                        const std::string& body) {
	auto resp = session->make_response(200, body);
	session->send(std::move(resp));
}

struct StubServer : petstore::Server {
	using petstore::Server::Server;

	void PetStore_CreatePet(const request req, Session::Ptr s) override {
		auto sp = s->json_storage();
		auto jv = boost::json::parse(req.body(), sp);
		auto pet = boost::json::value_to<petstore::Pet>(jv);
		pet.name = "created:" + pet.name;
		pet.kind = "created:" + pet.kind;
		reply_json(req, std::move(s), boost::json::serialize(boost::json::value_from(pet, sp)));
	}

	void PetStore_GetPet(const request req, Session::Ptr s) override {
		auto sp = s->json_storage();
		auto jv = boost::json::parse(req.body(), sp);
		auto req_pet = boost::json::value_to<petstore::GetPetRequest>(jv);
		petstore::Pet pet;
		pet.name = req_pet.name;
		pet.kind = "found";
		reply_json(req, std::move(s), boost::json::serialize(boost::json::value_from(pet, sp)));
	}
};

} // namespace rpc_testing
