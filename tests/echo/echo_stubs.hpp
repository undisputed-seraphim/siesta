#pragma once
#include "server.hpp"
#include "openapi_defs.hpp"
#include <boost/beast/http.hpp>
#include <boost/json.hpp>
#include <string>
#include <string_view>
#include <variant>

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

	void get__items(const request req, Session::Ptr s) override {
		auto limit_str = extract_query_param(req.target(), "limit");
		int limit = limit_str.empty() ? 3 : std::stoi(limit_str);
		boost::json::array arr;
		for (int i = 0; i < limit; i++) {
			Echo_API::Item item;
			item.id = i;
			item.name = "item_" + std::to_string(i);
			item.tags = std::vector<std::string>{"tag_a", "tag_b"};
			item.status = Echo_API::ItemStatus::active;
			arr.push_back(boost::json::value_from(item));
		}
		reply_json(req, std::move(s), boost::json::serialize(arr));
	}

	void post__items(const request req, Session::Ptr s) override {
		auto jv = boost::json::parse(req.body());
		auto item = boost::json::value_to<Echo_API::Item>(jv);
		item.name = "processed:" + item.name;
		item.tags.push_back("server-added");
		reply_json(req, std::move(s), boost::json::serialize(boost::json::value_from(item)));
	}

	void get__items_search(const request req, Session::Ptr s) override {
		auto cat = extract_query_param(req.target(), "category");
		auto q = url_decode(extract_query_param(req.target(), "q"));
		reply_json(req, std::move(s), "{\"message\":\"cat=" + cat + ",q=" + q + "\"}");
	}

	void get__items__itemId_tags__tagIndex(const request req, Session::Ptr s) override {
		auto itemId = extract_path_segment(req.target(), 1);
		auto tagIdx = extract_path_segment(req.target(), 3);
		reply_json(req, std::move(s), "{\"message\":\"" + itemId + ":" + tagIdx + "\"}");
	}

	void put__items__id(const request req, Session::Ptr s) override {
		auto id = extract_path_segment(req.target(), 1);
		auto jv = boost::json::parse(req.body());
		auto item = boost::json::value_to<Echo_API::Item>(jv);
		item.description = "updated:" + id;
		reply_json(req, std::move(s), boost::json::serialize(boost::json::value_from(item)));
	}

	void post__items_detailed(const request req, Session::Ptr s) override {
		auto jv = boost::json::parse(req.body());
		auto item = boost::json::value_to<Echo_API::DetailedItem>(jv);
		item.detail = "processed:" + item.detail;
		item.rating += 1.0;
		reply_json(req, std::move(s), boost::json::serialize(boost::json::value_from(item)));
	}

	void post__outcome(const request req, Session::Ptr s) override {
		auto jv = boost::json::parse(req.body());
		auto outcome = boost::json::value_to<Echo_API::Outcome>(jv);
		std::visit([](auto& alt) {
			using T = std::decay_t<decltype(alt)>;
			if constexpr (std::is_same_v<T, Echo_API::EchoResponse>)
				alt.message = "visited:" + alt.message;
			else if constexpr (std::is_same_v<T, Echo_API::Error>)
				alt.message = "visited:" + alt.message;
		}, outcome);
		reply_json(req, std::move(s), boost::json::serialize(boost::json::value_from(outcome)));
	}

	void handle_ws_ws_echo(
		::boost::beast::websocket::stream<
			::siesta::beast::ServerBase::stream_type&>& ws,
		const request req,
		Session::Ptr session) override {
		struct Echo : std::enable_shared_from_this<Echo> {
			::boost::beast::websocket::stream<
				::siesta::beast::ServerBase::stream_type&>* w;
			::boost::beast::flat_buffer buf;
			Session::Ptr session;
			Echo(decltype(w) w_, decltype(buf) b, decltype(session) s)
				: w(w_), buf(std::move(b)), session(std::move(s)) {}
			void start() {
				w->async_read(buf,
					[self = shared_from_this()](ec_t ec, std::size_t) {
						if (ec) return;
						self->w->text(self->w->got_text());
						self->w->async_write(self->buf.data(),
							[self](ec_t ec, std::size_t) {
								if (ec) return;
								self->buf.clear();
								self->start();
							});
					});
			}
		};
		auto e = std::make_shared<Echo>(&ws, ::boost::beast::flat_buffer{}, session);
		e->start();
	}
};

} // namespace echo_testing
