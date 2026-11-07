#include "mcp/viewer/ViewerBridge.h"

#include <QMetaObject>
#include <QThread>

#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>

#include "gui/QSettingsCached.h"
#include "mcp/BuildInfo.h"
#include "mcp/session/EngineSession.h"
#include "mcp/session/MessageLog.h"
#include "mcp/tools/CompileTools.h"
#include "mcp/tools/CoreTools.h"
#include "mcp/tools/DocsTools.h"
#include "mcp/tools/FileTools.h"
#include "mcp/tools/LibraryTools.h"
#include "mcp/tools/ParamTools.h"
#include "mcp/tools/ViewTools.h"
#include "mcp/viewer/ViewerWindow.h"
#include "version.h"

namespace fs = std::filesystem;

namespace mcp {

namespace {

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

ViewerBridge::ViewerBridge(Options options, ViewerWindow *window)
  : QObject(window),
    options_(std::move(options)),
    window_(window),
    sessions_("viewer", [](std::shared_ptr<Project> project) {
      return std::make_unique<EngineSession>(std::move(project));
    })
{
  MessageLog::instance().attach();
  registerCoreTools(tools_, sessions_);
  registerFileTools(tools_, sessions_);
  registerCompileTools(tools_, sessions_);
  registerViewTools(tools_, sessions_);
  registerParamTools(tools_, sessions_);
  registerLibraryTools(tools_, sessions_);
  registerDocsTools(tools_, sessions_);
  registerDocsResources(resources_, prompts_, sessions_);
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
        "paused: the user paused the agent in the viewer. Wait and retry; status "
        "reports paused=false once resumed.");
    }
    return std::nullopt;
  });
  token_ = options_.token.empty() ? generateToken() : options_.token;
  sessions_.setCurrentChanged(
    [this](ProjectSession *session) { window_->showSession(dynamic_cast<EngineSession *>(session)); });
  connect(this, &ViewerBridge::stateChanged, this, [this] {
    window_->setServerStatus(running() ? tr("MCP %1").arg(url()) : tr("server stopped"));
  });
}

ViewerBridge::~ViewerBridge()
{
  stop();
}

bool ViewerBridge::start(std::string *error)
{
  if (running()) return true;
  HttpOptions httpOptions;
  const int port = options_.port > 0
                     ? options_.port
                     : QSettingsCached().value("agent/port", Options::kDefaultPort).toInt();
  httpOptions.port = static_cast<unsigned short>(port);
  httpOptions.token = token_;
  stopping_ = false;
  transport_ = std::make_unique<HttpTransport>(*server_, httpOptions,
                                               [this](const std::function<void()>& fn) { execute(fn); });
  try {
    transport_->start();
  } catch (const std::exception& e) {
    if (error) *error = e.what();
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
  std::cerr << "agentic-openscad: MCP server listening on " << transport_->url() << "\n";
  if (options_.tokenFile.empty()) std::cerr << "agentic-openscad: bearer token " << token_ << "\n";
  std::cerr << "agentic-openscad: " << claudeCodeCommand().toStdString() << "\n";
  emit stateChanged();
  return true;
}

void ViewerBridge::stop()
{
  if (!transport_) return;
  stopping_ = true;
  transport_->stop();
  transport_.reset();
  emit stateChanged();
}

bool ViewerBridge::running() const
{
  return transport_ && transport_->running();
}

QString ViewerBridge::url() const
{
  return transport_ ? QString::fromStdString(transport_->url()) : QString();
}

QString ViewerBridge::claudeCodeCommand() const
{
  if (!running()) return QString();
  return QStringLiteral(
           "claude mcp add --transport http openscad %1 --header \"Authorization: Bearer %2\"")
    .arg(url(), token());
}

void ViewerBridge::setPaused(bool paused)
{
  if (paused_ == paused) return;
  paused_ = paused;
  for (auto *session : sessions_.sessions()) {
    if (auto *engine = dynamic_cast<EngineSession *>(session)) engine->setPaused(paused);
  }
  emit stateChanged();
}

void ViewerBridge::execute(const std::function<void()>& fn)
{
  if (QThread::currentThread() == thread()) {
    fn();
    return;
  }
  auto task = std::make_shared<Task>();
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
      task->abandoned = true;
      return;
    }
  }
}

void ViewerBridge::pump()
{
  std::shared_ptr<Task> task;
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

}  // namespace mcp
