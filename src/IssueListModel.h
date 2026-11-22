#pragma once
#include "Database.h"

#include <QAbstractTableModel>
#include <QColor>
#include <QStyledItemDelegate>

class IssueListModel : public QAbstractTableModel {
    Q_OBJECT
public:
    // Order = default column order. Which ones are visible is a user setting (header context menu).
    enum Column { Key, Type, Summary, Status, Priority, Assignee, Reporter, Project, Labels, Created, Updated,
                  ColumnCount };
    static QString columnName(int column);
    static QList<int> defaultHidden() { return {Type, Reporter, Project, Labels, Created}; }

    using QAbstractTableModel::QAbstractTableModel;

    void setRows(QList<IssueRow> rows);
    QString keyAt(int row) const;
    int rowOf(const QString &key) const;

    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : int(m_rows.size()); }
    int columnCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : ColumnCount; }
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    QList<IssueRow> m_rows;
};

// Shared look of issues in list and detail view.
namespace IssueStyle {
QColor statusColor(const QString &statusCategory);  // pill background
QColor typeColor(const QString &issueType);
QIcon typeIcon(const QString &issueType);       // small rounded square with the initial
QIcon priorityIcon(const QString &priority);    // arrow up / equal / down
QString relativeTime(const QString &utcIso);    // "09:40", "gestern 14:12", "Mo 10:23", "21. Okt.", "12.05.23"
QString fullTime(const QString &utcIso);        // "19.11.2026 09:40"
}

// Paints the status column as a pill.
class IssueDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
};
