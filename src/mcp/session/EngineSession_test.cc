#include "mcp/session/EngineSession.h"

#include <catch2/catch_all.hpp>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "core/Builtins.h"
#include "core/parsersettings.h"
#include "mcp/library/Archive.h"
#include "mcp/protocol/Registry.h"
#include "mcp/protocol/Server.h"
#include "mcp/session/MessageLog.h"
#include "mcp/session/SessionManager.h"
#include "mcp/session/SessionObserver.h"
#include "mcp/testing/TempDir.h"
#include "mcp/tools/CompileTools.h"
#include "mcp/tools/CoreTools.h"
#include "mcp/tools/FileTools.h"
#include "mcp/tools/LibraryTools.h"
#include "mcp/tools/ParamTools.h"
#include "mcp/tools/ViewTools.h"
#include "platform/PlatformUtils.h"

using nlohmann::json;
namespace fs = std::filesystem;

namespace {

struct RecordingObserver : mcp::SessionObserver {
  std::vector<std::string> events;
  void fileWritten(mcp::EngineSession&, const mcp::FileChange& change) override
  {
    events.push_back("written:" + change.path);
  }
  void fileRemoved(mcp::EngineSession&, const std::string& path) override
  {
    events.push_back("removed:" + path);
  }
  void fileRenamed(mcp::EngineSession&, const std::string& from, const std::string& to) override
  {
    events.push_back("renamed:" + from + ">" + to);
  }
  void fileExported(mcp::EngineSession&, const std::string& path) override
  {
    events.push_back("exported:" + path);
  }
  void compileFinished(mcp::EngineSession& session, const mcp::CompileRequest& request,
                       const mcp::CompileOutcome& outcome) override
  {
    const char *mode = request.mode == mcp::CompileMode::Check     ? "check"
                       : request.mode == mcp::CompileMode::Preview ? "preview"
                                                                   : "render";
    events.push_back(std::string("compiled:") + mode + ":" + (outcome.ok ? "ok" : "failed") + ":" +
                     (session.lastResult().rootNode ? "tree" : "notree"));
  }
  void cameraChanged(mcp::EngineSession&) override { events.push_back("camera"); }
  void viewOptionsChanged(mcp::EngineSession&) override { events.push_back("viewOptions"); }
  void paramsChanged(mcp::EngineSession&, const std::string& file) override
  {
    events.push_back("params:" + file);
  }
  void librariesChanged(mcp::EngineSession&) override { events.push_back("libraries"); }
};

struct Fixture {
  mcp::testing::TempDir tmp;
  mcp::ToolRegistry tools;
  mcp::ResourceRegistry resources;
  mcp::PromptRegistry prompts;
  RecordingObserver observer;
  std::vector<mcp::ProjectSession *> currentChanges;
  mcp::SessionManager sessions{"viewer", [this](std::shared_ptr<mcp::Project> project) {
                                 auto session = std::make_unique<mcp::EngineSession>(std::move(project));
                                 session->setObserver(&observer);
                                 return session;
                               }};
  mcp::Server server{mcp::ServerInfo{"t", "1", ""}, tools, resources, prompts};

  Fixture()
  {
    static bool initialized = false;
    if (!initialized) {
      std::error_code ec;
      auto exe = fs::read_symlink("/proc/self/exe", ec);
      PlatformUtils::registerApplicationPath((ec ? fs::current_path() : exe.parent_path()).string());
      parser_init();
      Builtins::initialize();
      initialized = true;
    }
    mcp::MessageLog::instance().attach();
    sessions.setCurrentChanged(
      [this](mcp::ProjectSession *session) { currentChanges.push_back(session); });
    mcp::registerCoreTools(tools, sessions);
    mcp::registerFileTools(tools, sessions);
    mcp::registerCompileTools(tools, sessions);
    mcp::registerViewTools(tools, sessions);
    mcp::registerParamTools(tools, sessions);
    mcp::registerLibraryTools(tools, sessions);
    server.handleMessage(json{{"jsonrpc", "2.0"},
                              {"id", 0},
                              {"method", "initialize"},
                              {"params", json{{"protocolVersion", "2025-06-18"}}}});
  }

