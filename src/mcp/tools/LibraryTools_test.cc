#include "mcp/tools/LibraryTools.h"

#include <catch2/catch_all.hpp>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "core/Builtins.h"
#include "core/parsersettings.h"
#include "mcp/headless/HeadlessSession.h"
#include "mcp/library/Archive.h"
#include "mcp/library/Registry.h"
#include "mcp/protocol/Registry.h"
#include "mcp/protocol/Server.h"
#include "mcp/session/MessageLog.h"
#include "mcp/session/SessionManager.h"
#include "mcp/testing/TempDir.h"
#include "mcp/tools/CompileTools.h"
#include "mcp/tools/CoreTools.h"
#include "mcp/tools/FileTools.h"
#include "platform/PlatformUtils.h"

using nlohmann::json;
namespace fs = std::filesystem;

namespace {

struct Fixture {
  mcp::testing::TempDir tmp;
  mcp::testing::TempDir downloads;
  mcp::ToolRegistry tools;
  mcp::ResourceRegistry resources;
  mcp::PromptRegistry prompts;
  mcp::SessionManager sessions{"headless", [](std::shared_ptr<mcp::Project> project) {
                                 return std::make_unique<mcp::HeadlessSession>(std::move(project));
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
    mcp::registerCoreTools(tools, sessions);
    mcp::registerFileTools(tools, sessions);
    mcp::registerCompileTools(tools, sessions);
    mcp::registerLibraryTools(tools, sessions);
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

  json ok(const std::string& name, json args = json::object())
  {
    json result = call(name, args);
    INFO(result.dump());
    REQUIRE(result["isError"] == false);
    return result["structuredContent"];
  }

  // A zip laid out like a forge archive: one top-level folder.
  fs::path makeLibraryZip()
  {
    const std::string zip = mcp::library::createZip({
      {"mylib-main/shapes.scad",
       "module box(size = 10, center = false) { cube(size, center); }\n"
       "function twice(x) = 2 * x;\n"},
      {"mylib-main/README.md", "# mylib\n"},
      {"mylib-main/sub/extra.scad", "module extra() { sphere(1); }\n"},
    });
    const fs::path path = downloads.path / "mylib.zip";
    std::ofstream out(path, std::ios::binary);
    out << zip;
    return path;
  }
};

}  // namespace

TEST_CASE("archive candidates are derived per forge", "[mcp][library]")
{
  auto github = mcp::library::archiveCandidates("https://github.com/BelfrySCAD/BOSL2.git", "master");
  REQUIRE(github.size() == 2);
  REQUIRE(github[0] == "https://codeload.github.com/BelfrySCAD/BOSL2/zip/refs/heads/master");
  REQUIRE(github[1] == "https://codeload.github.com/BelfrySCAD/BOSL2/zip/refs/tags/master");
  auto defaults = mcp::library::archiveCandidates("https://github.com/nophead/NopSCADlib/", "");
  REQUIRE(defaults.size() == 4);
  REQUIRE(defaults[0].find("/heads/main") != std::string::npos);
  REQUIRE(defaults[2].find("/heads/master") != std::string::npos);
  auto codeberg =
    mcp::library::archiveCandidates("https://codeberg.org/adrien-delhorme/openscad-doll-house", "main");
  REQUIRE(codeberg == std::vector<std::string>{
                        "https://codeberg.org/adrien-delhorme/openscad-doll-house/archive/main.zip"});
  REQUIRE(mcp::library::archiveCandidates("https://example.org/some/deep/path", "main").empty());
  REQUIRE(mcp::library::repositoryName("https://github.com/cfinke/LEGO.scad.git") == "LEGO.scad");
}

TEST_CASE("bundled registry parses and lists well-known libraries", "[mcp][library]")
{
  auto entries =
    mcp::library::parseRegistry(json::parse(mcp::library::bundledRegistryJson()), "bundled");
  REQUIRE(entries.size() >= 29);
  const auto *bosl2 = mcp::library::findEntry(entries, "bosl2");
  REQUIRE(bosl2 != nullptr);
  REQUIRE(bosl2->repository == "https://github.com/BelfrySCAD/BOSL2");
  REQUIRE(bosl2->usage.find("BOSL2/std.scad") != std::string::npos);
  REQUIRE(mcp::library::findEntry(entries, "MCAD")->bundled);
}

TEST_CASE("extractZip strips the top-level folder and rejects unsafe paths", "[mcp][library]")
{
  mcp::testing::TempDir tmp;
  const std::string zip =
    mcp::library::createZip({{"lib-1.0/a.scad", "a"}, {"lib-1.0/dir/b.scad", "b"}});
  auto files = mcp::library::extractZip(zip, tmp.path / "lib");
  REQUIRE(files == std::vector<std::string>{"a.scad", "dir/b.scad"});
  REQUIRE(fs::exists(tmp.path / "lib" / "dir" / "b.scad"));

  const std::string flat = mcp::library::createZip({{"a.scad", "a"}, {"other/b.scad", "b"}});
  REQUIRE(mcp::library::extractZip(flat, tmp.path / "flat") ==
          std::vector<std::string>{"a.scad", "other/b.scad"});

  const std::string evil = mcp::library::createZip({{"../evil.scad", "x"}});
  REQUIRE_THROWS_AS(mcp::library::extractZip(evil, tmp.path / "evil"), mcp::ToolError);
  REQUIRE_THROWS_AS(mcp::library::extractZip("not a zip", tmp.path / "bad"), mcp::ToolError);
}

TEST_CASE("library tools install from an archive and make it usable", "[mcp][library]")
{
  Fixture f;
  const fs::path zip = f.makeLibraryZip();

  json catalog = f.ok("library_catalog", json{{"query", "bosl"}});
  REQUIRE(catalog["libraries"].size() >= 2);
  REQUIRE(catalog["libraries"][0]["installed"] == false);
  REQUIRE(f.ok("library_installed")["libraries"].empty());

  json installed =
    f.ok("library_install", json{{"library", "file://" + zip.string()}, {"dirName", "mylib"}});
  REQUIRE(installed["name"] == "mylib");
  REQUIRE(installed["method"] == "archive");
  REQUIRE(installed["files"] == 3);
  REQUIRE(fs::exists(f.tmp.path / "libraries" / "mylib" / "shapes.scad"));
  REQUIRE(fs::exists(f.tmp.path / "libraries" / "mylib" / "sub" / "extra.scad"));

  json list = f.ok("library_installed");
  REQUIRE(list["libraries"].size() == 1);
  REQUIRE(list["libraries"][0]["name"] == "mylib");
  REQUIRE(list["libraries"][0]["present"] == true);

  json files = f.ok("library_files", json{{"name", "mylib"}, {"describe", true}});
  REQUIRE(files["files"].size() == 3);
  REQUIRE(files["described"].size() == 2);
  REQUIRE(files["described"][0]["path"] == "shapes.scad");
  REQUIRE(files["described"][0]["modules"][0]["name"] == "box");
  REQUIRE(files["described"][0]["modules"][0]["parameters"] == json::array({"size", "center"}));
  REQUIRE(files["described"][0]["functions"][0]["name"] == "twice");
  json filtered = f.ok("library_files", json{{"name", "mylib"}, {"filter", "extra"}});
  REQUIRE(filtered["files"].size() == 1);

  // The project's libraries folder is on the search path: use <mylib/...> works.
  f.call("file_write", json{{"path", "main.scad"}, {"content", "use <mylib/shapes.scad>\nbox(7);\n"}});
  json preview = f.ok("preview");
  REQUIRE(preview["ok"] == true);
  REQUIRE(preview["warnings"] == 0);
  json rendered = f.ok("render");
  REQUIRE(rendered["geometry"]["boundingBox"]["max"] == json::array({7, 7, 7}));

  // Reinstall replaces, remove cleans up.
  REQUIRE(f.ok("library_install", json{{"library", zip.string()}, {"dirName", "mylib"}})["files"] == 3);
  json removed = f.ok("library_remove", json{{"name", "mylib"}});
  REQUIRE(removed["removed"] == true);
  REQUIRE_FALSE(fs::exists(f.tmp.path / "libraries" / "mylib"));
  REQUIRE(f.ok("library_installed")["libraries"].empty());
  REQUIRE(f.call("library_remove", json{{"name", "mylib"}})["isError"] == true);
  REQUIRE(f.call("library_install", json{{"library", "MCAD"}})["isError"] == true);  // bundled
  REQUIRE(f.call("library_files", json{{"name", "../x"}})["isError"] == true);
}

TEST_CASE("project registries extend the catalog", "[mcp][library]")
{
  Fixture f;
  {
    std::ofstream out(f.tmp.path / "registry.json");
    out
      << R"({"name": "mine", "libraries": [{"name": "gears", "repository": "https://example.org/g/gears.git", "description": "custom gears"}]})";
  }
  json added = f.ok("library_registry_add", json{{"location", "registry.json"}});
  REQUIRE(added["entries"] == 1);
  REQUIRE(added["registries"].size() == 2);
  json catalog = f.ok("library_catalog", json{{"query", "gears"}});
  bool found = false;
  for (const auto& item : catalog["libraries"]) {
    if (item["name"] == "gears" && item["registry"] == "mine") found = true;
  }
  REQUIRE(found);
  REQUIRE(f.call("library_registry_add", json{{"location", "missing.json"}})["isError"] == true);
  {
    std::ofstream out(f.tmp.path / "bad.json");
    out << "{}";
  }
  REQUIRE(f.call("library_registry_add", json{{"location", "bad.json"}})["isError"] == true);
}
