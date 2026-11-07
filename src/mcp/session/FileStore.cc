#include "mcp/session/FileStore.h"

#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "mcp/transport/HttpTransport.h"

namespace fs = std::filesystem;

namespace mcp {

namespace {

std::string isoTime(fs::file_time_type time)
{
  // file_time_type -> system_clock (C++17 has no clock_cast; the offset of the
  // two clocks is taken now).
  const auto sys = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
    time - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
  const std::time_t t = std::chrono::system_clock::to_time_t(sys);
  std::tm tm{};
#ifdef _WIN32
  gmtime_s(&tm, &t);  // MinGW's UCRT has no gmtime_r
#else
  gmtime_r(&t, &tm);
#endif
  std::ostringstream out;
  out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
  return out.str();
}

bool isHidden(const fs::path& p)
{
  const std::string name = p.filename().string();
  return name.size() > 1 && name[0] == '.';
}

size_t countLines(const std::string& content)
{
  if (content.empty()) return 0;
  size_t n = 0;
  for (char c : content) {
    if (c == '\n') ++n;
  }
  if (content.back() != '\n') ++n;
  return n;
}

}  // namespace

nlohmann::json FileEntry::toJson() const
{
  return nlohmann::json{{"path", path}, {"size", size}, {"version", version}, {"modified", modified}};
}

nlohmann::json FileContent::toJson() const
{
  return nlohmann::json{{"path", path},           {"content", content},     {"version", version},
                        {"lineCount", lineCount}, {"startLine", startLine}, {"truncated", truncated}};
}

nlohmann::json FileChange::toJson() const
{
  nlohmann::json j{{"path", path},       {"version", version},     {"previousVersion", previousVersion},
                   {"created", created}, {"size", content.size()}, {"lineCount", countLines(content)}};
  j["change"] = hunk ? hunk->toJson() : nlohmann::json(nullptr);
  return j;
}

ConflictError::ConflictError(const std::string& path, const std::string& currentVersion,
                             const std::string& baseVersion)
  : ToolError(
      "conflict: " + path + " changed since version " + baseVersion + " (current version " +
        currentVersion + "); read the file again before writing",
      nlohmann::json{{"path", path}, {"currentVersion", currentVersion}, {"baseVersion", baseVersion}})
{
}

std::string FileStore::versionOf(const std::string& content)
{
  std::uint64_t hash = 1469598103934665603ULL;
  for (unsigned char c : content) {
    hash ^= c;
    hash *= 1099511628211ULL;
  }
  std::ostringstream out;
  out << std::hex << std::setfill('0') << std::setw(16) << hash;
  return out.str();
}

std::string FileStore::readAll(const fs::path& file)
{
  std::ifstream in(file, std::ios::binary);
  if (!in) throw ToolError("cannot read file: " + file.string());
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void FileStore::writeAtomically(const fs::path& file, const std::string& content)
{
  std::error_code ec;
  fs::create_directories(file.parent_path(), ec);
  const fs::path temp =
    file.parent_path() / ("." + file.filename().string() + ".agentic-tmp-" + generateToken(4));
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out) throw ToolError("cannot write file: " + file.string());
    out << content;
    if (!out) {
      fs::remove(temp, ec);
      throw ToolError("cannot write file: " + file.string());
    }
  }
  fs::rename(temp, file, ec);
  if (ec) {
    fs::remove(temp, ec);
    throw ToolError("cannot replace file: " + file.string() + ": " + ec.message());
  }
}

fs::path FileStore::resolveExisting(const std::string& path) const
{
  fs::path abs;
  try {
    abs = project_.resolve(path);
  } catch (const std::invalid_argument& e) {
    throw ToolError(e.what());
  }
  std::error_code ec;
  if (!fs::is_regular_file(abs, ec)) throw ToolError("file not found: " + path);
  return abs;
}

std::vector<FileEntry> FileStore::list(const std::string& dir, bool recursive, bool includeHidden,
                                       bool *truncated) const
{
  fs::path root;
  try {
    root = project_.resolve(dir.empty() ? "." : dir);
  } catch (const std::invalid_argument& e) {
    throw ToolError(e.what());
  }
  std::error_code ec;
  if (!fs::is_directory(root, ec)) throw ToolError("directory not found: " + dir);

  std::vector<FileEntry> entries;
  if (truncated) *truncated = false;
  auto addEntry = [&](const fs::directory_entry& entry) {
    if (!entry.is_regular_file(ec)) return true;
    if (entries.size() >= kMaxListEntries) {
      if (truncated) *truncated = true;
      return false;
    }
    FileEntry fe;
    fe.path = project_.relative(entry.path());
    fe.size = entry.file_size(ec);
    fe.modified = isoTime(entry.last_write_time(ec));
    fe.version = versionOf(readAll(entry.path()));
    entries.push_back(std::move(fe));
    return true;
  };

  if (recursive) {
    for (auto it =
           fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
      if (ec) break;
      if (!includeHidden && isHidden(it->path())) {
        if (it->is_directory(ec)) it.disable_recursion_pending();
        continue;
      }
      if (!addEntry(*it)) break;
    }
  } else {
    for (const auto& entry :
         fs::directory_iterator(root, fs::directory_options::skip_permission_denied, ec)) {
      if (!includeHidden && isHidden(entry.path())) continue;
      if (!addEntry(entry)) break;
    }
  }
  std::sort(entries.begin(), entries.end(),
            [](const FileEntry& a, const FileEntry& b) { return a.path < b.path; });
  return entries;
}

