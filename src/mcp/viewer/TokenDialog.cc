#include "mcp/viewer/TokenDialog.h"

#include <QCheckBox>
#include <QClipboard>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <chrono>
#include <string>
#include <vector>

#include "mcp/viewer/ViewerBridge.h"

namespace mcp {

namespace {

enum Column { ColumnLabel, ColumnToken, ColumnCreated, ColumnLastUsed, ColumnCount };
constexpr int kIdRole = Qt::UserRole;
constexpr int kValueRole = Qt::UserRole + 1;

QString maskedValue(const std::string& value)
{
  if (value.size() <= 8) return QString(static_cast<int>(value.size()), QChar('*'));
  return QString::fromStdString(value.substr(0, 4)) + QStringLiteral("…") +
         QString::fromStdString(value.substr(value.size() - 4));
}

QString formatTime(std::chrono::system_clock::time_point time)
{
  const auto msecs =
    std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count();
  return QDateTime::fromMSecsSinceEpoch(msecs).toString(QStringLiteral("hh:mm:ss"));
}

QString describe(const QTreeWidgetItem *item)
{
  const QString label = item->text(ColumnLabel);
  const QString masked = maskedValue(item->data(ColumnToken, kValueRole).toString().toStdString());
  return label.isEmpty() ? masked : QStringLiteral("%1 (%2)").arg(label, masked);
}

}  // namespace

TokenDialog::TokenDialog(ViewerBridge& bridge, QWidget *parent) : QDialog(parent), bridge_(bridge)
{
  setWindowTitle(tr("Access tokens"));

  auto *note =
    new QLabel(tr("MCP clients authenticate with one of these tokens. They are valid until the viewer "
                  "closes; none are saved."),
               this);
  note->setWordWrap(true);

  list_ = new QTreeWidget(this);
  list_->setColumnCount(ColumnCount);
  list_->setHeaderLabels({tr("Label"), tr("Token"), tr("Created"), tr("Last used")});
  list_->setRootIsDecorated(false);
  list_->setSelectionMode(QAbstractItemView::SingleSelection);
  list_->header()->setStretchLastSection(false);
  list_->header()->setSectionResizeMode(ColumnLabel, QHeaderView::Stretch);
  for (int column = ColumnToken; column < ColumnCount; ++column) {
    list_->header()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
  }
  connect(list_, &QTreeWidget::itemSelectionChanged, this, &TokenDialog::updateButtons);

  auto *addButton = new QPushButton(tr("Add…"), this);
  deleteButton_ = new QPushButton(tr("Delete"), this);
  copyTokenButton_ = new QPushButton(tr("Copy token"), this);
  copyCommandButton_ = new QPushButton(tr("Copy claude mcp add"), this);
  copyCommandButton_->setToolTip(tr("Copy the command that registers this server in Claude Code."));
  showValues_ = new QCheckBox(tr("Show values"), this);
  connect(addButton, &QPushButton::clicked, this, &TokenDialog::addToken);
  connect(deleteButton_, &QPushButton::clicked, this, &TokenDialog::deleteToken);
  connect(copyTokenButton_, &QPushButton::clicked, this,
          [this] { QGuiApplication::clipboard()->setText(selectedValue()); });
  connect(copyCommandButton_, &QPushButton::clicked, this,
          [this] { QGuiApplication::clipboard()->setText(bridge_.claudeCodeCommand(selectedValue())); });
  connect(showValues_, &QCheckBox::toggled, this, [this] { refresh(); });

  auto *actions = new QHBoxLayout;
  actions->addWidget(addButton);
  actions->addWidget(deleteButton_);
  actions->addWidget(copyTokenButton_);
  actions->addWidget(copyCommandButton_);
  actions->addStretch(1);
  actions->addWidget(showValues_);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);

  auto *layout = new QVBoxLayout(this);
  layout->addWidget(note);
  layout->addWidget(list_, 1);
  layout->addLayout(actions);
  layout->addWidget(buttons);
  resize(640, 280);

  refreshTimer_ = new QTimer(this);
  refreshTimer_->setInterval(2000);
  connect(refreshTimer_, &QTimer::timeout, this, [this] { refresh(); });

  refresh();
}

void TokenDialog::showEvent(QShowEvent *event)
{
  QDialog::showEvent(event);
  refresh();
  refreshTimer_->start();
}

void TokenDialog::hideEvent(QHideEvent *event)
{
  refreshTimer_->stop();
  QDialog::hideEvent(event);
}

void TokenDialog::refresh(int selectId)
{
  if (selectId < 0) selectId = selectedId();
  const bool show = showValues_->isChecked();
  list_->clear();
  for (const TokenEntry& entry : bridge_.tokens()) {
    auto *item = new QTreeWidgetItem(list_);
    item->setData(ColumnLabel, kIdRole, entry.id);
    item->setData(ColumnToken, kValueRole, QString::fromStdString(entry.value));
    item->setText(ColumnLabel, QString::fromStdString(entry.label));
    item->setText(ColumnToken, show ? QString::fromStdString(entry.value) : maskedValue(entry.value));
    item->setText(ColumnCreated, formatTime(entry.created));
    item->setText(ColumnLastUsed, entry.lastUsed ? formatTime(*entry.lastUsed) : tr("never"));
    if (entry.id == selectId) item->setSelected(true);
  }
  updateButtons();
}

int TokenDialog::selectedId() const
{
  const auto selected = list_->selectedItems();
  return selected.isEmpty() ? -1 : selected.first()->data(ColumnLabel, kIdRole).toInt();
}

QString TokenDialog::selectedValue() const
{
  const auto selected = list_->selectedItems();
  return selected.isEmpty() ? QString() : selected.first()->data(ColumnToken, kValueRole).toString();
}

void TokenDialog::updateButtons()
{
  const bool hasSelection = !list_->selectedItems().isEmpty();
  deleteButton_->setEnabled(hasSelection);
  copyTokenButton_->setEnabled(hasSelection);
  copyCommandButton_->setEnabled(hasSelection);
}

void TokenDialog::addToken()
{
  bool ok = false;
  const QString label = QInputDialog::getText(this, tr("Add access token"), tr("Label (optional):"),
                                              QLineEdit::Normal, QString(), &ok);
  if (!ok) return;
  const TokenEntry entry = bridge_.addToken(label);
  refresh(entry.id);
}

void TokenDialog::deleteToken()
{
  const auto selected = list_->selectedItems();
  if (selected.isEmpty()) return;
  const int id = selectedId();
  QString text = tr("Delete the token %1? Clients using it are rejected from their next request.")
                   .arg(describe(selected.first()));
  if (list_->topLevelItemCount() == 1) {
    text += QStringLiteral("\n\n") +
            tr("This is the last token: no MCP client will be able to connect until you add a new one.");
  }
  if (QMessageBox::question(this, tr("Delete access token"), text) != QMessageBox::Yes) return;
  bridge_.removeToken(id);
  refresh();
}

}  // namespace mcp
