#pragma once

#include <QDialog>

class QCheckBox;
class QHideEvent;
class QPushButton;
class QShowEvent;
class QTimer;
class QTreeWidget;

namespace mcp {

class ViewerBridge;

// Lists, adds and deletes the session-only access tokens of the viewer's
// MCP server. Values are masked unless "Show values" is checked; the copy
// buttons put the full token or the `claude mcp add` command on the
// clipboard. While visible, it refreshes every 2 s so "Last used" (stamped
// by the HTTP thread) stays current.
class TokenDialog : public QDialog
{
  Q_OBJECT

public:
  TokenDialog(ViewerBridge& bridge, QWidget *parent);

protected:
  void showEvent(QShowEvent *event) override;
  void hideEvent(QHideEvent *event) override;

private:
  // Rebuilds the rows from the bridge; keeps the row with `selectId`
  // selected (-1: keep the current selection).
  void refresh(int selectId = -1);
  int selectedId() const;
  QString selectedValue() const;
  void updateButtons();
  void addToken();
  void deleteToken();

  ViewerBridge& bridge_;
  QTreeWidget *list_;
  QPushButton *deleteButton_;
  QPushButton *copyTokenButton_;
  QPushButton *copyCommandButton_;
  QCheckBox *showValues_;
  QTimer *refreshTimer_;
};

}  // namespace mcp
