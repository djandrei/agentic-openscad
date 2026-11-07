#pragma once

#include <iosfwd>
#include <mutex>

#include "json/json.hpp"
#include "mcp/protocol/Server.h"
#include "mcp/transport/Transport.h"

namespace mcp {

// MCP stdio transport: newline-delimited JSON-RPC messages on an input and an
// output stream. The process's stdout must carry nothing else while this runs;
// OpenSCAD's log output goes to stderr when no output handler is installed.
class StdioTransport
{
public:
  StdioTransport(Server& server, std::istream& in, std::ostream& out,
                 Executor executor = directExecutor);

  // Reads messages until end of input. Returns 0.
  int run();

  // Writes one message; safe to call from any thread (used for notifications).
  void send(const nlohmann::json& message);

private:
  Server& server_;
  std::istream& in_;
  std::ostream& out_;
  Executor executor_;
  std::mutex writeMutex_;
};

}  // namespace mcp
