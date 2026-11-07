#pragma once

#include <string>

namespace mcp {

// Name reported to MCP clients in the `initialize` result (serverInfo.name).
std::string serverName();

// MCP protocol revision this server implements and offers during negotiation.
std::string protocolVersion();

}  // namespace mcp
