#pragma once

#include <QMainWindow>
#include <QString>

#include <memory>
#include <string>

#include "mcp/session/SessionObserver.h"

class QGLView;
class QLabel;
class Renderer;

namespace mcp {

class EngineSession;

// The agent viewer: the only window of agent mode. It mirrors the current
// session: the 3D view shows the last preview or render with the camera and
// view options the agent set. A person can orbit with the mouse; the agent's
// camera stays the reference and the view snaps back on the agent's next
// camera or view action.
class ViewerWindow : public QMainWindow, public SessionObserver
{
  Q_OBJECT

public:
  ViewerWindow();

  // Follows the project of the last tool call.
  void showSession(EngineSession *session);
  EngineSession *session() const { return session_; }

  // SessionObserver (called on the GUI thread, inside the tool call)
  void compileFinished(EngineSession& session, const CompileRequest& request,
                       const CompileOutcome& outcome) override;
  void cameraChanged(EngineSession& session) override;
  void viewOptionsChanged(EngineSession& session) override;

public slots:
  void setServerStatus(const QString& text);

private:
  void refreshScene();
  void applyCamera();
  void applyViewOptions();
  void updateStatus();
  std::shared_ptr<Renderer> buildRenderer();

  EngineSession *session_ = nullptr;
  QGLView *view_;
  QLabel *statusLabel_;
  QLabel *serverLabel_;
  std::shared_ptr<Renderer> renderer_;
  QString lastCompile_;
};

}  // namespace mcp
