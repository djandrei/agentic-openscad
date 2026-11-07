#include "mcp/library/Archive.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <zip.h>

#include "mcp/protocol/Registry.h"

namespace fs = std::filesystem;

namespace mcp::library {

namespace {

bool safeRelative(const std::string& name)
{
  if (name.empty() || name[0] == '/' || name[0] == '\\') return false;
  if (name.find("\\") != std::string::npos) return false;
  if (name.size() > 1 && name[1] == ':') return false;
  for (const auto& part : fs::path(name)) {
    if (part == "..") return false;
  }
  return true;
}

std::string topLevelFolder(const std::vector<std::string>& names)
{
  std::string folder;
  for (const auto& name : names) {
    const auto slash = name.find('/');
    if (slash == std::string::npos) return "";  // a file at the root: nothing to strip
    const std::string first = name.substr(0, slash + 1);
    if (folder.empty()) folder = first;
    else if (first != folder) return "";
  }
  return folder;
}

struct ZipCloser {
  void operator()(zip_t *zip) const
  {
    if (zip) zip_discard(zip);
  }
};

}  // namespace

std::vector<std::string> extractZip(const std::string& bytes, const fs::path& target)
{
  zip_error_t error;
  zip_error_init(&error);
  zip_source_t *source = zip_source_buffer_create(bytes.data(), bytes.size(), 0, &error);
  if (!source) throw ToolError("cannot read archive: " + std::string(zip_error_strerror(&error)));
  std::unique_ptr<zip_t, ZipCloser> zip(zip_open_from_source(source, ZIP_RDONLY, &error));
  if (!zip) {
    zip_source_free(source);
    throw ToolError("not a zip archive: " + std::string(zip_error_strerror(&error)));
  }

  const zip_int64_t count = zip_get_num_entries(zip.get(), 0);
  std::vector<std::string> names;
  for (zip_int64_t i = 0; i < count; ++i) {
    const char *name = zip_get_name(zip.get(), static_cast<zip_uint64_t>(i), ZIP_FL_ENC_GUESS);
    if (!name) continue;
    names.emplace_back(name);
  }
  const std::string strip = topLevelFolder(names);

  std::vector<std::string> extracted;
  std::error_code ec;
  fs::create_directories(target, ec);
  for (zip_int64_t i = 0; i < count; ++i) {
    const std::string name = names[static_cast<size_t>(i)];
    if (!safeRelative(name)) throw ToolError("archive contains an unsafe path: " + name);
    std::string relative = name.substr(strip.size());
    if (relative.empty()) continue;
    const fs::path path = target / relative;
    if (relative.back() == '/') {
      fs::create_directories(path, ec);
      continue;
    }
    zip_stat_t stat;
    if (zip_stat_index(zip.get(), static_cast<zip_uint64_t>(i), 0, &stat) != 0) continue;
    zip_file_t *file = zip_fopen_index(zip.get(), static_cast<zip_uint64_t>(i), 0);
    if (!file) throw ToolError("cannot read archive entry: " + name);
    std::string content(stat.size, '\0');
    zip_int64_t read = 0;
    while (read < static_cast<zip_int64_t>(stat.size)) {
      const zip_int64_t n = zip_fread(file, content.data() + read, stat.size - read);
      if (n <= 0) break;
      read += n;
    }
    zip_fclose(file);
    fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw ToolError("cannot write " + path.string());
    out.write(content.data(), read);
    extracted.push_back(relative);
  }
  return extracted;
}

std::string createZip(const std::vector<std::pair<std::string, std::string>>& entries)
{
  zip_error_t error;
  zip_error_init(&error);
  zip_source_t *target = zip_source_buffer_create(nullptr, 0, 0, &error);
  if (!target) throw ToolError("cannot create archive");
  zip_source_keep(target);
  zip_t *zip = zip_open_from_source(target, ZIP_TRUNCATE, &error);
  if (!zip) {
    zip_source_free(target);
    throw ToolError("cannot create archive: " + std::string(zip_error_strerror(&error)));
  }
  for (const auto& [name, content] : entries) {
    zip_source_t *entry = zip_source_buffer(zip, content.data(), content.size(), 0);
    if (!entry || zip_file_add(zip, name.c_str(), entry, ZIP_FL_ENC_UTF_8) < 0) {
      if (entry) zip_source_free(entry);
      zip_discard(zip);
      zip_source_free(target);
      throw ToolError("cannot add " + name + " to archive");
    }
  }
  if (zip_close(zip) != 0) {
    zip_source_free(target);
    throw ToolError("cannot finish archive");
  }
  std::string bytes;
  if (zip_source_open(target) == 0) {
    zip_source_seek(target, 0, SEEK_END);
    const zip_int64_t size = zip_source_tell(target);
    zip_source_seek(target, 0, SEEK_SET);
    bytes.resize(static_cast<size_t>(size));
    zip_source_read(target, bytes.data(), static_cast<zip_uint64_t>(size));
    zip_source_close(target);
  }
  zip_source_free(target);
  return bytes;
}

}  // namespace mcp::library
