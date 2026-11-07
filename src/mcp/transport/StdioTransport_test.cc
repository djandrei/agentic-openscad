#include "mcp/transport/StdioTransport.h"

#include <catch2/catch_all.hpp>
#include <sstream>
#include <string>
#include <vector>

#include "mcp/protocol/Registry.h"
#include "mcp/protocol/Server.h"

using nlohmann::json;

namespace {

std::vector<json> outputLines(const std::string& text)
{
  std::vector<json> lines;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty()) lines.push_back(json::parse(line));
  }
  return lines;
}

}  // namespace

TEST_CASE("Stdio transport answers each request line and skips blanks", "[mcp][stdio]")
{
  mcp::ToolRegistry tools;
  mcp::ResourceRegistry resources;
  mcp::PromptRegistry prompts;
  mcp::Server server(mcp::ServerInfo{"t", "1", ""}, tools, resources, prompts);

  std::istringstream in(
    "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-06-"
    "18\"}}\n"
    "\n"
    "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}\n"
    "not json\n"
    "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\"}\n");
  std::ostringstream out;
  int executions = 0;
  mcp::StdioTransport transport(server, in, out, [&](const std::function<void()>& fn) {
    ++executions;
    fn();
  });
  REQUIRE(transport.run() == 0);

  const auto lines = outputLines(out.str());
  REQUIRE(lines.size() == 3);
  REQUIRE(lines[0]["id"] == 1);
  REQUIRE(lines[0]["result"]["protocolVersion"] == "2025-06-18");
  REQUIRE(lines[1]["id"].is_null());
  REQUIRE(lines[1]["error"]["code"] == -32700);
  REQUIRE(lines[2]["id"] == 2);
  REQUIRE(lines[2]["result"]["tools"].is_array());
  REQUIRE(executions == 4);  // every non-blank line goes through the executor
}

TEST_CASE("Stdio transport writes server notifications", "[mcp][stdio]")
{
  mcp::ToolRegistry tools;
  mcp::ResourceRegistry resources;
  mcp::PromptRegistry prompts;
  mcp::Server server(mcp::ServerInfo{"t", "1", ""}, tools, resources, prompts);
  std::istringstream in;
  std::ostringstream out;
  mcp::StdioTransport transport(server, in, out);
  transport.send(
    json{{"jsonrpc", "2.0"}, {"method", "notifications/message"}, {"params", json{{"data", "x"}}}});
  const auto lines = outputLines(out.str());
  REQUIRE(lines.size() == 1);
  REQUIRE(lines[0]["method"] == "notifications/message");
}
