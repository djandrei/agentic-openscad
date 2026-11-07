#pragma once

#include <QObject>
#include <QString>

#include <atomic>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>

#include "mcp/Options.h"
#include "mcp/protocol/Registry.h"
#include "mcp/protocol/Server.h"
#include "mcp/session/SessionManager.h"
#include "mcp/transport/HttpTransport.h"

namespace mcp {

class ViewerWindow;

// Hosts the MCP server inside the viewer application: registries, server,
// sessions (the same engine-backed sessions headless mode uses) and the HTTP
// transport. Every request is executed on the Qt main thread through a queue
// so OpenSCAD's single-threaded evaluation and the viewer stay consistent.
class ViewerBridge : public QObject
{
  Q_OBJECT

public:
  ViewerBridge(Options options, ViewerWindow *window);
  ~ViewerBridge() override;

  bool start(std::string *error);
  void stop();
  bool running() const;
  QString url() const;
  QString token() const { return QString::fromStdString(token_); }
  QString claudeCodeCommand() const;

  bool paused() const { return paused_; }
  void setPaused(bool paused);

  SessionManager& sessions() { return sessions_; }

signals:
  void stateChanged();
  void toolCalled(const QString& name, const QString& argsSummary, bool ok, double millis);

private:
  struct Task {
    std::function<void()> fn;
    std::promise<void> done;
    bool running = false;
    bool abandoned = false;
  };

  void execute(const std::function<void()>& fn);
  void pump();

  Options options_;
  ViewerWindow *window_;
  ToolRegistry tools_;
  ResourceRegistry resources_;
  PromptRegistry prompts_;
  SessionManager sessions_;
  std::unique_ptr<Server> server_;
  std::unique_ptr<HttpTransport> transport_;
  std::string token_;
  bool paused_ = false;
  std::atomic<bool> stopping_{false};
  std::mutex tasksMutex_;
  std::deque<std::shared_ptr<Task>> tasks_;
};

}  // namespace mcp
