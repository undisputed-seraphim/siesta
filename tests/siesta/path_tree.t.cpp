// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_all.hpp>
#include <siesta/path_tree.hpp>

const std::string simple1 = "/api/v3/ping";
const std::string simple2 = "/api/v3/time";
const std::string simple3 = "/api/v3/ticker";
const std::string simple4 = "/api/v3/ticker/price";
const std::string simple5 = "/api/v3/ticker/bookTicker";

const std::string wildcard1 = "/api/v4/*/ping";
const std::string wildcard1_match = "/api/v4/alpha/ping";
const std::string wildcard2 = "/api/v4/*/time";
const std::string wildcard2_match = "/api/v4/beta/time";
const std::string wildcard3 = "/api/v4/*/ticker";
const std::string wildcard4 = "/api/v4/*/ticker/price";
const std::string wildcard5 = "/api/v4/*/ticker/bookTicker";
const std::string wildcard6 = "/api/v4/*/ticker/*/account";
const std::string wildcard6_match = "/api/v4/alpha/ticker/beta/account";

TEST_CASE("insert and contains", "[path_tree]") {
	siesta::node<int> node;
	REQUIRE(node.key_token().empty());

	node.insert(simple1);
	node.insert(simple2);
	node.insert(simple3);
	node.insert(simple4);
	node.insert(simple5);
    // We inserted 5 paths, however only 4 of them are leaf nodes
	REQUIRE(node.size() == 4);

	REQUIRE(node.contains(simple1));
	REQUIRE(node.contains(simple2));
	REQUIRE(node.contains(simple3));
	REQUIRE(node.contains(simple4));
	REQUIRE(node.contains(simple5));

    node.insert(simple5, 777);
    auto opt = node.const_at(simple5);
	REQUIRE(opt.has_value());
	REQUIRE(opt.value() == 777);

	REQUIRE(node.contains(simple5, 777));
}

TEST_CASE("init-construction", "[path_tree]") {
	siesta::node<int> node = {
		{simple1, 1},
		{simple2, 2},
		{simple3, 3},
		{simple4, 4},
		{simple5, 5},
	};

    // We inserted 5 paths, however only 4 of them are leaf nodes
	REQUIRE(node.size() == 4);

	REQUIRE(node.contains(simple1, 1));
	REQUIRE(node.contains(simple2, 2));
	REQUIRE(node.contains(simple3, 3));
	REQUIRE(node.contains(simple4, 4));
	REQUIRE(node.contains(simple5, 5));
}

TEST_CASE("wildcard", "[path_tree]") {
	siesta::node<int> node;
	REQUIRE(node.key_token().empty());

	node.insert(wildcard1);
	node.insert(wildcard2);
	node.insert(wildcard3);
	node.insert(wildcard4);
	node.insert(wildcard5);
    // We inserted 5 paths, however only 4 of them are leaf nodes
	REQUIRE(node.size() == 4);

	REQUIRE(node.contains(wildcard1));
	REQUIRE(node.contains(wildcard1_match));
	REQUIRE(node.contains(wildcard2));
	REQUIRE(node.contains(wildcard2_match));
	REQUIRE_FALSE(node.contains(wildcard6));
	REQUIRE_FALSE(node.contains(wildcard6_match));

	node.insert(wildcard6);
	REQUIRE(node.contains(wildcard6));
	REQUIRE(node.contains(wildcard6_match));
}

#include <unordered_map>

TEST_CASE("complex-objects", "[path_tree]") {
	using mapped_type = std::unordered_map<int64_t, std::string>;
	siesta::node<mapped_type> node = {
		{simple1, {
			{11, "11"},
		}},
		{simple2, {
			{21, "21"},
			{22, "22"},
		}},
		{simple3, {
			{31, "31"},
			{32, "32"},
			{33, "33"},
		}},
		{simple4, {
			{41, "41"},
			{42, "42"},
			{43, "43"},
			{44, "44"},
		}},
		{simple5, {
			{51, "51"},
			{52, "52"},
			{53, "53"},
			{54, "54"},
			{55, "55"},
		}},
	};

    // We inserted 5 paths, however only 4 of them are leaf nodes
	REQUIRE(node.size() == 4);

	const mapped_type test1 = {{11, "11"}};
	REQUIRE(node.contains(simple1, test1));

	const auto& test4 = node.const_at(simple4);
	REQUIRE(test4.has_value());
	REQUIRE(test4.value().get().size() == 4);
}