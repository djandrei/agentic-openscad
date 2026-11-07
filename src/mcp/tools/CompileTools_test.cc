#include "mcp/tools/CompileTools.h"

#include <catch2/catch_all.hpp>
#include <filesystem>
#include <memory>
#include <string>

#include "core/Builtins.h"
#include "core/parsersettings.h"
#include "platform/PlatformUtils.h"
#include "mcp/headless/HeadlessSession.h"
#include "mcp/protocol/Registry.h"
#include "mcp/protocol/Server.h"
#include "mcp/session/MessageLog.h"
#include "mcp/session/SessionManager.h"
#include "mcp/testing/TempDir.h"
#include "mcp/tools/CoreTools.h"
#include "mcp/tools/FileTools.h"

using nlohmann::json;

namespace {

struct Fixture {
  mcp::testing::TempDir tmp;
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
      // parser_init() resolves the bundled library folder relative to the
      // application path, which the unit test binary must register itself.
      std::error_code ec;
      auto exe = std::filesystem::read_symlink("/proc/self/exe", ec);
      PlatformUtils::registerApplicationPath(
        (ec ? std::filesystem::current_path() : exe.parent_path()).string());
      parser_init();
      Builtins::initialize();
      initialized = true;
    }
    mcp::MessageLog::instance().attach();
    mcp::registerCoreTools(tools, sessions);
    mcp::registerFileTools(tools, sessions);
    mcp::registerCompileTools(tools, sessions);
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
    REQUIRE(response->contains("result"));
    return (*response)["result"];
  }

  json compile(const std::string& tool, const std::string& file, const std::string& content)
  {
    call("file_write", json{{"path", file}, {"content", content}});
    json result = call(tool, json{{"file", file}});
    REQUIRE(result["isError"] == false);
    return result["structuredContent"];
  }

  static bool hasMessage(const json& outcome, const std::string& severity, const std::string& fragment)
  {
    for (const auto& message : outcome["messages"]) {
      if (message["severity"] == severity &&
          message["text"].get<std::string>().find(fragment) != std::string::npos) {
        return true;
      }
    }
    return false;
  }
};

}  // namespace

TEST_CASE("check reports syntax errors with locations", "[mcp][compile]")
{
  Fixture f;
  json bad = f.compile("check", "bad.scad", "cube(10);\nsphere(;\n");
  REQUIRE(bad["ok"] == false);
  REQUIRE(bad["parsed"] == false);
  REQUIRE(bad["errors"] >= 1);
  bool located = false;
  for (const auto& message : bad["messages"]) {
    if (message["severity"] == "error" && message.value("line", 0) == 2) located = true;
  }
  REQUIRE(located);

  json good = f.compile("check", "good.scad", "cube(10);\n");
  REQUIRE(good["ok"] == true);
  REQUIRE(good["parsed"] == true);
  REQUIRE(good["errors"] == 0);
  REQUIRE(good["mode"] == "check");
}

TEST_CASE("preview reports warnings, echo and assertion failures", "[mcp][compile]")
{
  Fixture f;
  json warned = f.compile("preview", "warn.scad", "echo(\"hi\");\ncube(undefined_size);\n");
  REQUIRE(warned["parsed"] == true);
  REQUIRE(warned["warnings"] >= 1);
  INFO(warned.dump());
  REQUIRE(Fixture::hasMessage(warned, "warning", "undefined_size"));
  REQUIRE(Fixture::hasMessage(warned, "echo", "hi"));
  bool relativeFile = false;
  for (const auto& message : warned["messages"]) {
    if (message.contains("file") && message["file"] == "warn.scad") relativeFile = true;
  }
  REQUIRE(relativeFile);

  json failed = f.compile("preview", "assert.scad", "assert(1 == 2, \"math broke\");\ncube(1);\n");
  REQUIRE(failed["ok"] == false);
  REQUIRE(failed["errors"] >= 1);
  REQUIRE(Fixture::hasMessage(failed, "error", "math broke"));

  json clean = f.compile("preview", "clean.scad", "difference() { cube(10); sphere(6); }\n");
  REQUIRE(clean["ok"] == true);
  REQUIRE(clean["errors"] == 0);
  REQUIRE(clean["warnings"] == 0);
  REQUIRE(clean["millis"].get<double>() >= 0);
}

TEST_CASE("render returns a geometry summary", "[mcp][compile]")
{
  Fixture f;
  json rendered = f.compile("render", "box.scad", "translate([1, 2, 3]) cube([4, 5, 6]);\n");
  REQUIRE(rendered["ok"] == true);
  REQUIRE(rendered["mode"] == "render");
  INFO(rendered.dump());
  REQUIRE(rendered["geometry"]["dimension"] == 3);
  REQUIRE(rendered["geometry"]["empty"] == false);
  REQUIRE(rendered["geometry"]["facets"].get<int>() >= 6);
  REQUIRE(rendered["geometry"]["boundingBox"]["min"][0].get<double>() == Catch::Approx(1.0));
  REQUIRE(rendered["geometry"]["boundingBox"]["max"][2].get<double>() == Catch::Approx(9.0));
  REQUIRE(rendered["geometry"]["size"].is_null());

  json flat = f.compile("render", "flat.scad", "square(5);\n");
  REQUIRE(flat["geometry"]["dimension"] == 2);

  json nothing = f.compile("render", "empty.scad", "// nothing here\n");
  REQUIRE(nothing["geometry"]["empty"] == true);
}

TEST_CASE("dumps return AST, CSG tree and products", "[mcp][compile]")
{
  Fixture f;
  f.call("file_write",
         json{{"path", "main.scad"}, {"content", "x = 3;\ntranslate([x, 0, 0]) cube(x);\n"}});
  json ast = f.call("ast_dump")["structuredContent"];
  REQUIRE(ast["parsed"] == true);
  REQUIRE(ast["text"].get<std::string>().find("x = 3;") != std::string::npos);

  json csg = f.call("csg_tree_dump")["structuredContent"];
  INFO(csg.dump());
  REQUIRE(csg["text"].get<std::string>().find("cube(size = [3, 3, 3]") != std::string::npos);
  REQUIRE(csg["text"].get<std::string>().find("multmatrix") != std::string::npos);

  json products = f.call("csg_products_dump")["structuredContent"];
  REQUIRE_FALSE(products["text"].get<std::string>().empty());

  json missing = f.call("ast_dump", json{{"file", "nope.scad"}});
  REQUIRE(missing["isError"] == true);
}

TEST_CASE("log_get pages through messages", "[mcp][compile]")
{
  Fixture f;
  const auto before = mcp::MessageLog::instance().lastSeq();
  f.compile("preview", "echo.scad", "echo(\"one\");\necho(\"two\");\n");
  json page = f.call("log_get", json{{"since", before}, {"limit", 1}})["structuredContent"];
  REQUIRE(page["messages"].size() == 1);
  REQUIRE(page["lastSeq"].get<std::uint64_t>() > before);
  json rest = f.call("log_get", json{{"since", page["messages"][0]["seq"]}})["structuredContent"];
  REQUIRE(rest["messages"].size() >= 1);
}
