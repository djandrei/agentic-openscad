#include "mcp/tools/CoreTools.h"

#include <catch2/catch_all.hpp>
#include <memory>
#include <string>

#include "mcp/protocol/Registry.h"
#include "mcp/protocol/Server.h"
#include "mcp/session/SessionManager.h"
#include "mcp/testing/TempDir.h"

using nlohmann::json;

namespace {

class FakeSession : public mcp::ProjectSession
{
public:
  using ProjectSession::ProjectSession;
  json status() const override { return json{{"mode", "fake"}, {"busy", false}}; }
};

struct Fixture {
  mcp::ToolRegistry tools;
  mcp::ResourceRegistry resources;
  mcp::PromptRegistry prompts;
  mcp::SessionManager sessions{"fake", [](std::shared_ptr<mcp::Project> project) {
                                 return std::make_unique<FakeSession>(std::move(project));
                               }};
  mcp::Server server{mcp::ServerInfo{"t", "1", mcp::serverInstructions()}, tools, resources, prompts};

  Fixture()
  {
    mcp::registerCoreTools(tools, sessions);
    server.handleMessage(json{{"jsonrpc", "2.0"},
                              {"id", 0},
                              {"method", "initialize"},
                              {"params", json{{"protocolVersion", "2025-06-18"}}}});
  }

  json callTool(const std::string& name, json args = json::object())
  {
    auto response = server.handleMessage(json{{"jsonrpc", "2.0"},
                                              {"id", 1},
                                              {"method", "tools/call"},
                                              {"params", json{{"name", name}, {"arguments", args}}}});
    REQUIRE(response.has_value());
    REQUIRE(response->contains("result"));
    return (*response)["result"];
  }
};

}  // namespace

TEST_CASE("Core tools report version and status without projects", "[mcp][tools]")
{
  Fixture f;
  json version = f.callTool("version");
  REQUIRE(version["isError"] == false);
  REQUIRE_FALSE(version["structuredContent"]["openscad"].get<std::string>().empty());
  REQUIRE(version["structuredContent"]["mode"] == "fake");
  REQUIRE(version["structuredContent"]["protocolVersion"] == "2025-06-18");

  json status = f.callTool("status");
  REQUIRE(status["structuredContent"]["projects"].empty());
  REQUIRE(status["structuredContent"]["current"].is_null());

  json info = f.callTool("project_info");
  REQUIRE(info["isError"] == true);
}

TEST_CASE("Core tools open, list and describe projects", "[mcp][tools]")
{
  Fixture f;
  mcp::testing::TempDir tmp;
  json opened = f.callTool("project_open", json{{"path", tmp.path.string()}});
  REQUIRE(opened["isError"] == false);
  const std::string id = opened["structuredContent"]["id"];
  REQUIRE(id == std::filesystem::canonical(tmp.path).string());
  REQUIRE(opened["structuredContent"]["main"] == "main.scad");
  REQUIRE(opened["structuredContent"]["mode"] == "fake");

  json list = f.callTool("project_list");
  REQUIRE(list["structuredContent"]["projects"].size() == 1);
  REQUIRE_FALSE(list["structuredContent"]["projects"][0].contains("config"));

  json info = f.callTool("project_info", json{{"project", id}});
  REQUIRE(info["structuredContent"]["config"]["main"] == "main.scad");

  json status = f.callTool("status");
  REQUIRE(status["structuredContent"]["current"] == id);

  json missing = f.callTool("project_open", json{{"path", (tmp.path / "nope").string()}});
  REQUIRE(missing["isError"] == true);

  // additionalProperties: false makes this a JSON-RPC error, not a tool result
  auto response =
    f.server.handleMessage(json{{"jsonrpc", "2.0"},
                                {"id", 2},
                                {"method", "tools/call"},
                                {"params", json{{"name", "version"}, {"arguments", json{{"x", 1}}}}}});
  REQUIRE((*response).contains("error"));
}
