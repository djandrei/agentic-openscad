#pragma once

#include <memory>

#include "json/json.hpp"
#include "mcp/session/ProjectSession.h"

namespace mcp {

// Project session without a GUI. Compiles and renders through the same code
// path as the command line (added in later stages).
class HeadlessSession : public ProjectSession
{
public:
  explicit HeadlessSession(std::shared_ptr<Project> project) : ProjectSession(std::move(project)) {}

  nlohmann::json status() const override;
};

}  // namespace mcp
