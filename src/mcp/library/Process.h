#pragma once

#include <string>
#include <vector>

namespace mcp::library {

// Runs a program without a shell and waits for it. Returns the exit code;
// combined stdout/stderr goes to `output`. Throws ToolError when the program
// cannot be started or the platform does not support it.
int runProcess(const std::vector<std::string>& args, std::string *output);

bool programAvailable(const std::string& program);

}  // namespace mcp::library
