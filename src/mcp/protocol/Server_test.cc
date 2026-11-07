#include "mcp/protocol/Server.h"

#include <catch2/catch_all.hpp>
#include <string>

#include "mcp/protocol/JsonRpc.h"
#include "mcp/protocol/Registry.h"

using nlohmann::json;
namespace jsonrpc = mcp::jsonrpc;

namespace {

struct Fixture {
  mcp::ToolRegistry tools;
  mcp::ResourceRegistry resources;
  mcp::PromptRegistry prompts;
  mcp::Server server;

  Fixture()
    : server(mcp::ServerInfo{"test-server", "1.0", "Use the echo tool."}, tools, resources, prompts)
  {
    tools.add(mcp::ToolDef{"echo", "Echo text",
                           json{{"type", "object"},
                                {"properties", json{{"text", json{{"type", "string"}}}}},
                                {"required", json::array({"text"})}},
                           [](const json& args) { return mcp::ToolResult::text(args["text"]); }});
    tools.add(mcp::ToolDef{"boom", "Always fails", json{{"type", "object"}},
                           [](const json&) -> mcp::ToolResult { throw mcp::ToolError("kaboom"); }});
    resources.add(
      mcp::ResourceDef{"test://alpha", "Alpha", "A resource", "text/plain",
                       [] { return mcp::ResourceContent{"test://alpha", "text/plain", "alpha", ""}; }});
    prompts.add(mcp::PromptDef{
      "greet", "Greeting", json::array({json{{"name", "who"}, {"required", true}}}),
      [](const json& args) {
        return json{
          {"description", "greeting"},
          {"messages",
           json::array({json{
             {"role", "user"},
             {"content", json{{"type", "text"}, {"text", "hi " + args["who"].get<std::string>()}}}}})}};
      }});
  }

  json request(const std::string& method, json params = json::object(), json id = 1)
  {
    return json{{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}};
  }

  json call(const json& message)
  {
    auto response = server.handleMessage(message);
    REQUIRE(response.has_value());
    return *response;
  }

  json initialize(const std::string& version = "2025-06-18")
  {
    return call(request("initialize", json{{"protocolVersion", version},
                                           {"capabilities", json::object()},
                                           {"clientInfo", json{{"name", "test"}, {"version", "0"}}}}));
  }
};

}  // namespace

TEST_CASE("Server negotiates the protocol version", "[mcp][server]")
{
  Fixture f;
  json response = f.initialize("2025-03-26");
  REQUIRE(response["result"]["protocolVersion"] == "2025-03-26");
  REQUIRE(response["result"]["serverInfo"]["name"] == "test-server");
  REQUIRE(response["result"]["instructions"] == "Use the echo tool.");
  REQUIRE(response["result"]["capabilities"].contains("tools"));

  Fixture g;
  response = g.initialize("1999-01-01");
  REQUIRE(response["result"]["protocolVersion"] == "2025-06-18");
}

TEST_CASE("Server rejects requests before initialize except ping", "[mcp][server]")
{
  Fixture f;
  json pong = f.call(f.request("ping"));
  REQUIRE(pong["result"].is_object());
  json response = f.call(f.request("tools/list"));
  REQUIRE(response["error"]["code"] == jsonrpc::kInvalidRequest);
}

TEST_CASE("Server lists and calls tools", "[mcp][server]")
{
  Fixture f;
  f.initialize();
  json list = f.call(f.request("tools/list"));
  REQUIRE(list["result"]["tools"].size() == 2);
  REQUIRE(list["result"]["tools"][0]["name"] == "echo");
  REQUIRE(list["result"]["tools"][0]["inputSchema"]["type"] == "object");

  json ok =
    f.call(f.request("tools/call", json{{"name", "echo"}, {"arguments", json{{"text", "hello"}}}}));
  REQUIRE(ok["result"]["isError"] == false);
  REQUIRE(ok["result"]["content"][0]["type"] == "text");
  REQUIRE(ok["result"]["content"][0]["text"] == "hello");
}

