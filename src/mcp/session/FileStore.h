#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "json/json.hpp"
#include "mcp/protocol/Registry.h"
#include "mcp/session/Diff.h"
#include "mcp/session/Project.h"

namespace mcp {

struct FileEntry {
  std::string path;  // relative to the project root, '/' separators
  std::uintmax_t size = 0;
  std::string version;
  std::string modified;  // ISO-8601 UTC
  nlohmann::json toJson() const;
};

struct FileContent {
  std::string path;
  std::string content;
  std::string version;  // of the whole file, not of the returned window
  size_t lineCount = 0;
  size_t startLine = 1;
  bool truncated = false;
  nlohmann::json toJson() const;
};

struct TextEdit {
  std::string oldText;
  std::string newText;
  bool replaceAll = false;
};

// Outcome of a write; carries both texts so the GUI can update its editor and
// show the change without re-reading the disk.
struct FileChange {
  std::string path;
  std::filesystem::path absolute;
  std::string previousVersion;  // empty when created
  std::string version;
  bool created = false;
  std::string previousContent;
  std::string content;
  std::optional<LineHunk> hunk;
  nlohmann::json toJson() const;
};

// Thrown when a write's baseVersion no longer matches the file on disk.
class ConflictError : public ToolError
{
public:
  ConflictError(const std::string& path, const std::string& currentVersion,
                const std::string& baseVersion);
};

// Sandboxed file access for one project: listing, reading with line windows,
// atomic writes with optimistic versioning, exact-text edits, rename, delete.
class FileStore
{
public:
  explicit FileStore(const Project& project) : project_(project) {}

  static constexpr size_t kMaxListEntries = 5000;

  // Lists regular files under `dir` (relative). Hidden entries (names starting
  // with '.') are skipped unless includeHidden is set.
  std::vector<FileEntry> list(const std::string& dir = ".", bool recursive = true,
                              bool includeHidden = false, bool *truncated = nullptr) const;

  // Reads a file; `startLine` is 1-based, `maxLines` 0 means to the end.
  FileContent read(const std::string& path, size_t startLine = 1, size_t maxLines = 0) const;

  // Writes the whole content atomically (temp file + rename), creating parent
  // directories. When baseVersion is given and the file exists, the write
  // fails with ConflictError unless the versions match.
  FileChange write(const std::string& path, const std::string& content,
                   const std::optional<std::string>& baseVersion = std::nullopt);

  // Applies exact-text replacements to the current content and writes it.
  FileChange patch(const std::string& path, const std::vector<TextEdit>& edits,
                   const std::optional<std::string>& baseVersion = std::nullopt);

  void rename(const std::string& from, const std::string& to);
  void remove(const std::string& path);

  std::optional<std::string> currentVersion(const std::string& path) const;

  // Content hash used as the file version (FNV-1a 64, hex).
  static std::string versionOf(const std::string& content);
  // Applies edits; throws ToolError when an old text is missing or ambiguous.
  static std::string applyEdits(const std::string& content, const std::vector<TextEdit>& edits);

private:
  std::filesystem::path resolveExisting(const std::string& path) const;
  static std::string readAll(const std::filesystem::path& file);
  static void writeAtomically(const std::filesystem::path& file, const std::string& content);

  const Project& project_;
};

}  // namespace mcp
