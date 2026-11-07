#include "mcp/library/LibraryManager.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "core/Assignment.h"
#include "core/LocalScope.h"
#include "core/SourceFile.h"
#include "core/UserModule.h"
#include "core/function.h"
#include "mcp/engine/CompileEngine.h"
#include "mcp/library/Archive.h"
#include "mcp/library/Download.h"
#include "mcp/library/Process.h"
#include "mcp/library/Registry.h"
#include "mcp/protocol/Registry.h"
#include "mcp/session/Project.h"

namespace fs = std::filesystem;

namespace mcp::library {

using nlohmann::json;

namespace {

std::string lower(std::string s)
{
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
  return s;
}

std::string nowIso()
{
  const std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm tm{};
  gmtime_r(&t, &tm);
  std::ostringstream out;
  out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
  return out.str();
}

bool validDirName(const std::string& name)
{
  if (name.empty() || name == "." || name == "..") return false;
  return name.find('/') == std::string::npos && name.find('\\') == std::string::npos;
}

bool isUrl(const std::string& s)
{
  return s.rfind("http://", 0) == 0 || s.rfind("https://", 0) == 0 || s.rfind("file://", 0) == 0;
}

bool endsWith(const std::string& s, const std::string& suffix)
{
  return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

json parameterNames(const AssignmentList& parameters)
{
  json list = json::array();
  for (const auto& assignment : parameters) list.push_back(assignment->getName());
  return list;
}

}  // namespace

fs::path LibraryManager::librariesDir() const
{
  const std::string dir = project_.config().value("libraries", json::object()).value("dir", "libraries");
  return project_.root() / dir;
}

json LibraryManager::installed() const
{
  const json installedConfig =
    project_.config().value("libraries", json::object()).value("installed", json::object());
  json list = json::array();
  std::error_code ec;
  for (const auto& [name, info] : installedConfig.items()) {
    json entry = info;
    entry["name"] = name;
    const fs::path dir = librariesDir() / info.value("dir", name);
    entry["present"] = fs::is_directory(dir, ec);
    list.push_back(entry);
  }
  return json{{"dir", project_.relative(librariesDir())}, {"libraries", list}};
}

json LibraryManager::catalog(const std::string& query) const
{
  std::vector<std::string> problems;
  const auto entries = loadRegistries(project_, &problems);
  const json installedConfig =
    project_.config().value("libraries", json::object()).value("installed", json::object());
  const std::string needle = lower(query);
  json list = json::array();
  for (const auto& entry : entries) {
    if (!needle.empty() && lower(entry.name).find(needle) == std::string::npos &&
        lower(entry.description).find(needle) == std::string::npos) {
      continue;
    }
    json item = entry.toJson();
    item["installed"] = installedConfig.contains(entry.name) || entry.bundled;
    list.push_back(item);
  }
  return json{{"libraries", list}, {"problems", problems}};
}

json LibraryManager::registries() const
{
  json list = json::array({"bundled (openscad.org)"});
  for (const auto& item : project_.config().value("registries", json::array())) list.push_back(item);
  return json{{"registries", list}};
}

json LibraryManager::addRegistry(const std::string& location)
{
  // Validate before recording.
  std::string text;
  if (isUrl(location)) {
    text = download(location);
  } else {
    try {
      text = download(project_.resolve(location).string());
    } catch (const std::invalid_argument& e) {
      throw ToolError(e.what());
    }
  }
  std::vector<LibraryEntry> entries;
  try {
    entries = parseRegistry(json::parse(text), location);
  } catch (const std::exception& e) {
    throw ToolError("invalid registry " + location + ": " + e.what());
  }
  json& registries = project_.config()["registries"];
  if (!registries.is_array()) registries = json::array();
  if (std::find(registries.begin(), registries.end(), json(location)) == registries.end()) {
    registries.push_back(location);
  }
  project_.save();
  json result = this->registries();
  result["added"] = location;
  result["entries"] = entries.size();
  return result;
}

json LibraryManager::record(const std::string& name, const json& info)
{
  json& libraries = project_.config()["libraries"];
  if (!libraries.is_object()) libraries = json::object();
  if (!libraries["installed"].is_object()) libraries["installed"] = json::object();
  libraries["installed"][name] = info;
  project_.save();
  json result = info;
  result["name"] = name;
  return result;
}

json LibraryManager::installFromArchive(const std::string& bytes, const fs::path& dir)
{
  std::error_code ec;
  fs::remove_all(dir, ec);
  const auto files = extractZip(bytes, dir);
  return json{{"files", files.size()}};
}

json LibraryManager::installWithGit(const std::string& repository, const std::string& ref,
                                    const fs::path& dir)
{
  if (!programAvailable("git")) {
    throw ToolError("cannot download " + repository +
                    ": no archive URL is known for this host and git is not available");
  }
  std::error_code ec;
  fs::remove_all(dir, ec);
  std::vector<std::string> args{"git", "clone", "--depth", "1"};
  if (!ref.empty()) {
    args.push_back("--branch");
    args.push_back(ref);
  }
  args.push_back(repository);
  args.push_back(dir.string());
  std::string output;
  if (runProcess(args, &output) != 0) throw ToolError("git clone failed: " + output);
  fs::remove_all(dir / ".git", ec);
  size_t count = 0;
  for (auto it = fs::recursive_directory_iterator(dir, ec); it != fs::recursive_directory_iterator();
       it.increment(ec)) {
    if (it->is_regular_file(ec)) ++count;
  }
  return json{{"files", count}, {"method", "git"}};
}

json LibraryManager::install(const std::string& nameOrUrl, const std::string& ref,
                             const std::string& dirName)
{
  std::vector<std::string> problems;
  const auto entries = loadRegistries(project_, &problems);
  const LibraryEntry *entry = findEntry(entries, nameOrUrl);
  if (entry && entry->bundled) {
    throw ToolError(entry->name + " is bundled with OpenSCAD; use it directly: " + entry->usage);
  }

  std::string repository = entry ? entry->repository : nameOrUrl;
  std::string useRef = ref.empty() && entry ? entry->ref : ref;
  std::string name = dirName.empty() ? (entry ? entry->name : repositoryName(repository)) : dirName;
  if (endsWith(lower(name), ".zip")) name = name.substr(0, name.size() - 4);
  if (!validDirName(name)) throw ToolError("invalid library folder name: " + name);
  const fs::path dir = librariesDir() / name;

  json info{{"repository", repository}, {"ref", useRef}, {"dir", name}, {"installedAt", nowIso()}};
  if (entry) info["registry"] = entry->source;

  const bool archiveSource = endsWith(lower(repository), ".zip") || (!isUrl(repository) && !entry);
  if (archiveSource) {
    info.update(installFromArchive(download(repository), dir));
    info["method"] = "archive";
    info["url"] = repository;
    return record(name, info);
  }

  const auto candidates = archiveCandidates(repository, useRef);
  std::string lastError;
  for (const auto& url : candidates) {
    try {
      info.update(installFromArchive(download(url), dir));
      info["method"] = "archive";
      info["url"] = url;
      if (useRef.empty()) {
        // Record which default branch answered.
        info["ref"] = url.find("/master") != std::string::npos ? "master" : "main";
      }
      return record(name, info);
    } catch (const ToolError& e) {
      lastError = e.what();
      if (!e.data().is_object() || e.data().value("status", 0) != 404) throw;
    }
  }
  if (!candidates.empty() && !programAvailable("git")) {
    throw ToolError("no archive found for " + repository + (useRef.empty() ? "" : " at " + useRef) +
                    " (" + lastError + ")");
  }
  info.update(installWithGit(repository, useRef, dir));
  return record(name, info);
}

json LibraryManager::remove(const std::string& name)
{
  json& libraries = project_.config()["libraries"];
  const bool known =
    libraries.is_object() && libraries.value("installed", json::object()).contains(name);
  if (!validDirName(name)) throw ToolError("invalid library name: " + name);
  const fs::path dir = librariesDir() / name;
  std::error_code ec;
  const bool present = fs::is_directory(dir, ec);
  if (!known && !present) throw ToolError("library not installed: " + name);
  fs::remove_all(dir, ec);
  if (known) {
    libraries["installed"].erase(name);
    project_.save();
  }
  return json{{"name", name}, {"removed", true}};
}

json LibraryManager::files(const std::string& name, const std::string& filter, bool describe) const
{
  if (!validDirName(name)) throw ToolError("invalid library name: " + name);
  const fs::path dir = librariesDir() / name;
  std::error_code ec;
  if (!fs::is_directory(dir, ec)) throw ToolError("library not installed: " + name);

  const std::string needle = lower(filter);
  std::vector<fs::path> scadFiles;
  json files = json::array();
  for (auto it = fs::recursive_directory_iterator(dir, ec); it != fs::recursive_directory_iterator();
       it.increment(ec)) {
    if (!it->is_regular_file(ec)) continue;
    const std::string relative = it->path().lexically_relative(dir).generic_string();
    if (!needle.empty() && lower(relative).find(needle) == std::string::npos) continue;
    files.push_back(json{{"path", relative}, {"size", it->file_size(ec)}});
    if (lower(it->path().extension().string()) == ".scad") scadFiles.push_back(it->path());
  }
  std::sort(files.begin(), files.end(),
            [](const json& a, const json& b) { return a["path"] < b["path"]; });
  std::sort(scadFiles.begin(), scadFiles.end());

  json result{{"name", name}, {"dir", project_.relative(dir)}, {"files", files}};
  if (!describe) return result;

  json described = json::array();
  size_t count = 0;
  for (const auto& file : scadFiles) {
    if (count++ >= kMaxDescribedFiles) {
      result["describeTruncated"] = true;
      break;
    }
    const EngineResult parsed = CompileEngine::run(file, CompileMode::Check, 30);
    json entry{{"path", file.lexically_relative(dir).generic_string()}, {"parsed", parsed.parsed}};
    if (parsed.sourceFile && parsed.sourceFile->scope) {
      json modules = json::array();
      for (const auto& [moduleName, module] : parsed.sourceFile->scope->getUserModules()) {
        modules.push_back(
          json{{"name", moduleName}, {"parameters", parameterNames(module->parameters)}});
      }
      json functions = json::array();
      for (const auto& [functionName, function] : parsed.sourceFile->scope->getUserFunctions()) {
        functions.push_back(
          json{{"name", functionName}, {"parameters", parameterNames(function->parameters)}});
      }
      std::sort(modules.begin(), modules.end(),
                [](const json& a, const json& b) { return a["name"] < b["name"]; });
      std::sort(functions.begin(), functions.end(),
                [](const json& a, const json& b) { return a["name"] < b["name"]; });
      entry["modules"] = modules;
      entry["functions"] = functions;
    }
    described.push_back(entry);
  }
  result["described"] = described;
  return result;
}

}  // namespace mcp::library
