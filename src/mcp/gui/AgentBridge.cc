#include "mcp/gui/AgentBridge.h"

#include <QFileInfo>
#include <QMetaObject>
#include <QStatusBar>
#include <QStringList>
#include <QThread>
#include <QTimer>

#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>

#include "gui/Editor.h"
#include "gui/MainWindow.h"
#include "gui/OpenSCADApp.h"
#include "gui/QSettingsCached.h"
#include "gui/TabManager.h"
#include "mcp/BuildInfo.h"
#include "mcp/gui/AgentStatusWidget.h"
#include "mcp/gui/GuiSession.h"
#include "mcp/session/MessageLog.h"
#include "mcp/tools/CompileTools.h"
#include "mcp/tools/CoreTools.h"
#include "mcp/tools/FileTools.h"
#include "mcp/tools/LibraryTools.h"
#include "mcp/tools/ParamTools.h"
#include "mcp/tools/ViewTools.h"
#include "utils/printutils.h"
#include "version.h"

namespace fs = std::filesystem;

namespace mcp {

namespace {

// Compact one-line rendering of tool arguments for the activity log.
QString summarizeArgs(const nlohmann::json& args)
{
  if (!args.is_object() || args.empty()) return QString();
  nlohmann::json shortened = nlohmann::json::object();
  for (const auto& [key, value] : args.items()) {
    if (value.is_string() && value.get<std::string>().size() > 60) {
      shortened[key] = value.get<std::string>().substr(0, 57) + "...";
    } else if (value.is_array() || value.is_object()) {
      shortened[key] = value.is_array() ? "[" + std::to_string(value.size()) + " items]" : "{...}";
    } else {
      shortened[key] = value;
    }
  }
  return QString::fromStdString(
    shortened.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
}

}  // namespace

AgentBridge *AgentBridge::createIfConfigured(OpenSCADApp *app)
{
  auto *bridge = new AgentBridge(app, Options::global());
  for (auto *window : app->windowManager.getWindows()) bridge->attachStatusWidget(window);

  int port = bridge->options_.port;
  if (port <= 0 && bridge->autoStart()) port = bridge->configuredPort();
  if (port > 0) {
    bridge->options_.port = port;
    QString error;
    if (!bridge->start(&error)) LOG(message_group::Error, "%1$s", error.toStdString());
  }
  if (!bridge->options_.project.empty()) {
    try {
      bridge->sessions_.open(bridge->options_.project);
    } catch (const std::exception& e) {
      LOG(message_group::Error, "Agent project: %1$s", e.what());
    }
  }
  return bridge;
}

AgentBridge::AgentBridge(OpenSCADApp *app, Options options)
  : QObject(app),
    app_(app),
    options_(std::move(options)),
    sessions_("gui",
              [this](std::shared_ptr<Project> project) { return makeSession(std::move(project)); })
{
  MessageLog::instance().attach();
  registerCoreTools(tools_, sessions_);
  registerFileTools(tools_, sessions_);
  registerCompileTools(tools_, sessions_);
  registerViewTools(tools_, sessions_);
  registerParamTools(tools_, sessions_);
  registerLibraryTools(tools_, sessions_);
  server_ =
    std::make_unique<Server>(ServerInfo{serverName(), openscad_versionnumber, serverInstructions()},
                             tools_, resources_, prompts_);
  server_->setToolObserver([this](const std::string& name, const nlohmann::json& args,
                                  const ToolResult& result, double millis) {
    emit toolCalled(QString::fromStdString(name), summarizeArgs(args), !result.isError, millis);
  });
  server_->setToolGate([this](const std::string& name) -> std::optional<std::string> {
    if (paused_ && name != "status" && name != "version") {
      return std::string(
        "paused: the user paused the agent in the OpenSCAD GUI. Wait and retry; "
        "status reports paused=false once resumed.");
    }
    return std::nullopt;
  });
  token_ = options_.token.empty() ? generateToken() : options_.token;
}

AgentBridge::~AgentBridge()
{
  stop();
}

bool AgentBridge::start(QString *error)
{
  if (running()) return true;
  HttpOptions httpOptions;
  httpOptions.port = static_cast<unsigned short>(options_.port > 0 ? options_.port : configuredPort());
  httpOptions.token = token_;
  stopping_ = false;
  transport_ = std::make_unique<HttpTransport>(*server_, httpOptions,
                                               [this](const std::function<void()>& fn) { execute(fn); });
  try {
    transport_->start();
  } catch (const std::exception& e) {
    if (error) *error = QString::fromUtf8(e.what());
    transport_.reset();
    return false;
  }
  if (!options_.tokenFile.empty()) {
    std::ofstream out(options_.tokenFile, std::ios::trunc);
    out << token_ << '\n';
    out.close();
    std::error_code ec;
    fs::permissions(options_.tokenFile, fs::perms::owner_read | fs::perms::owner_write,
                    fs::perm_options::replace, ec);
  }
  LOG("Agent MCP server listening on %1$s", transport_->url());
  emit stateChanged();
  return true;
}

void AgentBridge::stop()
{
  if (!transport_) return;
  stopping_ = true;  // releases a transport thread waiting on a queued task
  transport_->stop();
  transport_.reset();
  emit stateChanged();
}

bool AgentBridge::running() const
{
  return transport_ && transport_->running();
}

unsigned short AgentBridge::port() const
{
  return transport_ ? transport_->port() : 0;
}

QString AgentBridge::url() const
{
  return transport_ ? QString::fromStdString(transport_->url()) : QString();
}

QString AgentBridge::claudeCodeCommand() const
{
  if (!running()) return QString();
  return QStringLiteral(
           "claude mcp add --transport http openscad %1 --header \"Authorization: Bearer %2\"")
    .arg(url(), token());
}

int AgentBridge::configuredPort() const
{
  return QSettingsCached().value("agent/port", kDefaultPort).toInt();
}

void AgentBridge::setConfiguredPort(int port)
{
  QSettingsCached().setValue("agent/port", port);
  options_.port = 0;  // use the configured value from now on
}

bool AgentBridge::autoStart() const
{
  return QSettingsCached().value("agent/autoStart", false).toBool();
}

void AgentBridge::setAutoStart(bool enabled)
{
  QSettingsCached().setValue("agent/autoStart", enabled);
}

void AgentBridge::setPaused(bool paused)
{
  if (paused_ == paused) return;
  paused_ = paused;
  emit stateChanged();
}

bool AgentBridge::followAgent() const
{
  return QSettingsCached().value("agent/followAgent", true).toBool();
}

void AgentBridge::setFollowAgent(bool follow)
{
  QSettingsCached().setValue("agent/followAgent", follow);
  emit stateChanged();
}

void AgentBridge::execute(const std::function<void()>& fn)
{
  if (QThread::currentThread() == thread()) {
    fn();
    return;
  }
  auto task = std::make_shared<GuiTask>();
  task->fn = fn;
  auto future = task->done.get_future();
  {
    const std::lock_guard<std::mutex> lock(tasksMutex_);
    tasks_.push_back(task);
  }
  QMetaObject::invokeMethod(this, [this] { pump(); }, Qt::QueuedConnection);

  while (future.wait_for(std::chrono::milliseconds(50)) != std::future_status::ready) {
    if (!stopping_) continue;
    const std::lock_guard<std::mutex> lock(tasksMutex_);
    if (!task->running) {
      task->abandoned = true;  // the GUI will skip it; `fn` must not be touched after we return
      return;
    }
    // Already running on the GUI thread: fall through and keep waiting for it to finish.
  }
}

void AgentBridge::pump()
{
  if (GuiLocker::isLocked()) {
    // OpenSCAD is compiling or rendering (possibly pumping events right now);
    // try again once it is done.
    QTimer::singleShot(25, this, [this] { pump(); });
    return;
  }
  std::shared_ptr<GuiTask> task;
  {
    const std::lock_guard<std::mutex> lock(tasksMutex_);
    while (!tasks_.empty() && tasks_.front()->abandoned) tasks_.pop_front();
    if (tasks_.empty()) return;
    task = tasks_.front();
    tasks_.pop_front();
    task->running = true;
  }
  task->fn();
  task->done.set_value();
  const std::lock_guard<std::mutex> lock(tasksMutex_);
  if (!tasks_.empty()) QMetaObject::invokeMethod(this, [this] { pump(); }, Qt::QueuedConnection);
}

MainWindow *AgentBridge::adoptableWindow(const Project& project) const
{
  const QString root = QString::fromStdString(project.root().string());
  for (auto *window : app_->windowManager.getWindows()) {
    if (boundWindows_.contains(window) || !window->activeEditor) continue;
    const QString filepath = window->activeEditor->filepath;
    if (filepath.isEmpty()) {
      if (!window->activeEditor->isContentModified()) return window;
      continue;
    }
    if (QFileInfo(filepath).canonicalFilePath().startsWith(root + "/")) return window;
  }
  return nullptr;
}

void AgentBridge::bindWindow(MainWindow *window, const std::string& projectId)
{
  boundWindows_.insert(window);
  connect(window, &QObject::destroyed, this, [this, window, projectId] {
    boundWindows_.remove(window);
    windowsWithStatusWidget_.remove(window);
    sessions_.close(projectId);
  });
  attachStatusWidget(window);
}

void AgentBridge::attachStatusWidget(MainWindow *window)
{
  if (windowsWithStatusWidget_.contains(window)) return;
  windowsWithStatusWidget_.insert(window);
  window->statusBar()->addPermanentWidget(new AgentStatusWidget(this, window));
}

std::unique_ptr<ProjectSession> AgentBridge::makeSession(std::shared_ptr<Project> project)
{
  MainWindow *window = adoptableWindow(*project);
  if (!window) window = new MainWindow(QStringList{});
  std::error_code ec;
  if (fs::is_regular_file(project->mainPath(), ec)) {
    window->tabManager->open(QString::fromStdString(project->mainPath().string()));
  }
  window->setWindowTitle(QString::fromStdString(project->name()) + " - OpenSCAD");
  bindWindow(window, project->id());
  return std::make_unique<GuiSession>(std::move(project), window, this);
}

}  // namespace mcp
