#pragma once

#include "mcp/protocol/Registry.h"
#include "mcp/session/SessionManager.h"

namespace mcp {

// library_catalog, library_installed, library_install, library_remove,
// library_files, library_registry_list, library_registry_add.
void registerLibraryTools(ToolRegistry& tools, SessionManager& sessions);

}  // namespace mcp
