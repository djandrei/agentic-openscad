#include "mcp/session/Project.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

namespace mcp {

namespace {

bool isInside(const fs::path& root, const fs::path& candidate)
{
  const fs::path rel = candidate.lexically_relative(root);
  if (rel.empty()) return false;
  return *rel.begin() != "..";
}

void mergeDefaults(nlohmann::json& target, const nlohmann::json& defaults)
{
  for (const auto& [key, value] : defaults.items()) {
    if (!target.contains(key)) {
      target[key] = value;
    } else if (value.is_object() && target[key].is_object()) {
      mergeDefaults(target[key], value);
    }
  }
}

}  // namespace

Project::Project(const fs::path& root) : config_(defaultConfig())
{
  std::error_code ec;
  if (!fs::is_directory(root, ec)) {
    throw std::invalid_argument("project folder does not exist: " + root.string());
  }
  root_ = fs::canonical(root);
}

nlohmann::json Project::defaultConfig()
{
  return nlohmann::json{{"main", kDefaultMainFile},
                        {"autoPreview", true},
                        {"libraries", {{"dir", "libraries"}, {"installed", nlohmann::json::object()}}},
                        {"registries", nlohmann::json::array()}};
}

void Project::loadOrCreate()
{
  const fs::path path = configPath();
  if (fs::exists(path)) {
    std::ifstream in(path);
    nlohmann::json parsed = nlohmann::json::parse(in, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
      throw std::runtime_error("invalid project config: " + path.string());
    }
    config_ = std::move(parsed);
    mergeDefaults(config_, defaultConfig());
  } else {
    config_ = defaultConfig();
    save();
  }
}

void Project::save() const
{
  std::ofstream out(configPath());
  if (!out) throw std::runtime_error("cannot write project config: " + configPath().string());
  out << config_.dump(2) << '\n';
}

std::string Project::mainFile() const
{
  const auto& main = config_.value("main", nlohmann::json(kDefaultMainFile));
  return main.is_string() && !main.get<std::string>().empty() ? main.get<std::string>()
                                                              : kDefaultMainFile;
}

bool Project::autoPreview() const
{
  return config_.value("autoPreview", true);
}

fs::path Project::resolve(const std::string& path) const
{
  if (path.empty()) throw std::invalid_argument("empty path");
  const fs::path given(path);
  const fs::path full = given.is_absolute() ? given : root_ / given;
  std::error_code ec;
  fs::path canonical = fs::weakly_canonical(full, ec);
  if (ec) throw std::invalid_argument("cannot resolve path: " + path);
  if (!isInside(root_, canonical)) {
    throw std::invalid_argument("path is outside the project folder: " + path);
  }
  return canonical;
}

std::string Project::relative(const fs::path& absolute) const
{
  return absolute.lexically_relative(root_).generic_string();
}

nlohmann::json Project::infoJson() const
{
  std::error_code ec;
  return nlohmann::json{{"id", id()},
                        {"name", name()},
                        {"root", root_.string()},
                        {"main", mainFile()},
                        {"mainExists", fs::is_regular_file(mainPath(), ec)},
                        {"config", config_}};
}

}  // namespace mcp
