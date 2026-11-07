#include "mcp/session/Project.h"

#include <catch2/catch_all.hpp>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "mcp/testing/TempDir.h"

namespace fs = std::filesystem;
using nlohmann::json;

TEST_CASE("Project creates a default config file", "[mcp][project]")
{
  mcp::testing::TempDir tmp;
  mcp::Project project(tmp.path);
  REQUIRE_FALSE(fs::exists(project.configPath()));
  project.loadOrCreate();
  REQUIRE(fs::exists(project.configPath()));
  REQUIRE(project.mainFile() == "main.scad");
  REQUIRE(project.autoPreview());
  REQUIRE(project.mainPath() == project.root() / "main.scad");
  REQUIRE(project.name() == tmp.path.filename().string());
  json info = project.infoJson();
  REQUIRE(info["mainExists"] == false);
  REQUIRE(info["config"]["libraries"]["dir"] == "libraries");
}

TEST_CASE("Project preserves unknown config keys and fills defaults", "[mcp][project]")
{
  mcp::testing::TempDir tmp;
  {
    std::ofstream out(tmp.path / mcp::Project::kConfigFileName);
    out << R"({"main": "part.scad", "custom": {"a": 1}})";
  }
  mcp::Project project(tmp.path);
  project.loadOrCreate();
  REQUIRE(project.mainFile() == "part.scad");
  REQUIRE(project.config()["custom"]["a"] == 1);
  REQUIRE(project.config()["autoPreview"] == true);  // default merged in
  project.config()["autoPreview"] = false;
  project.save();

  mcp::Project reloaded(tmp.path);
  reloaded.loadOrCreate();
  REQUIRE_FALSE(reloaded.autoPreview());
  REQUIRE(reloaded.config()["custom"]["a"] == 1);
}

TEST_CASE("Project rejects invalid config and missing folders", "[mcp][project]")
{
  mcp::testing::TempDir tmp;
  {
    std::ofstream out(tmp.path / mcp::Project::kConfigFileName);
    out << "[1, 2";
  }
  mcp::Project project(tmp.path);
  REQUIRE_THROWS_AS(project.loadOrCreate(), std::runtime_error);
  REQUIRE_THROWS_AS(mcp::Project(tmp.path / "does-not-exist"), std::invalid_argument);
}

TEST_CASE("Project resolves paths inside the sandbox only", "[mcp][project]")
{
  mcp::testing::TempDir tmp;
  mcp::Project project(tmp.path);
  fs::create_directories(project.root() / "parts");

  REQUIRE(project.resolve("parts/bolt.scad") == project.root() / "parts" / "bolt.scad");
  REQUIRE(project.resolve("./main.scad") == project.root() / "main.scad");
  REQUIRE(project.resolve("parts/../main.scad") == project.root() / "main.scad");
  REQUIRE(project.resolve((project.root() / "main.scad").string()) == project.root() / "main.scad");
  REQUIRE(project.resolve(".") == project.root());
  REQUIRE(project.relative(project.root() / "parts" / "bolt.scad") == "parts/bolt.scad");

  REQUIRE_THROWS_AS(project.resolve("../outside.scad"), std::invalid_argument);
  REQUIRE_THROWS_AS(project.resolve("parts/../../outside.scad"), std::invalid_argument);
  REQUIRE_THROWS_AS(project.resolve("/etc/passwd"), std::invalid_argument);
  REQUIRE_THROWS_AS(project.resolve(""), std::invalid_argument);
}

TEST_CASE("Project follows symlinks when checking the sandbox", "[mcp][project]")
{
  mcp::testing::TempDir tmp;
  mcp::testing::TempDir outside;
  mcp::Project project(tmp.path);
  std::error_code ec;
  fs::create_directory_symlink(outside.path, project.root() / "escape", ec);
  if (ec) {
    WARN("symlinks not supported here; skipping");
    return;
  }
  REQUIRE_THROWS_AS(project.resolve("escape/secret.scad"), std::invalid_argument);
  REQUIRE_THROWS_AS(project.resolve("escape"), std::invalid_argument);

  fs::create_directories(project.root() / "lib");
  fs::create_directory_symlink(project.root() / "lib", project.root() / "inside-link");
  REQUIRE(project.resolve("inside-link/x.scad") == project.root() / "lib" / "x.scad");
}
