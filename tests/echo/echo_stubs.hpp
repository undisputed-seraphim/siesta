#pragma once
#include "server.hpp"
#include <boost/beast/http.hpp>
#include <string>
#include <string_view>

namespace echo_testing {

inline std::string extract_query_param(std::string_view target, std::string_view key) {
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

inline std::string extract_path_segment(std::string_view target, size_t index) {
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

inline std::string url_decode(std::string_view sv) {
	std::string result;
	result.reserve(sv.size());
	for (size_t i = 0; i < sv.size(); ++i) {
		if (sv[i] == '%' && i + 2 < sv.size()) {
			auto hex = [](char c) -> int {
				if (c >= '0' && c <= '9') return c - '0';
				if (c >= 'A' && c <= 'F') return c - 'A' + 10;
				if (c >= 'a' && c <= 'f') return c - 'a' + 10;
				return -1;
			};
			int h = hex(sv[i + 1]), l = hex(sv[i + 2]);
			if (h >= 0 && l >= 0) {
				result += static_cast<char>((h << 4) | l);
				i += 2;
				continue;
			}
		}
		result += sv[i];
	}
	return result;
}

struct DefaultServer : Echo_API::Server {
	using Echo_API::Server::Server;

	void reply_json(const request& req, Session::Ptr session, std::string body) {
		::boost::beast::http::response<::boost::beast::http::string_body> resp{
			::boost::beast::http::status::ok, req.version()};
		resp.body() = std::move(body);
		resp.set(::boost::beast::http::field::content_type, "application/json");
		resp.prepare_payload();
		session->send(std::move(resp));
	}

	void echo_body(const request& req, Session::Ptr session) {
		reply_json(req, std::move(session), std::string(req.body()));
	}

	void get__echo(const request req, Session::Ptr session) override {
		auto msg = extract_query_param(req.target(), "message");
		reply_json(req, std::move(session), "{\"message\":\"" + msg + "\"}");
	}

	void post__echo(const request req, Session::Ptr s) override { echo_body(req, std::move(s)); }
	void get__echo__id(const request req, Session::Ptr s) override { reply_json(req, std::move(s), "{}"); }
	void delete__echo__id(const request req, Session::Ptr s) override { reply_json(req, std::move(s), "{}"); }
	void get__items(const request req, Session::Ptr s) override { reply_json(req, std::move(s), "{\"id\":1,\"name\":\"stub\"}"); }
	void post__items(const request req, Session::Ptr s) override { echo_body(req, std::move(s)); }
	void get__items_search(const request req, Session::Ptr s) override { reply_json(req, std::move(s), "{}"); }
	void get__items__itemId_tags__tagIndex(const request req, Session::Ptr s) override { reply_json(req, std::move(s), "{}"); }
	void put__items__id(const request req, Session::Ptr s) override { echo_body(req, std::move(s)); }
	void post__items_detailed(const request req, Session::Ptr s) override { echo_body(req, std::move(s)); }
	void post__outcome(const request req, Session::Ptr s) override { echo_body(req, std::move(s)); }
};

} // namespace echo_testing
