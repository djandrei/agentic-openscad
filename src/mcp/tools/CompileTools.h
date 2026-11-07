#pragma once

#include "mcp/protocol/Registry.h"
#include "mcp/session/SessionManager.h"

namespace mcp {

// check, preview, render, ast_dump, csg_tree_dump, csg_products_dump, log_get.
void registerCompileTools(ToolRegistry& tools, SessionManager& sessions);

}  // namespace mcp
