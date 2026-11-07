#include "mcp/tools/ParamTools.h"

#include <catch2/catch_all.hpp>
#include <filesystem>
#include <memory>
#include <string>

#include "core/Builtins.h"
#include "core/parsersettings.h"
#include "mcp/engine/ParamEngine.h"
#include "mcp/headless/HeadlessSession.h"
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

const char *kModel =
  "// Size of the box\n"
  "size = 10; // [5:50]\n"
  "// Rounded?\n"
  "rounded = false;\n"
  "label = \"box\"; // [box, lid]\n"
  "offset = [1, 2, 3];\n"
  "/* [Hidden] */\n"
  "internal = 3;\n"
  "cube(size);\n";

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
    mcp::registerParamTools(tools, sessions);
    server.handleMessage(json{{"jsonrpc", "2.0"},
                              {"id", 0},
                              {"method", "initialize"},
                              {"params", json{{"protocolVersion", "2025-06-18"}}}});
    sessions.open(tmp.path);
    call("file_write", json{{"path", "main.scad"}, {"content", kModel}});
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

  static json param(const json& description, const std::string& name)
  {
    for (const auto& entry : description["parameters"]) {
      if (entry["name"] == name) return entry;
    }
    return nullptr;
  }
};

}  // namespace

TEST_CASE("ParamEngine converts values both ways", "[mcp][params]")
{
  const json values{{"size", 12.5}, {"rounded", true}, {"label", "lid"}, {"offset", {4, 5, 6}}};
  const ParameterSet set = mcp::param::fromJson(values);
  REQUIRE(set.at("size").data() == "12.5");
  REQUIRE(set.at("rounded").data() == "true");
  REQUIRE(set.at("label").data() == "lid");
  REQUIRE(set.at("offset").data() == "[4, 5, 6]");
  const json back = mcp::param::toJson(set);
  REQUIRE(back["size"] == 12.5);
  REQUIRE(back["rounded"] == true);
  REQUIRE(back["label"] == "lid");
  REQUIRE(back["offset"] == json::array({4, 5, 6}));
  REQUIRE_THROWS_AS(mcp::param::fromJson(json{{"x", json::object()}}), mcp::ToolError);
}

TEST_CASE("params_get describes customizer parameters", "[mcp][params]")
{
  Fixture f;
  json description = f.ok("params_get");
  REQUIRE(description["file"] == "main.scad");
  json size = Fixture::param(description, "size");
  REQUIRE(size["type"] == "number");
  REQUIRE(size["caption"] == "Size of the box");
  REQUIRE(size["initial"] == 10);
  REQUIRE(size["value"] == 10);
  REQUIRE(size["min"] == 5);
  REQUIRE(size["max"] == 50);
  REQUIRE(Fixture::param(description, "rounded")["type"] == "boolean");
  REQUIRE(Fixture::param(description, "label")["options"].size() == 2);
  REQUIRE(Fixture::param(description, "offset")["value"] == json::array({1, 2, 3}));
  REQUIRE(Fixture::param(description, "internal").is_null());  // hidden group
  REQUIRE(description["overrides"].empty());
  REQUIRE(description["presets"].empty());
}

TEST_CASE("params_set changes the evaluated model", "[mcp][params]")
{
  Fixture f;
  json changed = f.ok("params_set", json{{"values", json{{"size", 30}, {"label", "lid"}}}});
  REQUIRE(Fixture::param(changed, "size")["value"] == 30);
  REQUIRE(Fixture::param(changed, "label")["value"] == "lid");
  REQUIRE(changed["overrides"]["size"] == 30);

  json rendered = f.ok("render");
  REQUIRE(rendered["geometry"]["boundingBox"]["max"] == json::array({30, 30, 30}));

  json clamped = f.ok("params_set", json{{"values", json{{"size", 500}}}});
  REQUIRE(Fixture::param(clamped, "size")["value"] == 50);  // clamped to the declared range

  json reset = f.ok("params_set", json{{"reset", true}});
  REQUIRE(Fixture::param(reset, "size")["value"] == 10);
  REQUIRE(reset["overrides"].empty());
  json rendered2 = f.ok("render");
  REQUIRE(rendered2["geometry"]["boundingBox"]["max"] == json::array({10, 10, 10}));

  json badValue = f.call("params_set", json{{"values", json{{"size", json::object()}}}});
  REQUIRE(badValue["isError"] == true);
  json missingFile = f.call("params_get", json{{"file", "nope.scad"}});
  REQUIRE(missingFile["isError"] == true);
}

TEST_CASE("presets are saved next to the file and can be loaded", "[mcp][params]")
{
  Fixture f;
  f.ok("params_set", json{{"values", json{{"size", 42}, {"rounded", true}}}});
  json saved = f.ok("params_preset_save", json{{"name", "big"}});
  REQUIRE(saved["presets"].size() == 1);
  REQUIRE(saved["presets"][0]["name"] == "big");
  REQUIRE(saved["presets"][0]["values"]["size"] == 42);
  REQUIRE(saved["presets"][0]["values"]["rounded"] == true);
  REQUIRE(std::filesystem::exists(f.tmp.path / "main.json"));

  f.ok("params_set", json{{"reset", true}});
  REQUIRE(Fixture::param(f.ok("params_get"), "size")["value"] == 10);
  json loaded = f.ok("params_set", json{{"preset", "big"}});
  REQUIRE(Fixture::param(loaded, "size")["value"] == 42);
  REQUIRE(loaded["presets"] == json::array({"big"}));

  json listed = f.ok("params_presets_list");
  REQUIRE(listed["presetFile"] == "main.json");
  REQUIRE(listed["presets"].size() == 1);

  json replaced = f.ok("params_preset_save", json{{"name", "big"}});
  REQUIRE(replaced["presets"].size() == 1);
  json unknown = f.call("params_set", json{{"preset", "nope"}});
  REQUIRE(unknown["isError"] == true);
  json unnamed = f.call("params_preset_save", json{{"name", ""}});
  REQUIRE(unnamed.contains("error"));  // minLength in the schema
}
