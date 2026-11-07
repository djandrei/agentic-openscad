#include "mcp/gui/AgentStatusWidget.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>

#include "mcp/gui/AgentBridge.h"

namespace mcp {

AgentStatusWidget::AgentStatusWidget(AgentBridge *bridge, QWidget *parent)
  : QToolButton(parent), bridge_(bridge), menu_(new QMenu(this))
{
  setPopupMode(QToolButton::InstantPopup);
  setAutoRaise(true);
  setToolTip(tr("Agent MCP server"));

  toggleAction_ = menu_->addAction(QString(), this, &AgentStatusWidget::toggleServer);
  pauseAction_ = menu_->addAction(tr("Pause agent"));
  pauseAction_->setCheckable(true);
  connect(pauseAction_, &QAction::toggled, this, [this](bool checked) { bridge_->setPaused(checked); });
  menu_->addSeparator();
  copyCommandAction_ =
    menu_->addAction(tr("Copy Claude Code command"), this, &AgentStatusWidget::copyCommand);
  copyTokenAction_ = menu_->addAction(tr("Copy bearer token"), this, &AgentStatusWidget::copyToken);
  menu_->addSeparator();
  portAction_ = menu_->addAction(QString(), this, &AgentStatusWidget::choosePort);
  autoStartAction_ = menu_->addAction(tr("Start server when OpenSCAD starts"));
  autoStartAction_->setCheckable(true);
  connect(autoStartAction_, &QAction::toggled, this,
          [this](bool checked) { bridge_->setAutoStart(checked); });
  setMenu(menu_);

  connect(bridge_, &AgentBridge::stateChanged, this, &AgentStatusWidget::refresh);
  refresh();
}

void AgentStatusWidget::refresh()
{
  const bool running = bridge_->running();
  if (!running) {
    setText(tr("Agent: off"));
  } else if (bridge_->paused()) {
    setText(tr("Agent: paused"));
  } else {
    setText(tr("Agent: :%1").arg(bridge_->port()));
  }
  toggleAction_->setText(running ? tr("Stop MCP server") : tr("Start MCP server"));
  pauseAction_->setChecked(bridge_->paused());
  copyCommandAction_->setEnabled(running);
  copyTokenAction_->setEnabled(running);
  portAction_->setText(tr("Set port… (%1)").arg(bridge_->configuredPort()));
  portAction_->setEnabled(!running);
  autoStartAction_->setChecked(bridge_->autoStart());
}

void AgentStatusWidget::toggleServer()
{
  if (bridge_->running()) {
    bridge_->stop();
    return;
  }
  QString error;
  if (!bridge_->start(&error)) {
    QMessageBox::warning(this, tr("Agent MCP server"), error);
  }
}

void AgentStatusWidget::copyCommand()
{
  QApplication::clipboard()->setText(bridge_->claudeCodeCommand());
}

void AgentStatusWidget::copyToken()
{
  QApplication::clipboard()->setText(bridge_->token());
}

void AgentStatusWidget::choosePort()
{
  bool ok = false;
  const int port = QInputDialog::getInt(this, tr("Agent MCP server port"), tr("TCP port on 127.0.0.1:"),
                                        bridge_->configuredPort(), 1, 65535, 1, &ok);
  if (ok) {
    bridge_->setConfiguredPort(port);
    refresh();
  }
}

}  // namespace mcp
