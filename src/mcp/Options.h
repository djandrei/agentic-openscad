#pragma once

#include <string>

namespace mcp {

// Command-line options that control the MCP server. Filled in openscad.cc and
// read by the headless entry point or, in GUI mode, by AgentBridge through
// global().
struct Options {
  static constexpr int kDefaultPort = 7645;

  bool stdio = false;     // --mcp-stdio
  int port = 0;           // --mcp-port
  bool headless = false;  // --mcp-headless (with --mcp-port: no GUI)
  std::string token;      // --mcp-token
  std::string tokenFile;  // --mcp-token-file
  std::string project;    // --project (absolute path)

  static Options& global()
  {
    static Options options;
    return options;
  }
};

}  // namespace mcp
