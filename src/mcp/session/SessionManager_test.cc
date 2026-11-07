#include "mcp/session/SessionManager.h"

#include <catch2/catch_all.hpp>
#include <memory>

#include "mcp/protocol/Registry.h"
#include "mcp/testing/TempDir.h"

using nlohmann::json;

namespace {

class FakeSession : public mcp::ProjectSession
{
public:
  using ProjectSession::ProjectSession;
  json status() const override { return json{{"mode", "fake"}}; }
};

mcp::SessionManager makeManager()
{
  return mcp::SessionManager("fake", [](std::shared_ptr<mcp::Project> project) {
    return std::make_unique<FakeSession>(std::move(project));
  });
}

}  // namespace

TEST_CASE("SessionManager opens a project once and makes it current", "[mcp][session]")
{
  mcp::testing::TempDir tmp;
  auto manager = makeManager();
  REQUIRE(manager.mode() == "fake");
  REQUIRE(manager.current() == nullptr);

  auto& first = manager.open(tmp.path);
  auto& again = manager.open(tmp.path.string() + "/.");
  REQUIRE(&first == &again);
  REQUIRE(manager.sessions().size() == 1);
  REQUIRE(manager.current() == &first);
  REQUIRE(first.project().root() == std::filesystem::canonical(tmp.path));
  REQUIRE(std::filesystem::exists(first.project().configPath()));
}

TEST_CASE("SessionManager resolves the project argument", "[mcp][session]")
{
  mcp::testing::TempDir a;
  mcp::testing::TempDir b;
  auto manager = makeManager();
  REQUIRE_THROWS_AS(manager.resolve(json::object()), mcp::ToolError);

  auto& sa = manager.open(a.path);
  auto& sb = manager.open(b.path);
  REQUIRE(manager.current() == &sb);
  REQUIRE(&manager.resolve(json::object()) == &sb);
  REQUIRE(&manager.resolve(json{{"project", sa.project().id()}}) == &sa);
  REQUIRE(manager.current() == &sa);  // resolving by argument switches the current project
  REQUIRE(&manager.resolve(json{{"project", b.path.string()}}) == &sb);
  REQUIRE_THROWS_AS(manager.resolve(json{{"project", "/nonexistent/project"}}), mcp::ToolError);
}

TEST_CASE("SessionManager closes sessions and rejects bad folders", "[mcp][session]")
{
  mcp::testing::TempDir a;
  mcp::testing::TempDir b;
  auto manager = makeManager();
  auto& sa = manager.open(a.path);
  manager.open(b.path);
  manager.close(sa.project().id());
  REQUIRE(manager.sessions().size() == 1);
  REQUIRE(manager.current() != nullptr);
  REQUIRE(manager.find(a.path.string()) == nullptr);

  REQUIRE_THROWS_AS(manager.open(a.path / "missing"), mcp::ToolError);
}
