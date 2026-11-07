#pragma once

#include <QFont>
#include <QMainWindow>
#include <QString>

#include <array>
#include <map>
#include <memory>
#include <set>
#include <string>

#include "mcp/session/SessionObserver.h"

class QCloseEvent;
class QDockWidget;
class QGLView;
class QLabel;
class QPlainTextEdit;
class QTableWidget;
class QTextBrowser;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;
class QsciScintilla;
class QStackedWidget;
class Renderer;

namespace mcp {

class EngineSession;

// The agent viewer: the only window of agent mode. It mirrors the current
// session: the 3D view shows the last preview or render with the camera and
// view options the agent set; the code pane shows the file the agent last
// touched (read-only, changed lines highlighted); messages, activity, the
// change stream and the customizer parameters complete the picture. The
// person can orbit the view with the mouse, pick a file to look at, and
// pause the agent; nothing else is editable.
class ViewerWindow : public QMainWindow, public SessionObserver
{
  Q_OBJECT

public:
  ViewerWindow();

  // Follows the project of the last tool call.
  void showSession(EngineSession *session);
  EngineSession *session() const { return session_; }

  // SessionObserver (called on the GUI thread, inside the tool call)
  void fileWritten(EngineSession& session, const FileChange& change) override;
  void fileRemoved(EngineSession& session, const std::string& relativePath) override;
  void fileRenamed(EngineSession& session, const std::string& from, const std::string& to) override;
  void fileExported(EngineSession& session, const std::string& relativePath) override;
  void compileFinished(EngineSession& session, const CompileRequest& request,
                       const CompileOutcome& outcome) override;
  void cameraChanged(EngineSession& session) override;
  void viewOptionsChanged(EngineSession& session) override;
  void paramsChanged(EngineSession& session, const std::string& relativePath) override;
  void librariesChanged(EngineSession& session) override;

public slots:
  void setServerStatus(const QString& text);
  void appendToolCall(const QString& name, const QString& argsSummary, bool ok, double millis);
  void setPausedIndicator(bool paused);

signals:
  void pauseToggled(bool paused);

private:
  static constexpr int kChangeMarker = 1;

  void buildUi();
  void refreshScene();
  void applyCamera();
  void applyViewOptions();
  void updateStatus();
  std::shared_ptr<Renderer> buildRenderer();
  void refreshFileList();
  // Selects the item of currentFile_ and expands the folders above it.
  void selectCurrentFile();
  void showFile(const std::string& relativePath, const FileChange *change);
  void refreshMessages(const CompileOutcome *outcome);
  void refreshParameters();
  void appendChange(const FileChange& change);

  // The activity log and the change stream are kept per project (keyed by
  // the project root; "" before any project is open) and the pair of the
  // project on screen is shown.
  struct ProjectPanes {
    QPlainTextEdit *activity;
    QTextBrowser *changes;
  };
  ProjectPanes& panesFor(const std::string& projectRoot);
  void selectPanes(const std::string& projectRoot);

  // Every panel is a closable dock with a fixed position; a toggle button
  // per panel lives in the status bar. The 3D view is the central widget,
  // so it fills whatever the hidden panels free up.
  enum PanelId {
    PanelFiles,
    PanelCode,
    PanelParameters,
    PanelMessages,
    PanelActivity,
    PanelChanges,
    PanelCount
  };
  struct Panel {
    QDockWidget *dock = nullptr;
    QToolButton *button = nullptr;
    bool wasOpen = true;      // the state "View only" restores
    bool alertError = false;  // a red dot is showing (not downgraded by later neutral events)
  };
  QDockWidget *addPanel(PanelId id, const char *objectName, const QString& title, QWidget *widget,
                        Qt::DockWidgetArea area);
  // A hidden panel that receives new content shows a dot on its toggle
  // button (red for errors) until it is opened; nothing opens by itself.
  void setPanelAlert(PanelId id, bool error);
  void clearPanelAlert(PanelId id);
  // "View only" hides every open panel and restores exactly that set.
  void setViewOnly(bool on);
  // Panel visibility and sizes persist in the OpenSCAD settings store.
  void saveLayout();
  void restoreLayout();
  void closeEvent(QCloseEvent *event) override;

  EngineSession *session_ = nullptr;
  std::string currentFile_;
  std::array<Panel, PanelCount> panels_;
  QToolButton *viewOnlyButton_ = nullptr;
  bool switchingViewOnly_ = false;
  QGLView *view_;
  QsciScintilla *code_;
  QTreeWidget *files_;
  std::set<std::string> collapsedFolders_;  // folders the person collapsed; kept across rebuilds
  QTextBrowser *messages_;
  QStackedWidget *activityStack_;
  QStackedWidget *changesStack_;
  std::map<std::string, ProjectPanes> panes_;
  QPlainTextEdit *activity_;  // the pair of the project on screen
  QTextBrowser *changes_;
  QFont monoFont_;
  QTableWidget *parameters_;
  QToolButton *pauseButton_;
  QLabel *statusLabel_;
  QLabel *serverLabel_;
  std::shared_ptr<Renderer> renderer_;
  QString lastCompile_;
};

}  // namespace mcp
