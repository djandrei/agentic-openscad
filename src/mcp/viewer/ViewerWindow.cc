#include "mcp/viewer/ViewerWindow.h"

#include <QColor>
#include <QFont>
#include <QFontDatabase>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QStatusBar>
#include <QTableWidget>
#include <QTextBrowser>
#include <QTime>
#include <QToolButton>
#include <QVBoxLayout>

#include <Qsci/qsciscintilla.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "core/MouseConfig.h"
#include "core/Tree.h"
#include "geometry/Geometry.h"
#include "geometry/PolySet.h"
#include "geometry/Polygon2d.h"
#include "glview/CsgInfo.h"
#include "glview/PolySetRenderer.h"
#include "glview/RenderSettings.h"
#include "glview/Renderer.h"
#include "glview/preview/ThrownTogetherRenderer.h"
#include "gui/QGLView.h"
#include "gui/ScadLexer.h"
#include "mcp/session/EngineSession.h"
#include "mcp/session/FileStore.h"
#ifdef ENABLE_OPENCSG
#include "glview/preview/OpenCSGRenderer.h"
#endif
#ifdef ENABLE_CGAL
#include "glview/cgal/CGALRenderer.h"
#endif

namespace mcp {

namespace {

constexpr int kMaxHunkLinesShown = 40;

QString escaped(const std::string& text)
{
  return QString::fromStdString(text).toHtmlEscaped();
}

QString severityColor(const std::string& severity)
{
  if (severity == "error") return "#b00020";
  if (severity == "warning" || severity == "deprecated") return "#9a6700";
  if (severity == "echo") return "#1b5e9f";
  return "#555555";
}

}  // namespace

ViewerWindow::ViewerWindow()
{
  setWindowTitle(tr("Agentic OpenSCAD"));
  resize(1400, 900);
  buildUi();
}

void ViewerWindow::buildUi()
{
  const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);

