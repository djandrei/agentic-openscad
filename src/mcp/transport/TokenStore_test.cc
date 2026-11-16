#include "mcp/transport/TokenStore.h"

#include <catch2/catch_all.hpp>
#include <string>

TEST_CASE("TokenStore adds tokens with distinct values and increasing ids", "[mcp][tokens]")
{
  mcp::TokenStore store;
  const auto a = store.add("laptop");
  const auto b = store.add("");
  REQUIRE(a.value.size() == 32);
  REQUIRE(a.value.find_first_not_of("0123456789abcdef") == std::string::npos);
  REQUIRE(a.value != b.value);
  REQUIRE(b.id > a.id);
  REQUIRE_FALSE(a.lastUsed.has_value());

  const auto list = store.list();
  REQUIRE(list.size() == 2);
  REQUIRE(list[0].id == a.id);
  REQUIRE(list[0].label == "laptop");
  REQUIRE(list[1].id == b.id);
  REQUIRE(list[1].label.empty());
}

TEST_CASE("TokenStore authenticates only listed values and stamps last use", "[mcp][tokens]")
{
  mcp::TokenStore store;
  const auto a = store.add("a");
  REQUIRE_FALSE(store.authenticate(""));
  REQUIRE_FALSE(store.authenticate("unknown"));
  REQUIRE_FALSE(store.authenticate(a.value.substr(1)));
  REQUIRE_FALSE(store.authenticate(a.value + "0"));
  REQUIRE_FALSE(store.list()[0].lastUsed.has_value());

  REQUIRE(store.authenticate(a.value));
  REQUIRE(store.list()[0].lastUsed.has_value());
}

TEST_CASE("TokenStore remove revokes a token; an empty store accepts nothing", "[mcp][tokens]")
{
  mcp::TokenStore store;
  const auto a = store.add("a");
  const auto b = store.add("b");
  REQUIRE(store.remove(a.id));
  REQUIRE_FALSE(store.remove(a.id));
  REQUIRE_FALSE(store.authenticate(a.value));
  REQUIRE(store.authenticate(b.value));

  REQUIRE(store.remove(b.id));
  REQUIRE(store.list().empty());
  REQUIRE_FALSE(store.authenticate(b.value));
  REQUIRE_FALSE(store.authenticate(""));
}

TEST_CASE("TokenStore addValue keeps a given value and does not duplicate it", "[mcp][tokens]")
{
  mcp::TokenStore store;
  const auto first = store.addValue("fixed-token", "startup");
  const auto again = store.addValue("fixed-token", "other");
  REQUIRE(again.id == first.id);
  REQUIRE(store.list().size() == 1);
  REQUIRE(store.list()[0].label == "startup");
  REQUIRE(store.authenticate("fixed-token"));

  // Ids are never reused after a removal.
  REQUIRE(store.remove(first.id));
  REQUIRE(store.add("").id > first.id);
}
