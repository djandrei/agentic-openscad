#include "mcp/gui/GuiSession.h"

#include <QColor>
#include <QFileInfo>
#include <QString>
#include <QTimer>

#include <Qsci/qsciscintilla.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>

#include "gui/Editor.h"
#include "gui/MainWindow.h"
#include "gui/ScintillaEditor.h"
#include "gui/TabManager.h"
#include "mcp/gui/AgentBridge.h"
#include "mcp/gui/AgentDock.h"

namespace fs = std::filesystem;

namespace mcp {

namespace {

fs::path canonicalOrSelf(const QString& path)
{
  const QString canonical = QFileInfo(path).canonicalFilePath();
  return fs::path((canonical.isEmpty() ? path : canonical).toStdString());
}

}  // namespace

GuiSession::GuiSession(std::shared_ptr<Project> project, MainWindow *window, AgentBridge *bridge)
  : ProjectSession(std::move(project)), window_(window), bridge_(bridge)
{
  dock_ = new AgentDock(bridge_, window_, this->project().root());
  window_->addDockWidget(Qt::RightDockWidgetArea, dock_);
  window_->tabifyDockWidget(window_->consoleDock, dock_);
  dock_->show();
  dock_->raise();
}

nlohmann::json GuiSession::status() const
{
  return nlohmann::json{{"mode", "gui"},
                        {"busy", false},
                        {"paused", paused()},
                        {"windowTitle", window_ ? window_->windowTitle().toStdString() : ""}};
}

bool GuiSession::paused() const
{
  return bridge_->paused();
}

EditorInterface *GuiSession::findEditor(const fs::path& absolute) const
{
  for (auto *editor : window_->tabManager->editorList) {
    if (editor->filepath.isEmpty()) continue;
    if (canonicalOrSelf(editor->filepath) == absolute) return editor;
  }
  return nullptr;
}

std::optional<EditorState> GuiSession::editorState(const std::string& relativePath) const
{
  fs::path absolute;
  try {
    absolute = project().resolve(relativePath);
  } catch (const std::invalid_argument&) {
    return std::nullopt;
  }
  auto *editor = findEditor(absolute);
  if (!editor) return std::nullopt;
  const std::string text = editor->toPlainText().toStdString();
  return EditorState{text, FileStore::versionOf(text), editor->isContentModified()};
}

void GuiSession::applyToEditor(EditorInterface *editor, const FileChange& change, bool follow)
{
  const QString text = QString::fromStdString(change.content);
  auto *scintilla = dynamic_cast<ScintillaEditor *>(editor);
  int cursorLine = 0;
  int cursorIndex = 0;
  int firstVisible = 0;
  if (scintilla) {
    scintilla->qsci->getCursorPosition(&cursorLine, &cursorIndex);
    firstVisible = scintilla->qsci->firstVisibleLine();
  }

  // setText() replaces the selection, so the user's undo history survives.
  if (editor->toPlainText() != text) editor->setText(text);
  editor->setContentModified(false);

  if (!scintilla) return;
  auto *qsci = scintilla->qsci;
  qsci->markerDefine(QsciScintilla::Background, kAgentChangeMarker);
  qsci->setMarkerBackgroundColor(QColor(255, 236, 170), kAgentChangeMarker);
  qsci->markerDeleteAll(kAgentChangeMarker);
  if (change.hunk) {
    const int first = static_cast<int>(change.hunk->startLine) - 1;
    const int count = std::max<int>(1, static_cast<int>(change.hunk->added.size()));
    for (int line = first; line < first + count && line < qsci->lines(); ++line) {
      qsci->markerAdd(line, kAgentChangeMarker);
    }
  }
  if (follow && change.hunk) {
    scintilla->setCursorPosition(static_cast<int>(change.hunk->startLine) - 1, 0);
  } else {
    qsci->setCursorPosition(std::min(cursorLine, std::max(qsci->lines() - 1, 0)), cursorIndex);
    qsci->setFirstVisibleLine(firstVisible);
  }
}

void GuiSession::fileWritten(const FileChange& change)
{
  const QString absolute = QString::fromStdString(change.absolute.string());
  const bool follow = bridge_->followAgent();
  EditorInterface *editor = findEditor(change.absolute);
  if (!editor) {
    // Opens the file in a new tab (or in an untitled empty tab) and makes it current.
    window_->tabManager->open(absolute);
    editor = findEditor(change.absolute);
  } else if (follow) {
    window_->tabManager->open(absolute);  // switches to the existing tab
  }
  if (editor) applyToEditor(editor, change, follow);
  if (dock_) dock_->appendChange(change);

  const bool isMain = change.absolute == project().mainPath();
  if (isMain && project().autoPreview() && editor && editor == window_->tabManager->editor) {
    QTimer::singleShot(0, window_, [window = window_] { window->actionRenderPreview(); });
  }
}

void GuiSession::closeEditorFor(const fs::path& absolute)
{
  auto *editor = findEditor(absolute);
  if (!editor) return;
  editor->setContentModified(false);
  window_->tabManager->open(QString::fromStdString(absolute.string()));  // make it current (no reload)
  if (window_->tabManager->editor == editor) window_->tabManager->closeCurrentTab();
}

void GuiSession::fileRemoved(const std::string& relativePath)
{
  closeEditorFor(project().root() / relativePath);
}

void GuiSession::fileRenamed(const std::string& from, const std::string& to)
{
  const fs::path oldPath = project().root() / from;
  const bool wasOpen = findEditor(oldPath) != nullptr;
  closeEditorFor(oldPath);
  if (wasOpen) {
    std::error_code ec;
    const fs::path newPath = fs::weakly_canonical(project().root() / to, ec);
    window_->tabManager->open(QString::fromStdString(newPath.string()));
  }
}

}  // namespace mcp
