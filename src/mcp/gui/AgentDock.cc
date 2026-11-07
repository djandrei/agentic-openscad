#include "mcp/gui/AgentDock.h"

#include <QCheckBox>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QTextBrowser>
#include <QTime>
#include <QToolButton>
#include <QTreeView>
#include <QUrlQuery>
#include <QVBoxLayout>

#include <algorithm>
#include <string>

#include "gui/Editor.h"
#include "gui/MainWindow.h"
#include "gui/TabManager.h"
#include "mcp/gui/AgentBridge.h"
#include "mcp/session/FileStore.h"

namespace mcp {

namespace {

constexpr int kMaxHunkLinesShown = 40;

QString escaped(const std::string& text)
{
  return QString::fromStdString(text).toHtmlEscaped();
}

}  // namespace

AgentDock::AgentDock(AgentBridge *bridge, MainWindow *window, const std::filesystem::path& projectRoot)
  : Dock(window), bridge_(bridge), window_(window), root_(projectRoot)
{
  setObjectName("agentDock");
  setName(tr("Agent"));
  setWindowTitle(tr("Agent"));

  auto *body = new QWidget(this);
  auto *layout = new QVBoxLayout(body);
  layout->setContentsMargins(4, 4, 4, 4);

  auto *controls = new QHBoxLayout();
  pauseButton_ = new QToolButton(body);
  pauseButton_->setCheckable(true);
  pauseButton_->setText(tr("Pause agent"));
  pauseButton_->setToolTip(tr("While paused, every agent tool call fails until you resume."));
  connect(pauseButton_, &QToolButton::toggled, this,
          [this](bool checked) { bridge_->setPaused(checked); });
  controls->addWidget(pauseButton_);

  followBox_ = new QCheckBox(tr("Follow agent"), body);
  followBox_->setToolTip(tr("Switch to the file and line the agent is changing."));
  followBox_->setChecked(bridge_->followAgent());
  connect(followBox_, &QCheckBox::toggled, this,
          [this](bool checked) { bridge_->setFollowAgent(checked); });
  controls->addWidget(followBox_);

  controls->addStretch();
  stateLabel_ = new QLabel(body);
  controls->addWidget(stateLabel_);
  layout->addLayout(controls);

  auto *splitter = new QSplitter(Qt::Vertical, body);

  model_ = new QFileSystemModel(this);
  model_->setReadOnly(true);
  const QString rootPath = QString::fromStdString(root_.string());
  model_->setRootPath(rootPath);
  tree_ = new QTreeView(splitter);
  tree_->setModel(model_);
  tree_->setRootIndex(model_->index(rootPath));
  tree_->setHeaderHidden(true);
  for (int column = 1; column < model_->columnCount(); ++column) tree_->hideColumn(column);
  connect(tree_, &QTreeView::activated, this, [this](const QModelIndex& index) {
    if (model_->isDir(index)) return;
    window_->tabManager->open(model_->filePath(index));
  });
  splitter->addWidget(tree_);

  activity_ = new QPlainTextEdit(splitter);
  activity_->setReadOnly(true);
  activity_->setMaximumBlockCount(2000);
  activity_->setPlaceholderText(tr("Tool calls made by the agent appear here."));
  splitter->addWidget(activity_);

  changes_ = new QTextBrowser(splitter);
  changes_->setOpenLinks(false);
  changes_->setPlaceholderText(
    tr("Every change the agent writes appears here. Click a file name to jump to it."));
  connect(changes_, &QTextBrowser::anchorClicked, this, &AgentDock::openLocation);
  splitter->addWidget(changes_);

  splitter->setStretchFactor(0, 2);
  splitter->setStretchFactor(1, 2);
  splitter->setStretchFactor(2, 3);
  layout->addWidget(splitter);
  setWidget(body);

  connect(bridge_, &AgentBridge::stateChanged, this, &AgentDock::refreshState);
  connect(bridge_, &AgentBridge::toolCalled, this, &AgentDock::appendToolCall);
  refreshState();
}

void AgentDock::refreshState()
{
  pauseButton_->setChecked(bridge_->paused());
  followBox_->setChecked(bridge_->followAgent());
  if (!bridge_->running()) {
    stateLabel_->setText(tr("server off"));
  } else if (bridge_->paused()) {
    stateLabel_->setText(tr("paused"));
  } else {
    stateLabel_->setText(tr("listening on :%1").arg(bridge_->port()));
  }
}

void AgentDock::appendToolCall(const QString& name, const QString& argsSummary, bool ok, double millis)
{
  activity_->appendPlainText(QStringLiteral("%1  %2 %3  %4 (%5 ms)")
                               .arg(QTime::currentTime().toString("hh:mm:ss"), name, argsSummary,
                                    ok ? QStringLiteral("ok") : QStringLiteral("ERROR"))
                               .arg(millis, 0, 'f', 0));
}

void AgentDock::appendChange(const FileChange& change)
{
  const int startLine = change.hunk ? static_cast<int>(change.hunk->startLine) : 1;
  QUrl link(QStringLiteral("agentic:open"));
  QUrlQuery query;
  query.addQueryItem("path", QString::fromStdString(change.path));
  query.addQueryItem("line", QString::number(startLine));
  link.setQuery(query);

  QString html = QStringLiteral("<div style='margin-top:6px'><b><a href='%1'>%2</a></b> ")
                   .arg(link.toString(QUrl::FullyEncoded), escaped(change.path));
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

void AgentDock::openLocation(const QUrl& url)
{
  const QUrlQuery query(url);
  const QString path = query.queryItemValue("path", QUrl::FullyDecoded);
  const int line = query.queryItemValue("line").toInt();
  if (path.isEmpty()) return;
  const QString absolute = QString::fromStdString((root_ / path.toStdString()).string());
  window_->tabManager->open(absolute);
  if (window_->tabManager->editor)
    window_->tabManager->editor->setCursorPosition(std::max(line - 1, 0), 0);
}

}  // namespace mcp
