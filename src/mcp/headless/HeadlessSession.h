#pragma once

#include <memory>

#include "json/json.hpp"
#include "mcp/session/ProjectSession.h"

class Geometry;

namespace mcp {

// Project session without a GUI. Compiles and renders through the same code
// path as the command line.
class HeadlessSession : public ProjectSession
{
public:
  explicit HeadlessSession(std::shared_ptr<Project> project) : ProjectSession(std::move(project)) {}

  nlohmann::json status() const override;
  CompileOutcome compile(const CompileRequest& request) override;

  // Geometry of the last successful render, for geometry queries and exports.
  std::shared_ptr<const Geometry> lastGeometry() const { return lastGeometry_; }

private:
  std::shared_ptr<const Geometry> lastGeometry_;
};

}  // namespace mcp
