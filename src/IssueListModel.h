#pragma once
#include "Database.h"

#include <QAbstractTableModel>
#include <QColor>
#include <QHash>
#include <QSortFilterProxyModel>
#include <QStyledItemDelegate>

class IssueListModel : public QAbstractTableModel {
    Q_OBJECT
public:
    // Order = default column order. Which ones are visible is a user setting (header context menu).
    enum Column { Key, Priority, Summary, Status, Assignee, Account, Updated, Type, Reporter, Project, Labels, Created,
                  ColumnCount };
    enum Role { StatusCatRole = Qt::UserRole + 1, SortRole, KeyRole, AccountRole };
    static QString columnName(int column);
    static QList<int> defaultHidden() { return {Type, Reporter, Project, Labels, Created}; }

    using QAbstractTableModel::QAbstractTableModel;

    void setRows(QList<IssueRow> rows);
    void setAccountNames(const QHash<QString, QString> &names) { m_accountNames = names; }
    const IssueRow *rowAt(int row) const { return row >= 0 && row < m_rows.size() ? &m_rows[row] : nullptr; }
    int rowOf(const QString &accountId, const QString &key) const;
    void setRead(const QString &accountId, const QString &key);  // after opening it

    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : int(m_rows.size()); }
    int columnCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : ColumnCount; }
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    QList<IssueRow> m_rows;
    QHash<QString, QString> m_accountNames;
};

// Sorting (header click) and "Erledigte ausblenden".
class IssueProxyModel : public QSortFilterProxyModel {
    Q_OBJECT
public:
    explicit IssueProxyModel(QObject *parent = nullptr);
    void setHideDone(bool on);
    bool hideDone() const { return m_hideDone; }

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    template <typename F> void changeFilter(F change);
    bool m_hideDone = false;
};

// Shared look of issues in list and detail view.
namespace IssueStyle {
QColor accent();                                  // selection, primary buttons; setting "ui/accent"
QColor statusColor(const QString &statusCategory, const QString &status = {});
QColor typeColor(const QString &issueType);
QIcon typeIcon(const QString &issueType);        // 16 px tile with the initial
QIcon keyIcon(const QString &issueType, bool unread);  // dot slot (filled when unread) + tile, same width for all
QIcon priorityIcon(const QString &priority);      // double chevron up / two bars / chevron down
QString priorityLabel(const QString &priority);   // tooltip
int priorityRank(const QString &priority);        // 0 = most urgent
QString relativeTime(const QString &utcIso);      // "09:40", "gestern 14:12", "Mo 10:23", "21. Okt.", "12.05.23"
QString fullTime(const QString &utcIso);          // "19.11.2026 09:40"
}

// Paints the status column as a small colored square plus the text.
class IssueDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
};
