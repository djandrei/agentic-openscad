#include "mcp/session/FileStore.h"

#include <catch2/catch_all.hpp>
#include <filesystem>
#include <fstream>
#include <string>

#include "mcp/testing/TempDir.h"

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

struct Fixture {
  mcp::testing::TempDir tmp;
  mcp::Project project{tmp.path};
  mcp::FileStore files{project};
  Fixture() { project.loadOrCreate(); }
};

}  // namespace

TEST_CASE("FileStore writes atomically and reports versions", "[mcp][files]")
{
  Fixture f;
  auto change = f.files.write("main.scad", "cube(10);\n");
  REQUIRE(change.created);
  REQUIRE(change.previousVersion.empty());
  REQUIRE(change.version == mcp::FileStore::versionOf("cube(10);\n"));
  REQUIRE(change.path == "main.scad");
  REQUIRE(change.hunk.has_value());
  REQUIRE(change.hunk->added.size() == 1);
  REQUIRE(fs::exists(f.project.root() / "main.scad"));
  json j = change.toJson();
  REQUIRE(j["lineCount"] == 1);
  REQUIRE(j["change"]["startLine"] == 1);

  // No temp files left behind
  size_t count = 0;
  for (const auto& entry : fs::directory_iterator(f.project.root())) {
    (void)entry;
    ++count;
  }
  REQUIRE(count == 2);  // config + main.scad

  auto second = f.files.write("parts/bolt.scad", "sphere(2);\n");
  REQUIRE(second.created);
  REQUIRE(fs::exists(f.project.root() / "parts" / "bolt.scad"));
}

TEST_CASE("FileStore enforces optimistic versions", "[mcp][files]")
{
  Fixture f;
  auto first = f.files.write("main.scad", "a\n");
  auto second = f.files.write("main.scad", "b\n", first.version);
  REQUIRE_FALSE(second.created);
  REQUIRE(second.previousVersion == first.version);
  REQUIRE_THROWS_AS(f.files.write("main.scad", "c\n", first.version), mcp::ConflictError);
  try {
    f.files.write("main.scad", "c\n", first.version);
  } catch (const mcp::ConflictError& e) {
    REQUIRE(e.data()["currentVersion"] == second.version);
    REQUIRE(e.data()["path"] == "main.scad");
  }
  REQUIRE(f.files.read("main.scad").content == "b\n");
  // Creating a file with a stale base version is also a conflict
  REQUIRE_THROWS_AS(f.files.write("new.scad", "x", std::string("deadbeef")), mcp::ConflictError);
  REQUIRE(f.files.currentVersion("missing.scad") == std::nullopt);
  REQUIRE(f.files.currentVersion("main.scad") == second.version);
}

TEST_CASE("FileStore applies exact edits", "[mcp][files]")
{
  Fixture f;
  f.files.write("main.scad", "cube(10);\nsphere(5);\ncube(10);\n");
  REQUIRE_THROWS_AS(f.files.patch("main.scad", {{"cube(10);", "cube(20);"}}),
                    mcp::ToolError);                                                   // ambiguous
  REQUIRE_THROWS_AS(f.files.patch("main.scad", {{"cylinder", "x"}}), mcp::ToolError);  // missing
  REQUIRE_THROWS_AS(f.files.patch("main.scad", {{"", "x"}}), mcp::ToolError);          // empty

  auto change =
    f.files.patch("main.scad", {{"sphere(5);", "sphere(6);"}, {"cube(10);", "cube(20);", true}});
  REQUIRE(f.files.read("main.scad").content == "cube(20);\nsphere(6);\ncube(20);\n");
  REQUIRE(change.hunk->startLine == 1);
  REQUIRE(change.hunk->removed.size() == 3);
  REQUIRE(change.hunk->added.size() == 3);

  auto version = f.files.currentVersion("main.scad");
  REQUIRE_THROWS_AS(f.files.patch("main.scad", {{"sphere(6);", "sphere(7);"}}, std::string("stale")),
                    mcp::ConflictError);
  f.files.patch("main.scad", {{"sphere(6);", "sphere(7);"}}, version);
  REQUIRE(f.files.read("main.scad").content.find("sphere(7)") != std::string::npos);
}

TEST_CASE("FileStore reads line windows", "[mcp][files]")
{
  Fixture f;
  f.files.write("main.scad", "l1\nl2\nl3\nl4\n");
  auto whole = f.files.read("main.scad");
  REQUIRE(whole.lineCount == 4);
  REQUIRE_FALSE(whole.truncated);
  auto window = f.files.read("main.scad", 2, 2);
  REQUIRE(window.content == "l2\nl3\n");
  REQUIRE(window.startLine == 2);
  REQUIRE(window.truncated);
  REQUIRE(window.version == whole.version);
  auto tail = f.files.read("main.scad", 4, 10);
  REQUIRE(tail.content == "l4\n");
  REQUIRE_FALSE(tail.truncated);
  REQUIRE(f.files.read("main.scad", 9).content.empty());
  REQUIRE_THROWS_AS(f.files.read("missing.scad"), mcp::ToolError);
  REQUIRE_THROWS_AS(f.files.read("../x"), mcp::ToolError);
}

TEST_CASE("FileStore lists, renames and deletes", "[mcp][files]")
{
  Fixture f;
  f.files.write("main.scad", "a");
  f.files.write("parts/bolt.scad", "b");
  f.files.write(".hidden/secret.scad", "c");
  fs::create_directories(f.project.root() / "empty");

  auto entries = f.files.list();
  REQUIRE(entries.size() == 2);
  REQUIRE(entries[0].path == "main.scad");
  REQUIRE(entries[1].path == "parts/bolt.scad");
  REQUIRE(entries[0].version == mcp::FileStore::versionOf("a"));
  REQUIRE(entries[0].modified.size() == 20);

  auto withHidden = f.files.list(".", true, true);
  REQUIRE(withHidden.size() == 4);  // + config + .hidden/secret.scad
  auto flat = f.files.list(".", false);
  REQUIRE(flat.size() == 1);
  auto sub = f.files.list("parts");
  REQUIRE(sub.size() == 1);
  REQUIRE(sub[0].path == "parts/bolt.scad");
  REQUIRE_THROWS_AS(f.files.list("nope"), mcp::ToolError);

  f.files.rename("parts/bolt.scad", "parts/screw.scad");
  REQUIRE(fs::exists(f.project.root() / "parts" / "screw.scad"));
  REQUIRE_THROWS_AS(f.files.rename("parts/screw.scad", "main.scad"), mcp::ToolError);
  REQUIRE_THROWS_AS(f.files.rename("parts/screw.scad", "../out.scad"), mcp::ToolError);

  f.files.remove("parts/screw.scad");
  REQUIRE_FALSE(fs::exists(f.project.root() / "parts" / "screw.scad"));
  REQUIRE_THROWS_AS(f.files.remove("parts/screw.scad"), mcp::ToolError);
  REQUIRE_THROWS_AS(f.files.remove(mcp::Project::kConfigFileName), mcp::ToolError);
  REQUIRE_THROWS_AS(f.files.write(mcp::Project::kConfigFileName, "{}"), mcp::ToolError);
}
