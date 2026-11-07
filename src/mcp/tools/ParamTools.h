#pragma once

#include "mcp/protocol/Registry.h"
#include "mcp/session/SessionManager.h"

namespace mcp {

// params_get, params_set, params_presets_list, params_preset_save.
void registerParamTools(ToolRegistry& tools, SessionManager& sessions);

}  // namespace mcp
