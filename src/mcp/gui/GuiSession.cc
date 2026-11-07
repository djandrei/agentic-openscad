#include "mcp/gui/GuiSession.h"

#include <QColor>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QString>
#include <QTimer>

#include <Qsci/qsciscintilla.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

#include "gui/Editor.h"
#include "gui/MainWindow.h"
#include "gui/ScintillaEditor.h"
#include "gui/TabManager.h"
#include "mcp/engine/CompileEngine.h"
#include "mcp/gui/AgentBridge.h"
#include "mcp/gui/AgentDock.h"
#include "mcp/protocol/Registry.h"

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
                        {"busy", busy()},
                        {"paused", paused()},
                        {"windowTitle", window_ ? window_->windowTitle().toStdString() : ""}};
}

bool GuiSession::paused() const
{
  return bridge_->paused();
}

bool GuiSession::busy() const
{
  return GuiLocker::isLocked();
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

bool GuiSession::waitForCompileEnd(double timeoutSeconds)
{
  QElapsedTimer elapsed;
  elapsed.start();
  QEventLoop loop;
  QTimer poll;
  bool timedOut = false;
  poll.setInterval(25);
  QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
    if (!GuiLocker::isLocked()) {
      loop.quit();
    } else if (elapsed.elapsed() > static_cast<qint64>(timeoutSeconds * 1000)) {
      timedOut = true;
      loop.quit();
    }
  });
  poll.start();
  loop.exec();
  return !timedOut;
}

CompileOutcome GuiSession::compile(const CompileRequest& request)
{
  fs::path file;
  try {
    file = project().resolve(request.file);
  } catch (const std::invalid_argument& e) {
    throw ToolError(e.what());
  }
  std::error_code ec;
  if (!fs::is_regular_file(file, ec)) throw ToolError("file not found: " + request.file);

  CompileOutcome outcome;
  const auto started = std::chrono::steady_clock::now();
  const auto firstSeq = MessageLog::instance().lastSeq();

  if (request.mode == CompileMode::Check) {
    const EngineResult result = CompileEngine::run(file, CompileMode::Check, request.timeoutSeconds);
    outcome.parsed = result.parsed;
    outcome.millis =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    finishOutcome(outcome, firstSeq, result.parsed);
    return outcome;
  }

  // A preview or render may already be running (the user's, or the auto
  // preview scheduled by a write to the main file); wait for it first.
  if (GuiLocker::isLocked() && !waitForCompileEnd(request.timeoutSeconds)) {
    throw ToolError("busy: a preview or render is already running in the GUI; wait and retry");
  }

  // The window compiles the active editor, so make the requested file active.
  window_->tabManager->open(QString::fromStdString(file.string()));
  EditorInterface *editor = window_->tabManager->editor;
  outcome.unsavedEditor = editor && editor->isContentModified();

  if (request.mode == CompileMode::Preview) {
    window_->actionRenderPreview();  // synchronous: returns once the preview is drawn
    if (GuiLocker::isLocked()) outcome.timedOut = !waitForCompileEnd(request.timeoutSeconds);
  } else {
    window_->designActionRender->trigger();  // F6: geometry runs on a worker thread
    outcome.timedOut = !waitForCompileEnd(request.timeoutSeconds);
  }

  outcome.parsed = window_->rootFile != nullptr;
  const bool evaluated = window_->rootNode != nullptr && !outcome.timedOut;
  if (request.mode == CompileMode::Render && !outcome.timedOut) {
    outcome.geometry = CompileEngine::geometrySummary(window_->rootGeom.get());
  }
  outcome.millis =
    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
  finishOutcome(outcome, firstSeq, evaluated);
  return outcome;
}

}  // namespace mcp
