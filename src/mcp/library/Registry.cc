#include "mcp/library/Registry.h"

#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

#include "mcp/library/Download.h"
#include "mcp/session/Project.h"

namespace mcp::library {

using nlohmann::json;

json LibraryEntry::toJson() const
{
  return json{{"name", name},
              {"description", description},
              {"repository", repository},
              {"ref", ref},
              {"license", license},
              {"docs", docs},
              {"category", category},
              {"usage", usage},
              {"bundled", bundled},
              {"registry", source}};
}

std::vector<LibraryEntry> parseRegistry(const json& document, const std::string& sourceName)
{
  if (!document.is_object() || !document.contains("libraries") || !document["libraries"].is_array()) {
    throw std::runtime_error("registry must be an object with a 'libraries' array");
  }
  std::vector<LibraryEntry> entries;
  for (const auto& item : document["libraries"]) {
    if (!item.is_object() || !item.contains("name")) continue;
    LibraryEntry entry;
    entry.name = item.value("name", "");
    entry.description = item.value("description", "");
    entry.repository = item.value("repository", "");
    entry.ref = item.value("ref", "");
    entry.license = item.value("license", "");
    entry.docs = item.value("docs", "");
    entry.category = item.value("category", "");
    entry.usage = item.value("usage", "");
    entry.bundled = item.value("bundled", false);
    entry.source = document.value("name", sourceName);
    if (!entry.name.empty()) entries.push_back(std::move(entry));
  }
  return entries;
}

namespace {

std::string lower(std::string s)
{
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
  return s;
}

void merge(std::vector<LibraryEntry>& into, const std::vector<LibraryEntry>& extra)
{
  for (const auto& entry : extra) {
    auto existing = std::find_if(into.begin(), into.end(), [&](const LibraryEntry& e) {
      return lower(e.name) == lower(entry.name);
    });
    if (existing != into.end()) *existing = entry;
    else into.push_back(entry);
  }
}

}  // namespace

std::vector<LibraryEntry> loadRegistries(const Project& project, std::vector<std::string> *problems)
{
  std::vector<LibraryEntry> entries = parseRegistry(json::parse(bundledRegistryJson()), "bundled");
  const json registries = project.config().value("registries", json::array());
  for (const auto& item : registries) {
    if (!item.is_string()) continue;
    const std::string location = item.get<std::string>();
    try {
      std::string text;
      if (location.rfind("http://", 0) == 0 || location.rfind("https://", 0) == 0 ||
          location.rfind("file://", 0) == 0) {
        text = download(location);
      } else {
        text = download(project.resolve(location).string());
      }
      merge(entries, parseRegistry(json::parse(text), location));
    } catch (const std::exception& e) {
      if (problems) problems->push_back(location + ": " + e.what());
    }
  }
  return entries;
}

const LibraryEntry *findEntry(const std::vector<LibraryEntry>& entries, const std::string& name)
{
  const std::string wanted = lower(name);
  for (const auto& entry : entries) {
    if (lower(entry.name) == wanted) return &entry;
  }
  return nullptr;
}

std::string repositoryName(const std::string& repository)
{
  std::string trimmed = repository;
  while (!trimmed.empty() && trimmed.back() == '/') trimmed.pop_back();
  const auto slash = trimmed.rfind('/');
  std::string name = slash == std::string::npos ? trimmed : trimmed.substr(slash + 1);
  if (name.size() > 4 && name.compare(name.size() - 4, 4, ".git") == 0) name.resize(name.size() - 4);
  return name;
}

std::vector<std::string> archiveCandidates(const std::string& repository, const std::string& ref)
{
  // https://host/owner/repo[.git][/]
  std::string rest = repository;
  const auto scheme = rest.find("://");
  if (scheme == std::string::npos) return {};
  rest = rest.substr(scheme + 3);
  while (!rest.empty() && rest.back() == '/') rest.pop_back();
  const auto firstSlash = rest.find('/');
  if (firstSlash == std::string::npos) return {};
  const std::string host = lower(rest.substr(0, firstSlash));
  std::string path = rest.substr(firstSlash + 1);
  if (path.size() > 4 && path.compare(path.size() - 4, 4, ".git") == 0) path.resize(path.size() - 4);
  const auto secondSlash = path.find('/');
  if (secondSlash == std::string::npos || path.find('/', secondSlash + 1) != std::string::npos)
    return {};
  const std::string owner = path.substr(0, secondSlash);
  const std::string repo = path.substr(secondSlash + 1);

  const std::vector<std::string> refs =
    ref.empty() ? std::vector<std::string>{"main", "master"} : std::vector<std::string>{ref};
  std::vector<std::string> urls;
  for (const auto& r : refs) {
    if (host == "github.com" || host == "www.github.com") {
      urls.push_back("https://codeload.github.com/" + owner + "/" + repo + "/zip/refs/heads/" + r);
      urls.push_back("https://codeload.github.com/" + owner + "/" + repo + "/zip/refs/tags/" + r);
    } else if (host == "gitlab.com") {
      urls.push_back("https://gitlab.com/" + owner + "/" + repo + "/-/archive/" + r + "/" + repo + "-" +
                     r + ".zip");
    } else if (host == "codeberg.org") {
      urls.push_back("https://codeberg.org/" + owner + "/" + repo + "/archive/" + r + ".zip");
    }
  }
  return urls;
}

}  // namespace mcp::library
