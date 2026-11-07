#pragma once

#include <memory>

#include "json/json.hpp"
#include "mcp/session/ProjectSession.h"

class MainWindow;

namespace mcp {

// Project session backed by one MainWindow. Tool operations are performed
// through the window so the user sees them happen.
class GuiSession : public ProjectSession
{
public:
  GuiSession(std::shared_ptr<Project> project, MainWindow *window);

  MainWindow *window() const { return window_; }

  nlohmann::json status() const override;

private:
  MainWindow *window_;
};

}  // namespace mcp
