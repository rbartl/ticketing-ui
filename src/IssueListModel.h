#pragma once
#include "Database.h"

#include <QAbstractTableModel>

class IssueListModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { Key, Type, Summary, Status, Assignee, Updated, ColumnCount };

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
