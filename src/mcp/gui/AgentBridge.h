#pragma once

#include <QObject>
#include <QSet>
#include <QString>

#include <functional>
#include <memory>
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
// server, the sessions and the HTTP transport; marshals every request onto
// the GUI thread; maps projects to MainWindows; holds the user-side controls
// (pause, follow agent).
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
  void execute(const std::function<void()>& fn);
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
  QSet<MainWindow *> boundWindows_;
  QSet<MainWindow *> windowsWithStatusWidget_;
};

}  // namespace mcp
