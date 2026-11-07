#include "mcp/tools/FileTools.h"

#include <catch2/catch_all.hpp>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "mcp/protocol/Registry.h"
#include "mcp/protocol/Server.h"
#include "mcp/session/SessionManager.h"
#include "mcp/testing/TempDir.h"
#include "mcp/tools/CoreTools.h"

using nlohmann::json;

namespace {

// Session that records hooks.
class RecordingSession : public mcp::ProjectSession
{
public:
  using ProjectSession::ProjectSession;
  json status() const override { return json{{"mode", "fake"}}; }
  void fileWritten(const mcp::FileChange& change) override { written.push_back(change.path); }
  void fileRemoved(const std::string& path) override { removed.push_back(path); }
  void fileRenamed(const std::string& from, const std::string& to) override
  {
    renamed.push_back(from + "->" + to);
  }

  std::vector<std::string> written, removed, renamed;
};

struct Fixture {
  mcp::testing::TempDir tmp;
  mcp::ToolRegistry tools;
  mcp::ResourceRegistry resources;
  mcp::PromptRegistry prompts;
  RecordingSession *session = nullptr;
  mcp::SessionManager sessions{"fake", [this](std::shared_ptr<mcp::Project> project) {
                                 auto s = std::make_unique<RecordingSession>(std::move(project));
                                 session = s.get();
                                 return s;
                               }};
  mcp::Server server{mcp::ServerInfo{"t", "1", ""}, tools, resources, prompts};

  Fixture()
  {
    mcp::registerCoreTools(tools, sessions);
    mcp::registerFileTools(tools, sessions);
    server.handleMessage(json{{"jsonrpc", "2.0"},
                              {"id", 0},
                              {"method", "initialize"},
                              {"params", json{{"protocolVersion", "2025-06-18"}}}});
    sessions.open(tmp.path);
  }

  json call(const std::string& name, json args = json::object())
  {
    auto response = server.handleMessage(json{{"jsonrpc", "2.0"},
                                              {"id", 1},
                                              {"method", "tools/call"},
                                              {"params", json{{"name", name}, {"arguments", args}}}});
    REQUIRE(response.has_value());
    if (response->contains("error")) return *response;
    return (*response)["result"];
  }
};

}  // namespace

TEST_CASE("File tools write, read and list through the session", "[mcp][tools][files]")
{
  Fixture f;
  json written = f.call("file_write", json{{"path", "main.scad"}, {"content", "cube(1);\n"}});
  REQUIRE(written["isError"] == false);
  REQUIRE(written["structuredContent"]["created"] == true);
  const std::string version = written["structuredContent"]["version"];
  REQUIRE(f.session->written == std::vector<std::string>{"main.scad"});

  json read = f.call("file_read", json{{"path", "main.scad"}});
  REQUIRE(read["structuredContent"]["content"] == "cube(1);\n");
  REQUIRE(read["structuredContent"]["version"] == version);

  json list = f.call("file_list");
  REQUIRE(list["structuredContent"]["files"].size() == 1);
  REQUIRE(list["structuredContent"]["files"][0]["path"] == "main.scad");

  json both = f.call("file_write", json{{"path", "main.scad"},
                                        {"content", "x"},
                                        {"edits", json::array({json{{"old", "a"}, {"new", "b"}}})}});
  REQUIRE(both["isError"] == true);
  json neither = f.call("file_write", json{{"path", "main.scad"}});
  REQUIRE(neither["isError"] == true);
}

TEST_CASE("File tools detect conflicts", "[mcp][tools][files]")
{
  Fixture f;
  json first = f.call("file_write", json{{"path", "main.scad"}, {"content", "a\n"}});
  const std::string v1 = first["structuredContent"]["version"];
  json second =
    f.call("file_write", json{{"path", "main.scad"}, {"content", "b\n"}, {"baseVersion", v1}});
  REQUIRE(second["isError"] == false);
  json stale =
    f.call("file_write", json{{"path", "main.scad"}, {"content", "c\n"}, {"baseVersion", v1}});
  REQUIRE(stale["isError"] == true);
  REQUIRE(stale["structuredContent"]["currentVersion"] == second["structuredContent"]["version"]);

  json edited = f.call("file_write", json{{"path", "main.scad"},
                                          {"edits", json::array({json{{"old", "b"}, {"new", "B"}}})},
                                          {"baseVersion", second["structuredContent"]["version"]}});
  REQUIRE(edited["isError"] == false);
  REQUIRE(f.call("file_read", json{{"path", "main.scad"}})["structuredContent"]["content"] == "B\n");
}

TEST_CASE("File tools rename and delete with hooks", "[mcp][tools][files]")
{
  Fixture f;
  f.call("file_write", json{{"path", "a.scad"}, {"content", "a"}});
  json renamed = f.call("file_rename", json{{"from", "a.scad"}, {"to", "parts/b.scad"}});
  REQUIRE(renamed["isError"] == false);
  REQUIRE(f.session->renamed == std::vector<std::string>{"a.scad->parts/b.scad"});
  json deleted = f.call("file_delete", json{{"path", "parts/b.scad"}});
  REQUIRE(deleted["isError"] == false);
  REQUIRE(f.session->removed == std::vector<std::string>{"parts/b.scad"});
  json missing = f.call("file_delete", json{{"path", "parts/b.scad"}});
  REQUIRE(missing["isError"] == true);
  json outside = f.call("file_read", json{{"path", "../etc/passwd"}});
  REQUIRE(outside["isError"] == true);
  json badArg = f.call("file_read", json{{"path", 5}});
  REQUIRE(badArg.contains("error"));
}