  // Left: file list over the read-only code pane.
  files_ = new QListWidget(this);
  files_->setToolTip(
    tr("Project files; the agent's last touched file is selected. Click to look at another one."));
  connect(files_, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
    if (item) showFile(item->text().toStdString(), nullptr);
  });
  connect(files_, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
    if (item) showFile(item->text().toStdString(), nullptr);
  });

  code_ = new QsciScintilla(this);
  code_->setReadOnly(true);
  code_->setFont(mono);
  code_->setMarginsFont(mono);
  code_->setMarginType(0, QsciScintilla::NumberMargin);
  code_->setMarginWidth(0, "00000");
  code_->setWrapMode(QsciScintilla::WrapNone);
  code_->setCaretLineVisible(false);
  code_->markerDefine(QsciScintilla::Background, kChangeMarker);
  code_->setMarkerBackgroundColor(QColor(255, 236, 170), kChangeMarker);
  // Upstream's lexertl-based lexer; it must be finalized before Scintilla
  // asks it to style text.
  auto *lexer = new ScadLexer2(code_);
  lexer->finalizeLexer();
  code_->setLexer(lexer);
  lexer->setFont(mono);
  // Fixed light paper with dark text (like upstream's default editor scheme),
  // independent of the desktop theme: the syntax colors below assume it.
  const QColor paper(0xff, 0xff, 0xff);
  const QColor text(0x20, 0x20, 0x20);
  lexer->setDefaultPaper(paper);
  lexer->setDefaultColor(text);
  lexer->setPaper(paper, -1);
  lexer->setColor(text, -1);
  code_->setMarginsBackgroundColor(QColor(0xf0, 0xf0, 0xf0));
  code_->setMarginsForegroundColor(QColor(0x80, 0x80, 0x80));
  lexer->setColor(QColor(0x1b, 0x5e, 0x9f), ScadLexer2::Keyword);
  lexer->setColor(QColor(0x7b, 0x2e, 0x9f), ScadLexer2::Transformation);
  lexer->setColor(QColor(0x7b, 0x2e, 0x9f), ScadLexer2::Boolean);
  lexer->setColor(QColor(0x0f, 0x6e, 0x6e), ScadLexer2::Function);
  lexer->setColor(QColor(0x0f, 0x6e, 0x6e), ScadLexer2::Model);
  lexer->setColor(QColor(0x9a, 0x4a, 0x00), ScadLexer2::Number);
  lexer->setColor(QColor(0x1b, 0x7f, 0x3b), ScadLexer2::String);
  lexer->setColor(QColor(0x80, 0x80, 0x80), ScadLexer2::Comment);
  lexer->setColor(QColor(0xb0, 0x00, 0x20), ScadLexer2::SpecialVariable);

  auto *left = new QSplitter(Qt::Vertical, this);
  left->addWidget(files_);
  left->addWidget(code_);
  left->setStretchFactor(0, 1);
  left->setStretchFactor(1, 5);

  // Center: the 3D view. QGLView leaves its mouse-action table uninitialized
  // (the classic MainWindow fills it from the preferences); without this a
  // drag multiplies the mouse delta by garbage and QGLView::normalizeAngle
  // never terminates.
  view_ = new QGLView(this);
  for (const auto& [mouseAction, viewAction] : MouseConfig::presetSettings.at(MouseConfig::OPEN_SCAD)) {
    view_->setMouseActions(mouseAction, MouseConfig::viewActionArrays.at(viewAction));
  }

  // Right: parameters over messages.
  parameters_ = new QTableWidget(0, 3, this);
  parameters_->setHorizontalHeaderLabels({tr("Parameter"), tr("Value"), tr("Range / options")});
  parameters_->horizontalHeader()->setStretchLastSection(true);
  parameters_->verticalHeader()->setVisible(false);
  parameters_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  parameters_->setSelectionMode(QAbstractItemView::NoSelection);
  parameters_->setToolTip(tr("Customizer parameters of the last compiled file (read-only)."));

  messages_ = new QTextBrowser(this);
  messages_->setFont(mono);
  messages_->setOpenLinks(false);
  messages_->setPlaceholderText(tr("Messages of the last compile appear here."));

  auto *right = new QSplitter(Qt::Vertical, this);
  right->addWidget(parameters_);
  right->addWidget(messages_);
  right->setStretchFactor(0, 1);
  right->setStretchFactor(1, 2);

  auto *top = new QSplitter(Qt::Horizontal, this);
  top->addWidget(left);
  top->addWidget(view_);
  top->addWidget(right);
  top->setStretchFactor(0, 3);
  top->setStretchFactor(1, 6);
  top->setStretchFactor(2, 3);
  top->setSizes({360, 720, 320});

  // Bottom: activity log and change stream.
  activity_ = new QPlainTextEdit(this);
  activity_->setReadOnly(true);
  activity_->setFont(mono);
  activity_->setMaximumBlockCount(5000);
  activity_->setPlaceholderText(tr("Tool calls made by the agent appear here."));

  changes_ = new QTextBrowser(this);
  changes_->setFont(mono);
  changes_->setOpenLinks(false);
  changes_->setPlaceholderText(tr("Every change the agent writes appears here."));

  auto *bottom = new QSplitter(Qt::Horizontal, this);
  bottom->addWidget(activity_);
  bottom->addWidget(changes_);

  auto *main = new QSplitter(Qt::Vertical, this);
  main->addWidget(top);
  main->addWidget(bottom);
  main->setStretchFactor(0, 4);
  main->setStretchFactor(1, 1);
  main->setSizes({700, 160});
  setCentralWidget(main);

  pauseButton_ = new QToolButton(this);
  pauseButton_->setCheckable(true);
  pauseButton_->setText(tr("Pause agent"));
  pauseButton_->setToolTip(tr("While paused, every agent tool call fails until you resume."));
  connect(pauseButton_, &QToolButton::toggled, this, [this](bool checked) {
    emit pauseToggled(checked);
    updateStatus();
  });
  statusLabel_ = new QLabel(tr("Waiting for the agent…"), this);
  serverLabel_ = new QLabel(this);
  statusBar()->addWidget(pauseButton_);
  statusBar()->addWidget(statusLabel_, 1);
  statusBar()->addPermanentWidget(serverLabel_);
}

void ViewerWindow::setServerStatus(const QString& text)
{
  serverLabel_->setText(text);
}

