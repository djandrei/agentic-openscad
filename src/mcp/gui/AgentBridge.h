#pragma once

#include <QObject>
#include <QSet>
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

class MainWindow;
class OpenSCADApp;

namespace mcp {

// Hosts the MCP server inside the GUI process. Owns the registries, the
// server, the sessions and the HTTP transport; runs every request on the GUI
// thread when no compile is in progress; maps projects to MainWindows; holds
// the user-side controls (pause, follow agent).
class AgentBridge : public QObject
{
  Q_OBJECT

public:
  static constexpr int kDefaultPort = 7645;

  // Creates the bridge for the application and starts the server when
  // --mcp-port was given or auto-start is enabled in the settings. Opens
  // --project when given. Returns the bridge (owned by the application).
  static AgentBridge *createIfConfigured(OpenSCADApp *app);

  AgentBridge(OpenSCADApp *app, Options options);
  ~AgentBridge() override;

  bool start(QString *error = nullptr);
  void stop();
  bool running() const;
  unsigned short port() const;
  QString token() const { return QString::fromStdString(token_); }
  QString url() const;
  QString claudeCodeCommand() const;

  int configuredPort() const;
  void setConfiguredPort(int port);
  bool autoStart() const;
  void setAutoStart(bool enabled);

  // User controls shared by all windows.
  bool paused() const { return paused_; }
  void setPaused(bool paused);
  bool followAgent() const;
  void setFollowAgent(bool follow);

  SessionManager& sessions() { return sessions_; }

signals:
  void stateChanged();
  void toolCalled(const QString& name, const QString& argsSummary, bool ok, double millis);

private:
  // A request handed from the transport thread to the GUI thread.
  struct GuiTask {
    std::function<void()> fn;
    std::promise<void> done;
    bool running = false;
    bool abandoned = false;
  };

  // Transport-thread side: queues `fn` for the GUI thread and waits for it.
  void execute(const std::function<void()>& fn);
  // GUI-thread side: runs queued tasks, but only when OpenSCAD is not inside
  // a compile or render (otherwise the task would run nested in the event
  // pumping of that compile and could never wait for it to finish).
  void pump();

  std::unique_ptr<ProjectSession> makeSession(std::shared_ptr<Project> project);
  MainWindow *adoptableWindow(const Project& project) const;
  void bindWindow(MainWindow *window, const std::string& projectId);
  void attachStatusWidget(MainWindow *window);

  OpenSCADApp *app_;
  Options options_;
  ToolRegistry tools_;
  ResourceRegistry resources_;
  PromptRegistry prompts_;
  std::unique_ptr<Server> server_;
  SessionManager sessions_;
  std::unique_ptr<HttpTransport> transport_;
  std::string token_;
  bool paused_ = false;
  std::atomic<bool> stopping_{false};
  std::mutex tasksMutex_;
  std::deque<std::shared_ptr<GuiTask>> tasks_;
  QSet<MainWindow *> boundWindows_;
  QSet<MainWindow *> windowsWithStatusWidget_;
};

}  // namespace mcp
