#include "mcp/session/Diff.h"

#include <catch2/catch_all.hpp>

TEST_CASE("splitLines keeps the last line without newline", "[mcp][diff]")
{
  REQUIRE(mcp::splitLines("").empty());
  REQUIRE(mcp::splitLines("a\nb\n") == std::vector<std::string>{"a", "b"});
  REQUIRE(mcp::splitLines("a\nb") == std::vector<std::string>{"a", "b"});
  REQUIRE(mcp::splitLines("\n") == std::vector<std::string>{""});
}

TEST_CASE("singleHunkDiff finds the changed region", "[mcp][diff]")
{
  REQUIRE_FALSE(mcp::singleHunkDiff("same\n", "same\n").has_value());

  auto hunk = mcp::singleHunkDiff("a\nb\nc\nd\n", "a\nB\nX\nc\nd\n");
  REQUIRE(hunk.has_value());
  REQUIRE(hunk->startLine == 2);
  REQUIRE(hunk->removed == std::vector<std::string>{"b"});
  REQUIRE(hunk->added == std::vector<std::string>{"B", "X"});
  REQUIRE(hunk->toJson()["endLine"] == 3);

  auto appended = mcp::singleHunkDiff("a\n", "a\nb\n");
  REQUIRE(appended->startLine == 2);
  REQUIRE(appended->removed.empty());
  REQUIRE(appended->added == std::vector<std::string>{"b"});

  auto deleted = mcp::singleHunkDiff("a\nb\n", "a\n");
  REQUIRE(deleted->startLine == 2);
  REQUIRE(deleted->removed == std::vector<std::string>{"b"});
  REQUIRE(deleted->added.empty());
  REQUIRE(deleted->toJson()["endLine"] == 2);

  auto fromEmpty = mcp::singleHunkDiff("", "x\ny\n");
  REQUIRE(fromEmpty->startLine == 1);
  REQUIRE(fromEmpty->added.size() == 2);
}
