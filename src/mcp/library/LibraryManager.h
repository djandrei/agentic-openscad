#pragma once

#include <filesystem>
#include <string>

#include "json/json.hpp"

namespace mcp {
class Project;
}

namespace mcp::library {

// Installs, lists and removes OpenSCAD libraries inside a project's
// `libraries` folder and records them in .openscad-project.json. No Qt.
class LibraryManager
{
public:
  explicit LibraryManager(Project& project) : project_(project) {}

  std::filesystem::path librariesDir() const;

  // Registry entries (bundled + project registries) with an `installed` flag,
  // optionally filtered by a case-insensitive substring of name/description.
  nlohmann::json catalog(const std::string& query) const;
  nlohmann::json installed() const;
  nlohmann::json registries() const;
  nlohmann::json addRegistry(const std::string& location);

  // `nameOrUrl`: a registry name, a git repository URL, a URL or path of a
  // zip archive. `ref`: branch/tag (optional). `dirName`: folder under
  // libraries/ (defaults to the registry name or repository name).
  nlohmann::json install(const std::string& nameOrUrl, const std::string& ref,
                         const std::string& dirName);
  nlohmann::json remove(const std::string& name);

  // Files of an installed library; with `describe`, the top-level modules
  // and functions of its .scad files (parsed; limited to `maxDescribed`
  // files unless `filter` narrows the set).
  nlohmann::json files(const std::string& name, const std::string& filter, bool describe) const;

private:
  static constexpr size_t kMaxDescribedFiles = 60;

  nlohmann::json installFromArchive(const std::string& bytes, const std::filesystem::path& dir);
  nlohmann::json installWithGit(const std::string& repository, const std::string& ref,
                                const std::filesystem::path& dir);
  nlohmann::json record(const std::string& name, const nlohmann::json& info);

  Project& project_;
};

}  // namespace mcp::library
