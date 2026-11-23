#pragma once
#include <QColor>
#include <QIcon>
#include <QStyledItemDelegate>

class QApplication;
class QFrame;
class QWidget;

// Look of the app: Fusion + one style sheet. Colors derive from the system palette (light and dark),
// plus the accent color (IssueStyle::accent).
namespace Theme {
void apply(QApplication &app);

QColor line();      // separators between areas
QColor softLine();  // row separators
QColor side();      // sidebar, cards, table header
QColor muted();     // secondary text

enum class Icon { Sync, Search, Gear, Plus, Close };
QIcon icon(Icon which);  // thin stroke icons in the text color

QFrame *card();  // bordered box (object name "card")
void setPrimary(QWidget *button);  // filled accent button
}

// Account list: name, backend tag, host below (DisplayRole "name\ntag\nhost").
class AccountItemDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
};
