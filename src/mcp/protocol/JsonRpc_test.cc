#include "mcp/protocol/JsonRpc.h"

#include <catch2/catch_all.hpp>
#include <string>

using nlohmann::json;
namespace jsonrpc = mcp::jsonrpc;

TEST_CASE("JSON-RPC parse rejects invalid JSON with a parse error", "[mcp][jsonrpc]")
{
  jsonrpc::Error error;
  REQUIRE_FALSE(jsonrpc::parse("{not json", error).has_value());
  REQUIRE(error.code == jsonrpc::kParseError);
}

TEST_CASE("JSON-RPC parse rejects objects that are not messages", "[mcp][jsonrpc]")
{
  jsonrpc::Error error;
  REQUIRE_FALSE(jsonrpc::parse(R"({"foo": 1})", error).has_value());
  REQUIRE(error.code == jsonrpc::kInvalidRequest);
  REQUIRE_FALSE(jsonrpc::parse(R"({"method": 5, "id": 1})", error).has_value());
  REQUIRE_FALSE(jsonrpc::parse(R"({"method": "x", "id": {}})", error).has_value());
  REQUIRE_FALSE(jsonrpc::parse("[]", error).has_value());
}

TEST_CASE("JSON-RPC parse accepts requests, notifications and batches", "[mcp][jsonrpc]")
{
  jsonrpc::Error error;
  auto request = jsonrpc::parse(R"({"jsonrpc":"2.0","id":1,"method":"ping"})", error);
  REQUIRE(request.has_value());
  REQUIRE(jsonrpc::isRequest(*request));
  REQUIRE_FALSE(jsonrpc::isNotification(*request));

  auto notification = jsonrpc::parse(R"({"jsonrpc":"2.0","method":"notifications/initialized"})", error);
  REQUIRE(notification.has_value());
  REQUIRE(jsonrpc::isNotification(*notification));

  auto batch = jsonrpc::parse(
    R"([{"jsonrpc":"2.0","id":1,"method":"ping"},{"jsonrpc":"2.0","method":"n"}])", error);
  REQUIRE(batch.has_value());
  REQUIRE(batch->is_array());
  REQUIRE(batch->size() == 2);
}

TEST_CASE("JSON-RPC result and error envelopes", "[mcp][jsonrpc]")
{
  const json result = jsonrpc::makeResult(7, json{{"ok", true}});
  REQUIRE(result["jsonrpc"] == "2.0");
  REQUIRE(result["id"] == 7);
  REQUIRE(result["result"]["ok"] == true);

  const json error = jsonrpc::makeError("abc", {jsonrpc::kMethodNotFound, "nope", json{{"x", 1}}});
  REQUIRE(error["id"] == "abc");
  REQUIRE(error["error"]["code"] == jsonrpc::kMethodNotFound);
  REQUIRE(error["error"]["message"] == "nope");
  REQUIRE(error["error"]["data"]["x"] == 1);

  const json plain = jsonrpc::makeError(nullptr, {jsonrpc::kParseError, "bad"});
  REQUIRE_FALSE(plain["error"].contains("data"));
}

TEST_CASE("JSON-RPC serialize emits a single line", "[mcp][jsonrpc]")
{
  const std::string line = jsonrpc::serialize(json{{"text", "two\nlines"}});
  REQUIRE(line.find('\n') == std::string::npos);
  REQUIRE(json::parse(line)["text"] == "two\nlines");
}
