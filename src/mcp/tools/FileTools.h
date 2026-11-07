#pragma once

#include "mcp/protocol/Registry.h"
#include "mcp/session/SessionManager.h"

namespace mcp {

// file_list, file_read, file_write, file_rename, file_delete, editor_get.
void registerFileTools(ToolRegistry& tools, SessionManager& sessions);

}  // namespace mcp
