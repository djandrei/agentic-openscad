#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "json/json.hpp"
#include "mcp/session/ProjectSession.h"

class EditorInterface;
class MainWindow;

namespace mcp {

class AgentBridge;
class AgentDock;

// Project session backed by one MainWindow. Every file operation is reflected
// in the editor tabs so the user sees the agent work; previews and renders run
// through the window so the 3D view shows them; the Agent dock shows the tool
// calls and the changes.
class GuiSession : public ProjectSession
{
public:
  GuiSession(std::shared_ptr<Project> project, MainWindow *window, AgentBridge *bridge);

  MainWindow *window() const { return window_; }
  AgentDock *dock() const { return dock_; }

  nlohmann::json status() const override;
  bool paused() const override;
  bool busy() const override;
  std::optional<EditorState> editorState(const std::string& relativePath) const override;
  void fileWritten(const FileChange& change) override;
  void fileRemoved(const std::string& relativePath) override;
  void fileRenamed(const std::string& from, const std::string& to) override;
  CompileOutcome compile(const CompileRequest& request) override;

private:
  static constexpr int kAgentChangeMarker = 12;

  EditorInterface *findEditor(const std::filesystem::path& absolute) const;
  void applyToEditor(EditorInterface *editor, const FileChange& change, bool follow);
  void closeEditorFor(const std::filesystem::path& absolute);
  // Runs the GUI event loop until the window's compile lock is released or
  // the timeout passes; returns false on timeout.
  bool waitForCompileEnd(double timeoutSeconds);

  MainWindow *window_;
  AgentBridge *bridge_;
  AgentDock *dock_ = nullptr;
};

}  // namespace mcp
