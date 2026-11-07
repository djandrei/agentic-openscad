#include "mcp/BuildInfo.h"

#include <catch2/catch_all.hpp>
#include <regex>

TEST_CASE("MCP build info is populated", "[mcp]")
{
  REQUIRE(mcp::serverName() == "agentic-openscad");
  // MCP protocol revisions are dates: YYYY-MM-DD
  REQUIRE(std::regex_match(mcp::protocolVersion(), std::regex("\\d{4}-\\d{2}-\\d{2}")));
}
