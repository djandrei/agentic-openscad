#include "mcp/gui/GuiSession.h"

#include <QBuffer>
#include <QByteArray>
#include <QColor>
#include <QElapsedTimer>
#include <QImage>
#include <QMetaObject>
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
#include "glview/ColorMap.h"
#include "glview/RenderSettings.h"
#include "gui/QGLView.h"
#include "gui/ViewportControl.h"
#include "mcp/engine/Base64.h"
#include "mcp/engine/CompileEngine.h"
#include "mcp/engine/ViewEngine.h"
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

std::string GuiSession::currentMode() const
{
  if (window_->viewActionPreview->isChecked()) return "preview";
  if (window_->viewActionThrownTogether->isChecked()) return "thrownTogether";
  return "render";
}

ViewOptions GuiSession::currentViewOptions() const
{
  ViewOptions options;
  auto *gl = window_->qglview;
  options.flags["edges"] = gl->showEdges();
  options.flags["axes"] = gl->showAxes();
  options.flags["crosshairs"] = gl->showCrosshairs();
  options.flags["scales"] = gl->showScaleProportional();
  options.previewer =
    window_->viewActionThrownTogether->isChecked() ? Previewer::THROWNTOGETHER : Previewer::OPENCSG;
  return options;
}

void GuiSession::applyMode(const std::string& mode)
{
  if (mode == "preview") {
#ifdef ENABLE_OPENCSG
    window_->viewModePreview();
#else
    window_->viewModeThrownTogether();
#endif
  } else if (mode == "thrownTogether") {
    window_->viewModeThrownTogether();
  } else if (mode == "render") {
    if (!window_->rootGeom)
      throw ToolError("no rendered geometry: call render before switching to render mode");
    window_->viewModeRender();
  } else {
    throw ToolError("mode must be preview, render or thrownTogether");
  }
}

nlohmann::json GuiSession::cameraGet() const
{
  return view::cameraToJson(window_->qglview->cam);
}

nlohmann::json GuiSession::cameraSet(const nlohmann::json& spec)
{
  auto *gl = window_->qglview;
  const bool fit = view::applyCameraSpec(gl->cam, spec);
  if (fit) gl->viewAll();
  gl->update();
  window_->viewportControlWidget->cameraChanged();
  return view::cameraToJson(gl->cam);
}

nlohmann::json GuiSession::viewOptionsGet() const
{
  return view::viewOptionsToJson(currentViewOptions(), RenderSettings::inst()->colorscheme,
                                 currentMode());
}

nlohmann::json GuiSession::viewOptionsSet(const nlohmann::json& spec)
{
  if (spec.contains("edges")) window_->viewActionShowEdges->setChecked(spec["edges"].get<bool>());
  if (spec.contains("axes")) window_->viewActionShowAxes->setChecked(spec["axes"].get<bool>());
  if (spec.contains("crosshairs")) {
    window_->viewActionShowCrosshairs->setChecked(spec["crosshairs"].get<bool>());
  }
  if (spec.contains("scales")) {
    window_->viewActionShowScaleProportional->setChecked(spec["scales"].get<bool>());
  }
  if (spec.contains("colorScheme")) {
    const std::string name = spec["colorScheme"].get<std::string>();
    if (!ColorMap::instance().findColorScheme(name)) throw ToolError("unknown color scheme: " + name);
    QMetaObject::invokeMethod(window_, "setColorScheme", Qt::DirectConnection,
                              Q_ARG(QString, QString::fromStdString(name)));
  }
  if (spec.contains("mode")) applyMode(spec["mode"].get<std::string>());
  window_->qglview->update();
  return viewOptionsGet();
}

namespace {

// An unexposed QOpenGLWidget yields an all-black frame.
bool looksBlank(const QImage& image)
{
  if (image.isNull()) return true;
  for (int y = 0; y < image.height(); y += std::max(1, image.height() / 12)) {
    for (int x = 0; x < image.width(); x += std::max(1, image.width() / 12)) {
      const QRgb pixel = image.pixel(x, y);
      if (qRed(pixel) || qGreen(pixel) || qBlue(pixel)) return false;
    }
  }
  return true;
}

}  // namespace

