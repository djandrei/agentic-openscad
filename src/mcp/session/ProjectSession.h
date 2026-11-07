#pragma once

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "json/json.hpp"
#include "mcp/session/FileStore.h"
#include "mcp/session/Project.h"

namespace mcp {

// Text currently shown in an editor for a file, which may differ from disk.
struct EditorState {
  std::string text;
  std::string version;  // FileStore::versionOf(text)
  bool unsaved = false;
};

// One open project. Tool handlers are written against this interface; the
// headless and the GUI implementations provide the behaviour. Later stages
// extend it with compilation, view and parameter operations.
class ProjectSession
{
public:
  explicit ProjectSession(std::shared_ptr<Project> project)
    : project_(std::move(project)), files_(*project_)
  {
  }
  virtual ~ProjectSession() = default;

  Project& project() { return *project_; }
  const Project& project() const { return *project_; }
  FileStore& files() { return files_; }

  // Mode-specific status fields merged into the `status` tool result.
  virtual nlohmann::json status() const = 0;

  // True while the user has paused the agent (GUI only).
  virtual bool paused() const { return false; }

  // Editor contents for a file when it is open in an editor; nullopt when not
  // open or in headless mode.
  virtual std::optional<EditorState> editorState(const std::string& /*relativePath*/) const
  {
    return std::nullopt;
  }

  // Hooks invoked after the file store changed the disk.
  virtual void fileWritten(const FileChange& /*change*/) {}
  virtual void fileRemoved(const std::string& /*relativePath*/) {}
  virtual void fileRenamed(const std::string& /*from*/, const std::string& /*to*/) {}

private:
  std::shared_ptr<Project> project_;
  FileStore files_;
};

}  // namespace mcp
