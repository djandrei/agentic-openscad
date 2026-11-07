#pragma once

#include <string>
#include <vector>

#include "mcp/Options.h"

namespace mcp {

// Agent mode with a display: runs the MCP server (HTTP on 127.0.0.1) inside a
// Qt application whose only window is the viewer that mirrors what the agent
// does. The classic OpenSCAD GUI is not created. Returns the exit code.
int runViewerApp(int argc, char **argv, const Options& options,
                 const std::vector<std::string>& inputFiles);

}  // namespace mcp
