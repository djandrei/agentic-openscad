#pragma once

#include "mcp/protocol/Registry.h"
#include "mcp/session/SessionManager.h"

namespace mcp {

// docs_search, docs_page, docs_builtin, docs_status, docs_update; the
// openscad://docs/... and openscad://library/... resources; the prompts.
void registerDocsTools(ToolRegistry& tools, SessionManager& sessions);
void registerDocsResources(ResourceRegistry& resources, PromptRegistry& prompts,
                           SessionManager& sessions);

}  // namespace mcp
