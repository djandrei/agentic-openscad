#pragma once

#include <QString>
#include <QUrl>

#include <filesystem>

#include "gui/Dock.h"

class MainWindow;
class QCheckBox;
class QFileSystemModel;
class QLabel;
class QPlainTextEdit;
class QTextBrowser;
class QToolButton;
class QTreeView;

namespace mcp {

class AgentBridge;
struct FileChange;

// Dock showing what the agent does to a project: pause and follow controls,
// the project file tree, the tool-call log and a stream of every change the
// agent wrote (one hunk per write; clicking a hunk jumps to the line).
class AgentDock : public Dock
{
  Q_OBJECT

public:
  AgentDock(AgentBridge *bridge, MainWindow *window, const std::filesystem::path& projectRoot);

  void appendChange(const FileChange& change);
  void appendToolCall(const QString& name, const QString& argsSummary, bool ok, double millis);

private:
  void refreshState();
  void openLocation(const QUrl& url);

  AgentBridge *bridge_;
  MainWindow *window_;
  std::filesystem::path root_;
  QToolButton *pauseButton_;
  QCheckBox *followBox_;
  QLabel *stateLabel_;
  QFileSystemModel *model_;
  QTreeView *tree_;
  QPlainTextEdit *activity_;
  QTextBrowser *changes_;
};

}  // namespace mcp