std::vector<view::CaptureImage> GuiSession::capture(const CaptureRequest& request)
{
  auto *gl = window_->qglview;
  if (!request.mode.empty()) applyMode(request.mode);

  const Camera savedCamera = gl->cam;
  const bool savedEdges = gl->showEdges();
  const bool savedAxes = gl->showAxes();
  const bool savedCrosshairs = gl->showCrosshairs();
  const bool savedScales = gl->showScaleProportional();
  const std::string savedScheme = RenderSettings::inst()->colorscheme;

  ViewOptions options = currentViewOptions();
  std::string scheme = savedScheme;
  view::applyViewOptionSpec(options, scheme, request.options);
  gl->setShowEdges(options.flags["edges"]);
  gl->setShowAxes(options.flags["axes"]);
  gl->setShowCrosshairs(options.flags["crosshairs"]);
  gl->setShowScaleProportional(options.flags["scales"]);
  if (scheme != savedScheme) gl->setColorScheme(scheme);

  std::vector<view::CaptureImage> images;
  Camera lastCamera = savedCamera;
  try {
    for (const auto& spec : request.views) {
      gl->cam = savedCamera;
      std::string name = "current";
      bool fit = false;
      if (spec.is_string()) {
        name = spec.get<std::string>();
        if (name != "current") fit = view::applyCameraSpec(gl->cam, nlohmann::json{{"preset", name}});
      } else if (spec.is_object()) {
        name = spec.value("name", std::string("custom"));
        fit = view::applyCameraSpec(gl->cam, spec);
      }
      if (fit) gl->viewAll();
      gl->update();
      const QImage frame = gl->grabFrame();
      if (looksBlank(frame)) {
        if (!window_->rootGeom) {
          throw ToolError(
            "capture failed: the 3D view is not exposed and nothing has been rendered; "
            "call render first or make the OpenSCAD window visible");
        }
        images.push_back(view::captureGeometry(window_->rootGeom, gl->cam, options, scheme,
                                               request.width, request.height, name));
      } else {
        const QImage scaled =
          frame.scaled(static_cast<int>(request.width), static_cast<int>(request.height),
                       Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        scaled.save(&buffer, "PNG");
        view::CaptureImage image;
        image.name = name;
        image.pngBase64 =
          base64Encode(std::string(bytes.constData(), static_cast<size_t>(bytes.size())));
        image.width = static_cast<unsigned>(scaled.width());
        image.height = static_cast<unsigned>(scaled.height());
        image.camera = view::cameraToJson(gl->cam);
        images.push_back(std::move(image));
      }
      lastCamera = gl->cam;
    }
  } catch (...) {
    gl->setShowEdges(savedEdges);
    gl->setShowAxes(savedAxes);
    gl->setShowCrosshairs(savedCrosshairs);
    gl->setShowScaleProportional(savedScales);
    if (scheme != savedScheme) gl->setColorScheme(savedScheme);
    gl->cam = savedCamera;
    gl->update();
    throw;
  }

  gl->setShowEdges(savedEdges);
  gl->setShowAxes(savedAxes);
  gl->setShowCrosshairs(savedCrosshairs);
  gl->setShowScaleProportional(savedScales);
  if (scheme != savedScheme) gl->setColorScheme(savedScheme);
  gl->cam = request.keep ? lastCamera : savedCamera;
  gl->update();
  window_->viewportControlWidget->cameraChanged();
  return images;
}

nlohmann::json GuiSession::geometryInfo()
{
  if (!window_->rootGeom) throw ToolError("no rendered geometry: call render first");
  return view::geometryInfo(window_->rootGeom);
}

nlohmann::json GuiSession::exportGeometry(const std::string& format, const std::string& path,
                                          const nlohmann::json& options)
{
  if (!window_->rootGeom) throw ToolError("no rendered geometry: call render first");
  fs::path target;
  try {
    target = project().resolve(path);
  } catch (const std::invalid_argument& e) {
    throw ToolError(e.what());
  }
  const std::string source = window_->activeEditor && !window_->activeEditor->filepath.isEmpty()
                               ? window_->activeEditor->filepath.toStdString()
                               : project().mainPath().string();
  nlohmann::json result =
    view::exportGeometry(window_->rootGeom, format, target, options, source, window_->qglview->cam,
                         currentViewOptions(), RenderSettings::inst()->colorscheme);
  result["path"] = project().relative(target);
  return result;
}

}  // namespace mcp