  json call(const std::string& name, json args = json::object())
  {
    auto response = server.handleMessage(json{{"jsonrpc", "2.0"},
                                              {"id", 1},
                                              {"method", "tools/call"},
                                              {"params", json{{"name", name}, {"arguments", args}}}});
    REQUIRE(response.has_value());
    REQUIRE(response->contains("result"));
    INFO((*response)["result"].dump());
    REQUIRE((*response)["result"]["isError"] == false);
    return (*response)["result"]["structuredContent"];
  }
};

}  // namespace

TEST_CASE("EngineSession reports file, compile, view and parameter events", "[mcp][session][observer]")
{
  Fixture f;
  f.call("project_open", json{{"path", f.tmp.path.string()}});
  REQUIRE(f.currentChanges.size() == 1);
  auto *session = dynamic_cast<mcp::EngineSession *>(f.sessions.current());
  REQUIRE(session != nullptr);
  REQUIRE(session->observer() == &f.observer);

  f.call("file_write", json{{"path", "main.scad"}, {"content", "size = 5; // [1:10]\ncube(size);\n"}});
  f.call("check");
  f.call("preview");
  f.call("render");
  REQUIRE(session->lastMode() == mcp::CompileMode::Render);
  REQUIRE(session->lastResult().geometry != nullptr);
  REQUIRE(session->lastFile() == "main.scad");
  f.call("export", json{{"format", "stl"}, {"path", "out/main.stl"}});
  f.call("camera_set", json{{"preset", "top"}});
  f.call("view_options", json{{"edges", true}});
  f.call("params_set", json{{"values", json{{"size", 7}}}});
  f.call("file_rename", json{{"from", "main.scad"}, {"to", "part.scad"}});
  f.call("file_delete", json{{"path", "part.scad"}});

  REQUIRE(f.observer.events ==
          std::vector<std::string>{
            "written:main.scad", "compiled:preview:ok:tree", "compiled:check:ok:tree",
            "compiled:preview:ok:tree", "compiled:render:ok:tree", "exported:out/main.stl", "camera",
            "viewOptions", "params:main.scad", "renamed:main.scad>part.scad", "removed:part.scad"});
}

TEST_CASE("EngineSession reports library changes and pause state", "[mcp][session][observer]")
{
  Fixture f;
  f.call("project_open", json{{"path", f.tmp.path.string()}});
  auto *session = dynamic_cast<mcp::EngineSession *>(f.sessions.current());
  const std::string zip = mcp::library::createZip({{"lib-main/a.scad", "module a() {}\n"}});
  const fs::path zipPath = f.tmp.path / ".." / ("agentic-lib-" + mcp::generateToken(4) + ".zip");
  {
    std::ofstream out(zipPath, std::ios::binary);
    out << zip;
  }
  f.call("library_install", json{{"library", zipPath.string()}, {"dirName", "lib"}});
  f.call("library_remove", json{{"name", "lib"}});
  fs::remove(zipPath);
  // The write to the main file is followed by the automatic preview.
  REQUIRE(f.observer.events == std::vector<std::string>{"libraries", "libraries"});

  REQUIRE(f.call("status")["projects"][0]["paused"] == false);
  session->setPaused(true);
  REQUIRE(session->paused());
  REQUIRE(f.call("status")["projects"][0]["paused"] == true);
}

TEST_CASE("SessionManager announces the current project", "[mcp][session][observer]")
{
  Fixture f;
  mcp::testing::TempDir other;
  f.call("project_open", json{{"path", f.tmp.path.string()}});
  f.call("project_open", json{{"path", other.path.string()}});
  REQUIRE(f.currentChanges.size() == 2);
  REQUIRE(f.currentChanges[1]->project().root() == fs::canonical(other.path));
  f.call("project_info", json{{"project", f.tmp.path.string()}});
  REQUIRE(f.currentChanges.size() == 3);
  f.call("project_info");  // same project: no announcement
  REQUIRE(f.currentChanges.size() == 3);
  f.sessions.close(f.currentChanges[2]->project().id());
  REQUIRE(f.currentChanges.size() == 4);
  REQUIRE(f.currentChanges[3]->project().root() == fs::canonical(other.path));
}
