#pragma once

#include <string>

#include "mcp/protocol/Registry.h"
#include "mcp/session/SessionManager.h"

namespace mcp {

// Tools available in every mode: version, status, project_open, project_list,
// project_info.
void registerCoreTools(ToolRegistry& tools, SessionManager& sessions);

// Text returned in the initialize result to tell the agent how to work with
// this server.
std::string serverInstructions();

// JSON schema fragment for the optional `project` argument shared by tools.
nlohmann::json projectArgSchema();

}  // namespace mcp
