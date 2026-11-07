#include "mcp/tools/ViewTools.h"

#include <catch2/catch_all.hpp>
#include <filesystem>
#include <memory>
#include <string>

#include "core/Builtins.h"
#include "core/parsersettings.h"
#include "mcp/session/EngineSession.h"
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

namespace {

struct Fixture {
  mcp::testing::TempDir tmp;
  mcp::ToolRegistry tools;
  mcp::ResourceRegistry resources;
  mcp::PromptRegistry prompts;
  mcp::SessionManager sessions{"headless", [](std::shared_ptr<mcp::Project> project) {
                                 return std::make_unique<mcp::EngineSession>(std::move(project));
                               }};
  mcp::Server server{mcp::ServerInfo{"t", "1", ""}, tools, resources, prompts};

  Fixture()
  {
    static bool initialized = false;
    if (!initialized) {
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
    mcp::registerViewTools(tools, sessions);
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
    if (response->contains("error")) return *response;  // JSON-RPC error (schema rejection)
    return (*response)["result"];
  }

  json ok(const std::string& name, json args = json::object())
  {
    json result = call(name, args);
    INFO(result.dump());
    REQUIRE(result["isError"] == false);
    return result["structuredContent"];
  }

  void renderCube(double size = 20)
  {
    call("file_write",
         json{{"path", "main.scad"}, {"content", "cube(" + std::to_string(size) + ");\n"}});
    json rendered = ok("render");
    REQUIRE(rendered["ok"] == true);
  }
};

}  // namespace

TEST_CASE("camera tools report and change the virtual camera", "[mcp][view]")
{
  Fixture f;
  json camera = f.ok("camera_get");
  REQUIRE(camera["projection"] == "perspective");
  REQUIRE(camera["vpr"].size() == 3);

  json moved = f.ok("camera_set", json{{"vpt", {1, 2, 3}}, {"vpd", 250}, {"projection", "orthogonal"}});
  REQUIRE(moved["vpt"] == json::array({1, 2, 3}));
  REQUIRE(moved["vpd"] == 250);
  REQUIRE(moved["projection"] == "orthogonal");
  REQUIRE(f.ok("camera_get")["vpd"] == 250);

  json top = f.ok("camera_set", json{{"preset", "top"}});
  REQUIRE(top["vpr"] == json::array({0, 0, 0}));  // GUI's "top" rotation expressed as $vpr
  json bad = f.call("camera_set", json{{"preset", "sideways"}});
  REQUIRE(bad.contains("error"));  // rejected by the schema enum
  json badVpt = f.call("camera_set", json{{"vpt", {1, 2}}});
  REQUIRE(badVpt.contains("error"));
}

TEST_CASE("view_options reports and validates options", "[mcp][view]")
{
  Fixture f;
  json options = f.ok("view_options");
  REQUIRE(options["edges"] == false);
  REQUIRE(options["mode"] == "preview");
  REQUIRE(options["colorSchemes"].is_array());
  REQUIRE_FALSE(options["colorSchemes"].empty());

  json changed = f.ok("view_options", json{{"edges", true}, {"axes", true}, {"mode", "render"}});
  REQUIRE(changed["edges"] == true);
  REQUIRE(changed["axes"] == true);
  REQUIRE(changed["mode"] == "render");
  json unknownScheme = f.call("view_options", json{{"colorScheme", "NoSuchScheme"}});
  REQUIRE(unknownScheme["isError"] == true);
  const std::string scheme = options["colorSchemes"][0];
  REQUIRE(f.ok("view_options", json{{"colorScheme", scheme}})["colorScheme"] == scheme);
}

TEST_CASE("geometry_info reports volume, area and bounding box", "[mcp][view]")
{
  Fixture f;
  json none = f.call("geometry_info");
  REQUIRE(none["isError"] == true);

  f.renderCube(20);
  json info = f.ok("geometry_info");
  REQUIRE(info["dimension"] == 3);
  REQUIRE(info["empty"] == false);
  REQUIRE(info["volume"].get<double>() == Catch::Approx(8000.0));
  REQUIRE(info["surfaceArea"].get<double>() == Catch::Approx(2400.0));
  REQUIRE(info["boundingBox"]["max"] == json::array({20, 20, 20}));
  REQUIRE(info["facets"].get<int>() >= 6);
  if (info.contains("manifold")) REQUIRE(info["manifold"] == true);

  f.call("file_write", json{{"path", "main.scad"}, {"content", "square(10);\n"}});
  f.ok("render");
  json flat = f.ok("geometry_info");
  REQUIRE(flat["dimension"] == 2);
  REQUIRE(flat["area"].get<double>() == Catch::Approx(100.0));
  REQUIRE(flat["contours"] == 1);
}

TEST_CASE("export writes geometry files inside the project", "[mcp][view]")
{
  Fixture f;
  f.renderCube(10);
  json stl = f.ok("export", json{{"format", "binstl"}, {"path", "out/cube.stl"}});
  REQUIRE(stl["path"] == "out/cube.stl");
  REQUIRE(stl["bytes"].get<int>() > 84);
  REQUIRE(std::filesystem::exists(f.tmp.path / "out" / "cube.stl"));

  json ascii = f.ok("export", json{{"format", "stl"}, {"path", "cube-ascii.stl"}});
  REQUIRE(ascii["format"] == "asciistl");
  json off = f.ok("export", json{{"format", "off"}, {"path", "cube.off"}});
  REQUIRE(off["bytes"].get<int>() > 0);

  json outside = f.call("export", json{{"format", "stl"}, {"path", "../escape.stl"}});
  REQUIRE(outside["isError"] == true);
  json wrongDim = f.call("export", json{{"format", "svg"}, {"path", "cube.svg"}});
  REQUIRE(wrongDim["isError"] == true);
  json badFormat = f.call("export", json{{"format", "xyz"}, {"path", "cube.xyz"}});
  REQUIRE(badFormat.contains("error"));  // schema enum
}

TEST_CASE("view_capture returns PNG images when offscreen GL is available", "[mcp][view]")
{
  Fixture f;
  f.renderCube(10);
  json result = f.call(
    "view_capture",
    json{
      {"views", json::array({"diagonal", "top"})}, {"width", 160}, {"height", 120}, {"mode", "render"}});
  if (result["isError"] == true) {
    WARN("view_capture unavailable here: " + result["content"][0]["text"].get<std::string>());
    return;
  }
  REQUIRE(result["structuredContent"]["views"].size() == 2);
  REQUIRE(result["structuredContent"]["views"][0]["name"] == "diagonal");
  REQUIRE(result["structuredContent"]["views"][0]["width"] == 160);
  int images = 0;
  for (const auto& item : result["content"]) {
    if (item["type"] == "image") {
      ++images;
      REQUIRE(item["mimeType"] == "image/png");
      const std::string data = item["data"];
      REQUIRE(data.size() > 100);
      REQUIRE(data.substr(0, 5) == "iVBOR");  // base64 of the PNG signature
    }
  }
  REQUIRE(images == 2);

  json preview = f.call("view_capture", json{{"views", json::array({"front"})}, {"mode", "preview"}});
  REQUIRE(preview["isError"] == false);
  REQUIRE(preview["structuredContent"]["views"][0]["camera"]["vpr"] == json::array({90, 0, 0}));
}
