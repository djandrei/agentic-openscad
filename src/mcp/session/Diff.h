#pragma once

#include <optional>
#include <string>
#include <vector>

#include "json/json.hpp"

namespace mcp {

// The changed region between two texts, as one hunk: the lines removed from
// the old text and the lines added in the new text, both starting at
// `startLine` (1-based, in the new text). Computed from the common prefix and
// suffix, which is what the GUI needs to show where the agent worked.
struct LineHunk {
  size_t startLine = 1;
  std::vector<std::string> removed;
  std::vector<std::string> added;

  nlohmann::json toJson() const;
};

std::vector<std::string> splitLines(const std::string& text);

// nullopt when the texts are identical.
std::optional<LineHunk> singleHunkDiff(const std::string& before, const std::string& after);

}  // namespace mcp
