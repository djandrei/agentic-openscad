#include "mcp/session/MessageLog.h"

#include <catch2/catch_all.hpp>
#include <filesystem>
#include <memory>

#include "core/AST.h"
#include "utils/printutils.h"

TEST_CASE("MessageLog records printed messages with locations", "[mcp][log]")
{
  auto& log = mcp::MessageLog::instance();
  log.attach();
  const auto before = log.lastSeq();

  auto path = std::make_shared<std::filesystem::path>("/tmp/demo.scad");
  LOG(message_group::Warning, Location(7, 3, 7, 10, path), "", "variable %1$s undefined", "x");
  LOG(message_group::Echo, "hello");
  LOG(message_group::Error, "boom");
  LOG("plain");

  auto messages = log.since(before);
  REQUIRE(messages.size() == 4);
  REQUIRE(messages[0].severity == "warning");
  REQUIRE(messages[0].file == "/tmp/demo.scad");
  REQUIRE(messages[0].line == 7);
  REQUIRE(messages[0].column == 3);
  REQUIRE(messages[0].text.find("undefined") != std::string::npos);
  REQUIRE(messages[1].severity == "echo");
  REQUIRE(messages[1].text == "hello");
  REQUIRE(messages[2].severity == "error");
  REQUIRE(messages[3].severity == "info");
  REQUIRE(messages[3].line == 0);
  REQUIRE(messages[3].seq == before + 4);

  auto counts = mcp::MessageLog::count(messages);
  REQUIRE(counts.errors == 1);
  REQUIRE(counts.warnings == 1);

  REQUIRE(log.since(before, 2).size() == 2);
  REQUIRE(log.since(before + 3).size() == 1);

  nlohmann::json j = messages[0].toJson();
  REQUIRE(j["line"] == 7);
  REQUIRE_FALSE(messages[1].toJson().contains("line"));

  log.detach();
  LOG("after detach");
  REQUIRE(log.lastSeq() == before + 4);
}
