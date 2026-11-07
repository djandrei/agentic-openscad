#include "mcp/viewer/ViewerWindow.h"

#include <QLabel>
#include <QStatusBar>

namespace mcp {

ViewerWindow::ViewerWindow()
{
  setWindowTitle(tr("Agentic OpenSCAD"));
  resize(1200, 800);
  placeholder_ = new QLabel(tr("Waiting for the agent…"), this);
  placeholder_->setAlignment(Qt::AlignCenter);
  setCentralWidget(placeholder_);
  statusLabel_ = new QLabel(this);
  statusBar()->addWidget(statusLabel_, 1);
}

void ViewerWindow::setServerStatus(const QString& text)
{
  statusLabel_->setText(text);
}

}  // namespace mcp
