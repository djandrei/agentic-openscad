#include "mcp/gui/GuiSession.h"

#include <memory>

#include "gui/MainWindow.h"

namespace mcp {

GuiSession::GuiSession(std::shared_ptr<Project> project, MainWindow *window)
  : ProjectSession(std::move(project)), window_(window)
{
}

nlohmann::json GuiSession::status() const
{
  return nlohmann::json{{"mode", "gui"},
                        {"busy", false},
                        {"paused", false},
                        {"windowTitle", window_ ? window_->windowTitle().toStdString() : ""}};
}

}  // namespace mcp
