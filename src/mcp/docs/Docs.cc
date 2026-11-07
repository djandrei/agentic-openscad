#include "mcp/docs/Docs.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/Builtins.h"
#include "mcp/docs/Html.h"
#include "mcp/library/Archive.h"
#include "mcp/library/Download.h"
#include "mcp/protocol/Registry.h"
#include "mcp/session/Project.h"

namespace fs = std::filesystem;

namespace mcp::docs {

using nlohmann::json;

namespace {

std::string readFile(const fs::path& path)
{
  std::ifstream in(path, std::ios::binary);
  if (!in) return "";
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::string lower(std::string s)
{
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
  return s;
}

const json& builtinDocs()
{
  static const json docs = json::parse(builtinDocsJson());
  return docs;
}

std::string pageIdFromFile(const fs::path& file)
{
  return file.stem().string();  // e.g. Primitive_Solids
}

std::string prettyTitle(const std::string& id)
{
  std::string title = id;
  std::replace(title.begin(), title.end(), '_', ' ');
  return title;
}

}  // namespace

Docs& Docs::instance()
{
  static Docs docs;
  return docs;
}

fs::path Docs::cacheDir() const
{
  if (const char *env = std::getenv("AGENTIC_OPENSCAD_DOCS_CACHE"); env && *env) return fs::path(env);
  if (const char *xdg = std::getenv("XDG_CACHE_HOME"); xdg && *xdg) {
    return fs::path(xdg) / "agentic-openscad" / "docs";
  }
  if (const char *home = std::getenv("HOME"); home && *home) {
    return fs::path(home) / ".cache" / "agentic-openscad" / "docs";
  }
  return fs::temp_directory_path() / "agentic-openscad" / "docs";
}

std::string Docs::builtinText(const std::string& name) const
{
  std::string text = name;
  const auto it = Builtins::keywordList.find(name);
  if (it != Builtins::keywordList.end()) {
    for (const auto& signature : it->second) text += "\n  " + signature;
  }
  if (builtinDocs().contains(name)) text += "\n" + builtinDocs()[name].get<std::string>();
  return text;
}

json Docs::builtin(const std::string& name) const
{
  const auto& modules = Builtins::instance().getModules();
  const auto& functions = Builtins::instance().getFunctions();
  const auto keyword = Builtins::keywordList.find(name);
  const bool documented = builtinDocs().contains(name);
  if (keyword == Builtins::keywordList.end() && !documented) return nullptr;
  json entry{{"name", name}};
  if (modules.count(name)) entry["kind"] = "module";
  else if (functions.count(name)) entry["kind"] = "function";
  else if (!name.empty() && name[0] == '$') entry["kind"] = "special variable";
  else entry["kind"] = "keyword";
  entry["signatures"] = keyword != Builtins::keywordList.end() ? json(keyword->second) : json::array();
  if (documented) entry["summary"] = builtinDocs()[name];
  const std::string deprecated = Builtins::instance().isDeprecated(name);
  if (!deprecated.empty()) entry["deprecated"] = deprecated;
  return entry;
}

json Docs::builtins() const
{
  std::vector<std::string> names;
  for (const auto& [name, tips] : Builtins::keywordList) names.push_back(name);
  for (const auto& [name, summary] : builtinDocs().items()) {
    if (std::find(names.begin(), names.end(), name) == names.end()) names.push_back(name);
  }
  std::sort(names.begin(), names.end());
  json list = json::array();
  for (const auto& name : names) {
    json entry = builtin(name);
    if (!entry.is_null()) list.push_back(entry);
  }
  return list;
}

std::string Docs::builtinsText() const
{
  std::string text = "# OpenSCAD builtins\n\n";
  for (const auto& entry : builtins()) {
    text += entry["name"].get<std::string>() + " (" + entry["kind"].get<std::string>() + ")\n";
    for (const auto& signature : entry["signatures"]) text += "  " + signature.get<std::string>() + "\n";
    if (entry.contains("summary")) text += "  " + entry["summary"].get<std::string>() + "\n";
    text += "\n";
  }
  return text;
}

void Docs::rebuildIndex()
{
  index_.clear();
  pages_.clear();
  pageTitles_.clear();
  manualAvailable_ = false;
  cheatsheetAvailable_ = false;

  for (const auto& entry : builtins()) {
    const std::string name = entry["name"];
    Document doc{"builtins/" + name, name, "builtins", builtinText(name)};
    pages_[doc.id] = doc.text;
    index_.add(std::move(doc));
  }
  pages_["builtins"] = builtinsText();

  std::error_code ec;
  const fs::path manualDir = cacheDir() / "manual";
  std::vector<fs::path> files;
  for (const auto& entry : fs::directory_iterator(manualDir, ec)) {
    if (entry.is_regular_file(ec) && lower(entry.path().extension().string()) == ".html") {
      files.push_back(entry.path());
    }
  }
  std::sort(files.begin(), files.end());
  for (const auto& file : files) {
    const std::string html = readFile(file);
    const std::string id = pageIdFromFile(file);
    std::string title = htmlTitle(html);
    if (title.empty()) title = prettyTitle(id);
    Document doc{"manual/" + id, title, "manual", htmlToText(html)};
    pages_[doc.id] = doc.text;
    pageTitles_.emplace_back(id, title);
    index_.add(std::move(doc));
  }
  manualAvailable_ = !files.empty();

  const fs::path cheatsheet = cacheDir() / "cheatsheet.html";
  if (fs::is_regular_file(cheatsheet, ec)) {
    Document doc{"cheatsheet", "OpenSCAD Cheat Sheet", "cheatsheet", htmlToText(readFile(cheatsheet))};
    pages_[doc.id] = doc.text;
    index_.add(std::move(doc));
    cheatsheetAvailable_ = true;
  }
  loaded_ = true;
}

bool Docs::ensureLoaded()
{
  if (!loaded_) rebuildIndex();
  return manualAvailable_;
}

json Docs::status()
{
  ensureLoaded();
  return json{{"cacheDir", cacheDir().string()},   {"manualAvailable", manualAvailable_},
              {"manualPages", pageTitles_.size()}, {"cheatsheetAvailable", cheatsheetAvailable_},
              {"builtins", builtins().size()},     {"indexedDocuments", index_.size()},
              {"manualUrl", kManualUrl},           {"cheatsheetUrl", kCheatsheetUrl}};
}

json Docs::update(bool force)
{
  std::error_code ec;
  const fs::path dir = cacheDir();
  fs::create_directories(dir, ec);
  json result{{"cacheDir", dir.string()}};
  const bool haveManual = fs::is_directory(dir / "manual", ec) && !fs::is_empty(dir / "manual", ec);
  if (force || !haveManual) {
    const std::string archive = library::download(kManualUrl);
    fs::remove_all(dir / "manual", ec);
    result["manualFiles"] = library::extractZip(archive, dir / "manual").size();
  }
  if (force || !fs::is_regular_file(dir / "cheatsheet.html", ec)) {
    const std::string html = library::download(kCheatsheetUrl);
    std::ofstream out(dir / "cheatsheet.html", std::ios::binary | std::ios::trunc);
    out << html;
    result["cheatsheetBytes"] = html.size();
  }
  rebuildIndex();
  result.update(status());
  return result;
}

json Docs::manualPages()
{
  ensureLoaded();
  json list = json::array();
  for (const auto& [id, title] : pageTitles_)
    list.push_back(json{{"id", "manual/" + id}, {"title", title}});
  return list;
}

std::optional<std::string> Docs::page(const std::string& id)
{
  ensureLoaded();
  if (id == "manual") {
    std::string text = "# OpenSCAD User Manual (offline copy)\n\n";
    for (const auto& [pageId, title] : pageTitles_) text += "manual/" + pageId + "  " + title + "\n";
    if (pageTitles_.empty()) text += "(not downloaded; call docs_update)\n";
    return text;
  }
  const auto it = pages_.find(id);
  if (it == pages_.end()) return std::nullopt;
  return it->second;
}

std::vector<Document> Docs::libraryDocuments(const Project& project) const
{
  std::vector<Document> documents;
  std::error_code ec;
  const auto libraries = project.config().value("libraries", json::object());
  const fs::path root = project.root() / libraries.value("dir", "libraries");
  if (!fs::is_directory(root, ec)) return documents;
  for (const auto& library : fs::directory_iterator(root, ec)) {
    if (!library.is_directory(ec)) continue;
    size_t count = 0;
    for (auto it = fs::recursive_directory_iterator(library.path(), ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
      if (!it->is_regular_file(ec) || count >= 50) continue;
      const std::string ext = lower(it->path().extension().string());
      const std::string stem = lower(it->path().stem().string());
      if (ext != ".md" && ext != ".txt" && ext != ".rst" && stem != "readme" && stem != "license")
        continue;
      if (it->file_size(ec) > 300000) continue;
      const std::string relative = it->path().lexically_relative(root).generic_string();
      documents.push_back(Document{"library/" + relative, relative, "libraries", readFile(it->path())});
      ++count;
    }
  }
  return documents;
}

std::optional<std::string> Docs::libraryDocument(const Project& project, const std::string& path) const
{
  for (const auto& doc : libraryDocuments(project)) {
    if (doc.id == "library/" + path || doc.id == path) return doc.text;
  }
  return std::nullopt;
}

json Docs::search(const std::string& query, const std::string& scope, size_t limit,
                  const Project *project)
{
  ensureLoaded();
  json hits = json::array();
  auto append = [&](const std::vector<Hit>& found) {
    for (const auto& hit : found) {
      hits.push_back(json{{"id", hit.id},
                          {"title", hit.title},
                          {"scope", hit.scope},
                          {"score", hit.score},
                          {"snippet", hit.snippet}});
    }
  };
  if (scope != "libraries") append(index_.search(query, scope, limit));
  if ((scope == "all" || scope.empty() || scope == "libraries") && project) {
    SearchIndex libraries;
    for (auto& doc : libraryDocuments(*project)) libraries.add(std::move(doc));
    append(libraries.search(query, "libraries", limit));
  }
  std::sort(hits.begin(), hits.end(), [](const json& a, const json& b) {
    return a["score"].get<double>() > b["score"].get<double>();
  });
  if (hits.size() > limit) hits.erase(hits.begin() + static_cast<long>(limit), hits.end());
  json result{{"query", query}, {"scope", scope.empty() ? "all" : scope}, {"hits", hits}};
  if (!manualAvailable_ && (scope.empty() || scope == "all" || scope == "manual")) {
    result["note"] = "the offline User Manual is not downloaded yet; call docs_update to fetch it";
  }
  return result;
}

}  // namespace mcp::docs
