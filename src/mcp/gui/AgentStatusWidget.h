#pragma once

#include <QToolButton>

class QMenu;
class QAction;

namespace mcp {

class AgentBridge;

// Status-bar control showing whether the MCP server is running and offering
// start/stop, the Claude Code registration command and the port setting.
class AgentStatusWidget : public QToolButton
{
  Q_OBJECT

public:
  AgentStatusWidget(AgentBridge *bridge, QWidget *parent);

private:
  void refresh();
  void toggleServer();
  void copyCommand();
  void copyToken();
  void choosePort();

  AgentBridge *bridge_;
  QMenu *menu_;
  QAction *toggleAction_;
  QAction *copyCommandAction_;
  QAction *copyTokenAction_;
  QAction *portAction_;
  QAction *autoStartAction_;
};

}  // namespace mcp
