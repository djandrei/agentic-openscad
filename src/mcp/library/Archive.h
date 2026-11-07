#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace mcp::library {

// Extracts a zip archive (in memory) into `target`. Entry names are
// sanitized (no absolute paths, no ".." components). When every entry sits
// under one top-level folder (as forge archives do) that folder is stripped.
// Returns the relative paths of the extracted files. Throws ToolError.
std::vector<std::string> extractZip(const std::string& bytes, const std::filesystem::path& target);

// Builds a zip archive in memory from {path, content} pairs (used by tests
// and to keep archive handling in one place).
std::string createZip(const std::vector<std::pair<std::string, std::string>>& entries);

}  // namespace mcp::library
