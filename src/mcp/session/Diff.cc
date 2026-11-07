#include "mcp/session/Diff.h"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

namespace mcp {

nlohmann::json LineHunk::toJson() const
{
  return nlohmann::json{{"startLine", startLine},
                        {"removedLines", removed.size()},
                        {"addedLines", added.size()},
                        {"endLine", startLine + (added.empty() ? 0 : added.size() - 1)}};
}

std::vector<std::string> splitLines(const std::string& text)
{
  std::vector<std::string> lines;
  size_t start = 0;
  while (start < text.size()) {
    const size_t end = text.find('\n', start);
    if (end == std::string::npos) {
      lines.push_back(text.substr(start));
      break;
    }
    lines.push_back(text.substr(start, end - start));
    start = end + 1;
  }
  return lines;
}

std::optional<LineHunk> singleHunkDiff(const std::string& before, const std::string& after)
{
  if (before == after) return std::nullopt;
  const auto oldLines = splitLines(before);
  const auto newLines = splitLines(after);

  size_t prefix = 0;
  while (prefix < oldLines.size() && prefix < newLines.size() && oldLines[prefix] == newLines[prefix]) {
    ++prefix;
  }
  size_t suffix = 0;
  while (suffix < oldLines.size() - prefix && suffix < newLines.size() - prefix &&
         oldLines[oldLines.size() - 1 - suffix] == newLines[newLines.size() - 1 - suffix]) {
    ++suffix;
  }

  LineHunk hunk;
  hunk.startLine = prefix + 1;
  hunk.removed.assign(oldLines.begin() + prefix, oldLines.end() - suffix);
  hunk.added.assign(newLines.begin() + prefix, newLines.end() - suffix);
  return hunk;
}

}  // namespace mcp