FileContent FileStore::read(const std::string& path, size_t startLine, size_t maxLines) const
{
  const fs::path abs = resolveExisting(path);
  const std::string whole = readAll(abs);
  FileContent result;
  result.path = project_.relative(abs);
  result.version = versionOf(whole);
  result.lineCount = countLines(whole);
  result.startLine = startLine == 0 ? 1 : startLine;
  if (result.startLine == 1 && maxLines == 0) {
    result.content = whole;
    return result;
  }
  const auto lines = splitLines(whole);
  if (result.startLine > lines.size()) {
    result.content.clear();
    return result;
  }
  const size_t first = result.startLine - 1;
  const size_t last = maxLines == 0 ? lines.size() : std::min(lines.size(), first + maxLines);
  std::string window;
  for (size_t i = first; i < last; ++i) {
    window += lines[i];
    if (i + 1 < lines.size() || (!whole.empty() && whole.back() == '\n')) window += '\n';
  }
  result.content = window;
  result.truncated = last < lines.size();
  return result;
}

std::optional<std::string> FileStore::currentVersion(const std::string& path) const
{
  fs::path abs;
  try {
    abs = project_.resolve(path);
  } catch (const std::invalid_argument& e) {
    throw ToolError(e.what());
  }
  std::error_code ec;
  if (!fs::is_regular_file(abs, ec)) return std::nullopt;
  return versionOf(readAll(abs));
}

FileChange FileStore::write(const std::string& path, const std::string& content,
                            const std::optional<std::string>& baseVersion)
{
  fs::path abs;
  try {
    abs = project_.resolve(path);
  } catch (const std::invalid_argument& e) {
    throw ToolError(e.what());
  }
  std::error_code ec;
  if (fs::is_directory(abs, ec)) throw ToolError("path is a directory: " + path);
  if (abs.filename() == Project::kConfigFileName) {
    throw ToolError("use project_set to change the project configuration");
  }

  FileChange change;
  change.path = project_.relative(abs);
  change.absolute = abs;
  change.created = !fs::exists(abs, ec);
  if (!change.created) {
    change.previousContent = readAll(abs);
    change.previousVersion = versionOf(change.previousContent);
    if (baseVersion && *baseVersion != change.previousVersion) {
      throw ConflictError(change.path, change.previousVersion, *baseVersion);
    }
  } else if (baseVersion && !baseVersion->empty()) {
    throw ConflictError(change.path, "", *baseVersion);
  }
  writeAtomically(abs, content);
  change.content = content;
  change.version = versionOf(content);
  change.hunk = singleHunkDiff(change.previousContent, content);
  return change;
}

std::string FileStore::applyEdits(const std::string& content, const std::vector<TextEdit>& edits)
{
  std::string result = content;
  size_t index = 0;
  for (const auto& edit : edits) {
    ++index;
    if (edit.oldText.empty())
      throw ToolError("edit " + std::to_string(index) + ": 'old' must not be empty");
    const size_t first = result.find(edit.oldText);
    if (first == std::string::npos) {
      throw ToolError("edit " + std::to_string(index) + ": text not found: " +
                      (edit.oldText.size() > 80 ? edit.oldText.substr(0, 80) + "..." : edit.oldText));
    }
    if (edit.replaceAll) {
      size_t pos = first;
      while (pos != std::string::npos) {
        result.replace(pos, edit.oldText.size(), edit.newText);
        pos = result.find(edit.oldText, pos + edit.newText.size());
      }
      continue;
    }
    if (result.find(edit.oldText, first + 1) != std::string::npos) {
      throw ToolError("edit " + std::to_string(index) +
                      ": text occurs more than once; include more context or set replaceAll");
    }
    result.replace(first, edit.oldText.size(), edit.newText);
  }
  return result;
}

FileChange FileStore::patch(const std::string& path, const std::vector<TextEdit>& edits,
                            const std::optional<std::string>& baseVersion)
{
  const fs::path abs = resolveExisting(path);
  const std::string current = readAll(abs);
  const std::string currentVersion = versionOf(current);
  if (baseVersion && *baseVersion != currentVersion) {
    throw ConflictError(project_.relative(abs), currentVersion, *baseVersion);
  }
  return write(path, applyEdits(current, edits), currentVersion);
}

void FileStore::rename(const std::string& from, const std::string& to)
{
  const fs::path source = resolveExisting(from);
  fs::path target;
  try {
    target = project_.resolve(to);
  } catch (const std::invalid_argument& e) {
    throw ToolError(e.what());
  }
  std::error_code ec;
  if (fs::exists(target, ec)) throw ToolError("target already exists: " + to);
  fs::create_directories(target.parent_path(), ec);
  fs::rename(source, target, ec);
  if (ec) throw ToolError("cannot rename " + from + " to " + to + ": " + ec.message());
}

void FileStore::remove(const std::string& path)
{
  const fs::path abs = resolveExisting(path);
  if (abs.filename() == Project::kConfigFileName)
    throw ToolError("the project configuration cannot be deleted");
  std::error_code ec;
  fs::remove(abs, ec);
  if (ec) throw ToolError("cannot delete " + path + ": " + ec.message());
}

}  // namespace mcp