void ViewerWindow::setPausedIndicator(bool paused)
{
  if (pauseButton_->isChecked() != paused) pauseButton_->setChecked(paused);
  updateStatus();
}

void ViewerWindow::appendToolCall(const QString& name, const QString& argsSummary, bool ok,
                                  double millis)
{
  activity_->appendPlainText(QStringLiteral("%1  %2 %3  %4 (%5 ms)")
                               .arg(QTime::currentTime().toString("hh:mm:ss"), name, argsSummary,
                                    ok ? QStringLiteral("ok") : QStringLiteral("ERROR"))
                               .arg(millis, 0, 'f', 0));
}

void ViewerWindow::showSession(EngineSession *session)
{
  if (session_ == session) return;
  if (session_) session_->setObserver(nullptr);
  session_ = session;
  if (session_) session_->setObserver(this);
  lastCompile_.clear();
  currentFile_.clear();
  refreshFileList();
  if (session_) {
    const std::string main =
      session_->lastFile().empty() ? session_->project().mainFile() : session_->lastFile();
    if (session_->files().currentVersion(main)) showFile(main, nullptr);
    else code_->setText(QString());
  } else {
    code_->setText(QString());
  }
  refreshMessages(nullptr);
  refreshParameters();
  refreshScene();
}

// ---- 3D view --------------------------------------------------------------

std::shared_ptr<Renderer> ViewerWindow::buildRenderer()
{
  if (!session_) return nullptr;
  const EngineResult& result = session_->lastResult();
  const std::string& viewMode = session_->viewMode();
  const bool wantRender = viewMode == "render" ||
                          (session_->lastMode() == CompileMode::Render && viewMode != "thrownTogether");
  if (wantRender && result.geometry) {
#if defined(ENABLE_CGAL) && !defined(USE_POLYSET_FOR_CGAL)
    if (RenderSettings::inst()->backend3D != RenderBackend3D::ManifoldBackend &&
        !std::dynamic_pointer_cast<const PolySet>(result.geometry) &&
        !std::dynamic_pointer_cast<const Polygon2d>(result.geometry)) {
      return std::make_shared<CGALRenderer>(result.geometry);
    }
#endif
    return std::make_shared<PolySetRenderer>(result.geometry);
  }
  if (!result.rootNode) return nullptr;

  Tree tree(result.rootNode, result.documentPath);
  CsgInfo info;
  info.compile_products(tree);
#ifdef ENABLE_OPENCSG
  if (viewMode != "thrownTogether" && view_->hasOpenCSGSupport()) {
    return std::make_shared<OpenCSGRenderer>(info.root_products, info.highlights_products,
                                             info.background_products);
  }
#endif
  return std::make_shared<ThrownTogetherRenderer>(info.root_products, info.highlights_products,
                                                  info.background_products);
}

void ViewerWindow::refreshScene()
{
  renderer_ = buildRenderer();
  view_->setRenderer(renderer_);
  applyViewOptions();
  applyCamera();
  updateStatus();
}

void ViewerWindow::applyCamera()
{
  if (!session_) return;
  // The agent's camera is the reference: any mouse orbiting is replaced.
  view_->cam = session_->camera();
  if (view_->cam.viewall) view_->viewAll();
  view_->update();
}

void ViewerWindow::applyViewOptions()
{
  if (!session_) return;
  const ViewOptions& options = session_->viewOptions();
  view_->setShowEdges(options.flags.at("edges"));
  view_->setShowAxes(options.flags.at("axes"));
  view_->setShowCrosshairs(options.flags.at("crosshairs"));
  view_->setShowScaleProportional(options.flags.at("scales"));
  view_->setColorScheme(session_->colorScheme());
  view_->update();
}

// ---- files and code -------------------------------------------------------

void ViewerWindow::refreshFileList()
{
  files_->clear();
  if (!session_) return;
  bool truncated = false;
  for (const auto& entry : session_->files().list(".", true, false, &truncated)) {
    files_->addItem(QString::fromStdString(entry.path));
  }
  for (int i = 0; i < files_->count(); ++i) {
    if (files_->item(i)->text().toStdString() == currentFile_) files_->setCurrentRow(i);
  }
}

