#pragma once

#include "mcp/protocol/Registry.h"
#include "mcp/session/SessionManager.h"

namespace mcp {

// camera_get, camera_set, view_options, view_capture, geometry_info, export.
void registerViewTools(ToolRegistry& tools, SessionManager& sessions);

}  // namespace mcp
