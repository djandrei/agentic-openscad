#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "json/json.hpp"
#include "mcp/docs/SearchIndex.h"

namespace mcp {
class Project;
}

namespace mcp::docs {

const std::string& builtinDocsJson();

// Documentation for the agent: builtin signatures (from OpenSCAD's own
// call-tip table) with short semantics, the offline User Manual and the
// cheat sheet (downloaded once into a cache folder), and the documentation
// files of a project's installed libraries. Everything is searchable.
class Docs
{
public:
  static constexpr const char *kManualUrl = "https://files.openscad.org/documentation/manual.zip";
  static constexpr const char *kCheatsheetUrl = "https://www.openscad.org/cheatsheet/index.html";

  static Docs& instance();

  // $AGENTIC_OPENSCAD_DOCS_CACHE, else $XDG_CACHE_HOME or ~/.cache under
  // agentic-openscad/docs, else the temp folder.
  std::filesystem::path cacheDir() const;

  // Loads the cached manual and cheat sheet into the index (no network).
  // Returns true when the manual is available.
  bool ensureLoaded();
  nlohmann::json status();
  // Downloads the manual and cheat sheet (network) and rebuilds the index.
  nlohmann::json update(bool force);

  nlohmann::json builtins() const;
  nlohmann::json builtin(const std::string& name) const;
  nlohmann::json manualPages();
  // "builtins", "builtins/<name>", "cheatsheet", "manual", "manual/<Page>".
  std::optional<std::string> page(const std::string& id);
  // Documentation files (README, *.md, *.txt) of the project's installed libraries.
  std::vector<Document> libraryDocuments(const Project& project) const;
  std::optional<std::string> libraryDocument(const Project& project, const std::string& path) const;

  // scope: all | manual | cheatsheet | builtins | libraries.
  nlohmann::json search(const std::string& query, const std::string& scope, size_t limit,
                        const Project *project);

private:
  Docs() = default;
  void rebuildIndex();
  std::string builtinText(const std::string& name) const;
  std::string builtinsText() const;

  bool loaded_ = false;
  bool manualAvailable_ = false;
  bool cheatsheetAvailable_ = false;
  SearchIndex index_;
  std::map<std::string, std::string> pages_;                     // id -> text
  std::vector<std::pair<std::string, std::string>> pageTitles_;  // manual id -> title, in order
};

}  // namespace mcp::docs