void ViewerWindow::showFile(const std::string& relativePath, const FileChange *change)
{
  if (!session_) return;
  std::string text;
  if (change) {
    text = change->content;
  } else {
    try {
      text = session_->files().read(relativePath).content;
    } catch (const ToolError&) {
      return;
    }
  }
  currentFile_ = relativePath;
  const int firstVisible = code_->firstVisibleLine();
  code_->setText(QString::fromStdString(text));
  code_->markerDeleteAll(kChangeMarker);
  if (change && change->hunk) {
    const int first = static_cast<int>(change->hunk->startLine) - 1;
    const int count = std::max<int>(1, static_cast<int>(change->hunk->added.size()));
    for (int line = first; line < first + count && line < code_->lines(); ++line) {
      code_->markerAdd(line, kChangeMarker);
    }
    code_->ensureLineVisible(std::max(first - 3, 0));
    code_->ensureLineVisible(std::min(first + count + 3, code_->lines() - 1));
    code_->setCursorPosition(first, 0);
  } else {
    code_->setFirstVisibleLine(firstVisible);
  }
  for (int i = 0; i < files_->count(); ++i) {
    if (files_->item(i)->text().toStdString() == currentFile_) files_->setCurrentRow(i);
  }
}

void ViewerWindow::appendChange(const FileChange& change)
{
  const int startLine = change.hunk ? static_cast<int>(change.hunk->startLine) : 1;
  QString html = QStringLiteral("<div style='margin-top:6px'><b>%1</b> ").arg(escaped(change.path));
  if (change.created) {
    html += tr("(created)");
  } else if (change.hunk) {
    html += tr("line %1: -%2 +%3")
              .arg(startLine)
              .arg(change.hunk->removed.size())
              .arg(change.hunk->added.size());
  } else {
    html += tr("(unchanged)");
  }
  html += "</div>";
  if (change.hunk) {
    html += "<pre style='margin:0'>";
    const auto limit = [](const std::vector<std::string>& lines) {
      return std::min(lines.size(), static_cast<size_t>(kMaxHunkLinesShown));
    };
    for (size_t i = 0; i < limit(change.hunk->removed); ++i) {
      html += "<span style='color:#b00020'>- " + escaped(change.hunk->removed[i]) + "</span>\n";
    }
    if (change.hunk->removed.size() > static_cast<size_t>(kMaxHunkLinesShown)) {
      html += tr("  ... %1 more removed lines\n").arg(change.hunk->removed.size() - kMaxHunkLinesShown);
    }
    for (size_t i = 0; i < limit(change.hunk->added); ++i) {
      html += "<span style='color:#1b7f3b'>+ " + escaped(change.hunk->added[i]) + "</span>\n";
    }
    if (change.hunk->added.size() > static_cast<size_t>(kMaxHunkLinesShown)) {
      html += tr("  ... %1 more added lines\n").arg(change.hunk->added.size() - kMaxHunkLinesShown);
    }
    html += "</pre>";
  }
  changes_->append(html);
}

// ---- messages and parameters ---------------------------------------------

void ViewerWindow::refreshMessages(const CompileOutcome *outcome)
{
  messages_->clear();
  if (!outcome) return;
  QString html;
  for (const auto& message : outcome->messages) {
    if (message.severity == "info" || message.severity == "trace") continue;
    QString where;
    if (!message.file.empty() && message.line > 0) {
      std::string file = message.file;
      if (session_) {
        const auto relative = std::filesystem::path(file).lexically_relative(session_->project().root());
        if (!relative.empty() && *relative.begin() != "..") file = relative.generic_string();
      }
      where =
        QStringLiteral(" <span style='color:#777'>%1:%2</span>").arg(escaped(file)).arg(message.line);
    }
    html +=
      QStringLiteral("<div><span style='color:%1'><b>%2</b></span>%3 %4</div>")
        .arg(severityColor(message.severity), escaped(message.severity), where, escaped(message.text));
  }
  if (html.isEmpty()) html = tr("<div style='color:#1b7f3b'>no errors, no warnings</div>");
  messages_->setHtml(html);
}

