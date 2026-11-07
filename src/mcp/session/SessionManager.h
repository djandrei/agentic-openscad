#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "json/json.hpp"
#include "mcp/session/Project.h"
#include "mcp/session/ProjectSession.h"

namespace mcp {

// Owns the open project sessions. One instance per process; the factory
// decides whether a session is headless or GUI-backed.
class SessionManager
{
public:
  using Factory = std::function<std::unique_ptr<ProjectSession>(std::shared_ptr<Project>)>;

  SessionManager(std::string mode, Factory factory);

  const std::string& mode() const { return mode_; }

  // Opens (or returns the already open) session for `dir`, creating the
  // project config when missing. Throws ToolError on failure.
  ProjectSession& open(const std::filesystem::path& dir);
  void close(const std::string& id);

  std::vector<ProjectSession *> sessions() const;
  ProjectSession *find(const std::string& idOrPath) const;
  ProjectSession *current() const { return current_; }
  // Called whenever the current session changes (open, resolve by argument,
  // close); the viewer follows the project of the last tool call.
  using CurrentChanged = std::function<void(ProjectSession *)>;
  void setCurrentChanged(CurrentChanged callback) { currentChanged_ = std::move(callback); }

  // Picks the session a tool call refers to: args["project"] when given,
  // otherwise the current one. Throws ToolError when nothing applies.
  ProjectSession& resolve(const nlohmann::json& args);

private:
  std::string mode_;
  Factory factory_;
  void setCurrent(ProjectSession *session);

  std::vector<std::unique_ptr<ProjectSession>> sessions_;
  ProjectSession *current_ = nullptr;
  CurrentChanged currentChanged_;
};

}  // namespace mcp
