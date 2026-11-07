#pragma once

#include <memory>
#include <utility>

#include "json/json.hpp"
#include "mcp/session/Project.h"

namespace mcp {

// One open project. Tool handlers are written against this interface; the
// headless and the GUI implementations provide the behaviour. Later stages
// extend it with files, compilation, view and parameter operations.
class ProjectSession
{
public:
  explicit ProjectSession(std::shared_ptr<Project> project) : project_(std::move(project)) {}
  virtual ~ProjectSession() = default;

  Project& project() { return *project_; }
  const Project& project() const { return *project_; }

  // Mode-specific status fields merged into the `status` tool result.
  virtual nlohmann::json status() const = 0;

private:
  std::shared_ptr<Project> project_;
};

}  // namespace mcp
