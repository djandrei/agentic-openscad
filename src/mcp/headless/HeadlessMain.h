#pragma once

#include "mcp/Options.h"

namespace mcp {

// Runs the MCP server without a GUI: over stdio when options.stdio is set,
// otherwise over HTTP on options.port until SIGINT/SIGTERM. Returns the
// process exit code.
int runHeadless(const Options& options);

}  // namespace mcp
