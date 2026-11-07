#pragma once

#include <QMainWindow>
#include <QString>

class QLabel;

namespace mcp {

// The agent viewer: the only window of agent mode. Stage V0 shows the
// server state; later stages add the 3D view, code pane, messages,
// activity and parameters.
class ViewerWindow : public QMainWindow
{
  Q_OBJECT

public:
  ViewerWindow();

public slots:
  void setServerStatus(const QString& text);

private:
  QLabel *placeholder_;
  QLabel *statusLabel_;
};

}  // namespace mcp
