#pragma once

#include <filesystem>
#include <string>

#include "json/json.hpp"

namespace mcp {

// A project is a folder on disk with a `.openscad-project.json` config file.
// The folder is the sandbox: every path the agent names is resolved against it
// and must stay inside it.
class Project
{
public:
  static constexpr const char *kConfigFileName = ".openscad-project.json";
  static constexpr const char *kDefaultMainFile = "main.scad";

  // `root` must be an existing directory; it is stored in canonical form.
  explicit Project(const std::filesystem::path& root);

  const std::filesystem::path& root() const { return root_; }
  std::string id() const { return root_.string(); }
  std::string name() const { return root_.filename().string(); }
  std::filesystem::path configPath() const { return root_ / kConfigFileName; }

  // Reads the config file, creating it with defaults when missing. Missing
  // keys are filled with defaults; unknown keys are preserved.
  void loadOrCreate();
  void save() const;

  const nlohmann::json& config() const { return config_; }
  nlohmann::json& config() { return config_; }

  std::string mainFile() const;  // relative to root
  std::filesystem::path mainPath() const { return root_ / mainFile(); }
  bool autoPreview() const;

  // Resolves an agent-supplied path (relative to the root, or absolute) and
  // checks that it stays inside the project, following symlinks. Throws
  // std::invalid_argument otherwise. The result need not exist.
  std::filesystem::path resolve(const std::string& path) const;
  // Relative form of an absolute path inside the project, with '/' separators.
  std::string relative(const std::filesystem::path& absolute) const;

  nlohmann::json infoJson() const;

  static nlohmann::json defaultConfig();

private:
  std::filesystem::path root_;
  nlohmann::json config_;
};

}  // namespace mcp