void ViewerWindow::refreshParameters()
{
  parameters_->setRowCount(0);
  if (!session_) return;
  const nlohmann::json parameters = session_->parameterSummary();
  for (const auto& parameter : parameters) {
    const int row = parameters_->rowCount();
    parameters_->insertRow(row);
    QString range;
    if (parameter.contains("options")) {
      QStringList names;
      for (const auto& option : parameter["options"])
        names << QString::fromStdString(option.value("name", ""));
      range = names.join(", ");
    } else if (parameter.contains("min") || parameter.contains("max")) {
      range = QStringLiteral("%1 .. %2")
                .arg(parameter.contains("min") ? QString::fromStdString(parameter["min"].dump()) : "")
                .arg(parameter.contains("max") ? QString::fromStdString(parameter["max"].dump()) : "");
    }
    auto *name = new QTableWidgetItem(QString::fromStdString(parameter.value("name", "")));
    name->setToolTip(QString::fromStdString(parameter.value("caption", "")));
    parameters_->setItem(row, 0, name);
    parameters_->setItem(row, 1,
                         new QTableWidgetItem(QString::fromStdString(parameter["value"].dump())));
    parameters_->setItem(row, 2, new QTableWidgetItem(range));
  }
  parameters_->resizeColumnsToContents();
}

// ---- status ---------------------------------------------------------------

void ViewerWindow::updateStatus()
{
  if (!session_) {
    statusLabel_->setText(tr("Waiting for the agent…"));
    setWindowTitle(tr("Agentic OpenSCAD"));
    return;
  }
  const QString project = QString::fromStdString(session_->project().name());
  setWindowTitle(tr("%1 – Agentic OpenSCAD").arg(project));
  QString text = project;
  if (!session_->lastFile().empty()) text += "  ·  " + QString::fromStdString(session_->lastFile());
  if (!lastCompile_.isEmpty()) text += "  ·  " + lastCompile_;
  if (pauseButton_->isChecked()) text += "  ·  " + tr("PAUSED");
  statusLabel_->setText(text);
}

// ---- SessionObserver ------------------------------------------------------

void ViewerWindow::fileWritten(EngineSession& session, const FileChange& change)
{
  if (&session != session_) return;
  if (change.created) refreshFileList();
  showFile(change.path, &change);
  appendChange(change);
}

void ViewerWindow::fileRemoved(EngineSession& session, const std::string& relativePath)
{
  if (&session != session_) return;
  refreshFileList();
  if (relativePath == currentFile_) {
    currentFile_.clear();
    code_->setText(QString());
  }
}

void ViewerWindow::fileRenamed(EngineSession& session, const std::string& from, const std::string& to)
{
  if (&session != session_) return;
  refreshFileList();
  if (from == currentFile_) showFile(to, nullptr);
}

void ViewerWindow::compileFinished(EngineSession& session, const CompileRequest& request,
                                   const CompileOutcome& outcome)
{
  if (&session != session_) return;
  const char *mode = request.mode == CompileMode::Check     ? "check"
                     : request.mode == CompileMode::Preview ? "preview"
                                                            : "render";
  lastCompile_ = tr("%1 %2: %3 errors, %4 warnings, %5 ms")
                   .arg(QString::fromUtf8(mode), outcome.ok ? tr("ok") : tr("FAILED"))
                   .arg(outcome.errors)
                   .arg(outcome.warnings)
                   .arg(static_cast<int>(outcome.millis));
  refreshMessages(&outcome);
  if (request.mode == CompileMode::Check) {
    updateStatus();
    return;
  }
  refreshParameters();
  refreshScene();
}

void ViewerWindow::cameraChanged(EngineSession& session)
{
  if (&session == session_) applyCamera();
}

void ViewerWindow::viewOptionsChanged(EngineSession& session)
{
  if (&session == session_) refreshScene();
}

void ViewerWindow::paramsChanged(EngineSession& session, const std::string&)
{
  if (&session == session_) refreshParameters();
}

void ViewerWindow::librariesChanged(EngineSession& session)
{
  if (&session == session_) refreshFileList();
}

}  // namespace mcp