TEST_CASE("Server validates tool arguments and names", "[mcp][server]")
{
  Fixture f;
  f.initialize();
  json missing = f.call(f.request("tools/call", json{{"name", "echo"}, {"arguments", json::object()}}));
  REQUIRE(missing["error"]["code"] == jsonrpc::kInvalidParams);
  REQUIRE(missing["error"]["message"].get<std::string>().find("text") != std::string::npos);

  json unknown = f.call(f.request("tools/call", json{{"name", "nope"}}));
  REQUIRE(unknown["error"]["code"] == jsonrpc::kInvalidParams);

  json noName = f.call(f.request("tools/call", json::object()));
  REQUIRE(noName["error"]["code"] == jsonrpc::kInvalidParams);
}

TEST_CASE("Server reports tool failures as tool results", "[mcp][server]")
{
  Fixture f;
  f.initialize();
  json failed = f.call(f.request("tools/call", json{{"name", "boom"}}));
  REQUIRE_FALSE(failed.contains("error"));
  REQUIRE(failed["result"]["isError"] == true);
  REQUIRE(failed["result"]["content"][0]["text"] == "kaboom");
}

TEST_CASE("Server handles unknown methods, notifications, parse errors and batches", "[mcp][server]")
{
  Fixture f;
  f.initialize();
  json unknown = f.call(f.request("does/not/exist"));
  REQUIRE(unknown["error"]["code"] == jsonrpc::kMethodNotFound);

  REQUIRE_FALSE(f.server.handleMessage(json{{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}})
                  .has_value());

  auto parseError = f.server.handleText("{oops");
  REQUIRE(parseError.has_value());
  REQUIRE((*parseError)["id"].is_null());
  REQUIRE((*parseError)["error"]["code"] == jsonrpc::kParseError);

  json wrongVersion = f.call(json{{"jsonrpc", "1.0"}, {"id", 3}, {"method", "ping"}});
  REQUIRE(wrongVersion["error"]["code"] == jsonrpc::kInvalidRequest);

  auto batch = f.server.handleText(
    R"([{"jsonrpc":"2.0","id":10,"method":"ping"},{"jsonrpc":"2.0","method":"notifications/x"}])");
  REQUIRE(batch.has_value());
  REQUIRE(batch->is_array());
  REQUIRE(batch->size() == 1);
  REQUIRE((*batch)[0]["id"] == 10);
}

TEST_CASE("Server serves resources and prompts", "[mcp][server]")
{
  Fixture f;
  f.initialize();
  json list = f.call(f.request("resources/list"));
  REQUIRE(list["result"]["resources"][0]["uri"] == "test://alpha");

  json read = f.call(f.request("resources/read", json{{"uri", "test://alpha"}}));
  REQUIRE(read["result"]["contents"][0]["text"] == "alpha");
  REQUIRE(read["result"]["contents"][0]["mimeType"] == "text/plain");

  json notFound = f.call(f.request("resources/read", json{{"uri", "test://missing"}}));
  REQUIRE(notFound["error"]["code"] == jsonrpc::kResourceNotFound);

  json prompts = f.call(f.request("prompts/list"));
  REQUIRE(prompts["result"]["prompts"][0]["name"] == "greet");
  json prompt =
    f.call(f.request("prompts/get", json{{"name", "greet"}, {"arguments", json{{"who", "bob"}}}}));
  REQUIRE(prompt["result"]["messages"][0]["content"]["text"] == "hi bob");
  json missingArg = f.call(f.request("prompts/get", json{{"name", "greet"}}));
  REQUIRE(missingArg["error"]["code"] == jsonrpc::kInvalidParams);
}

TEST_CASE("Server forwards notifications to the sink", "[mcp][server]")
{
  Fixture f;
  json captured;
  f.server.setNotificationSink([&](const json& message) { captured = message; });
  f.server.notify("notifications/message", json{{"level", "info"}, {"data", "hello"}});
  REQUIRE(captured["method"] == "notifications/message");
  REQUIRE(captured["params"]["data"] == "hello");
  REQUIRE_FALSE(captured.contains("id"));
}
