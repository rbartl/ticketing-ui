#include "IssueListModel.h"

#include <QColor>
#include <QDateTime>

void IssueListModel::setRows(QList<IssueRow> rows)
{
    beginResetModel();
    m_rows = std::move(rows);
    endResetModel();
}

QString IssueListModel::keyAt(int row) const
{
    return row >= 0 && row < m_rows.size() ? m_rows[row].key : QString();
}

int IssueListModel::rowOf(const QString &key) const
{
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows[i].key == key)
            return i;
    return -1;
}

QVariant IssueListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return {};
    const IssueRow &r = m_rows[index.row()];

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case Key: return r.cloud ? QString::fromUtf8("☁ ") + r.key : r.key;
        case Type: return r.issueType;
        case Summary: return r.summary;
        case Status: return r.status;
        case Assignee: return r.assignee;
        case Updated:
            return QDateTime::fromString(r.updated, Qt::ISODateWithMs).toLocalTime().toString("dd.MM.yy HH:mm");
        }
    } else if (role == Qt::ForegroundRole) {
        if (index.column() == Status) {
            if (r.statusCat == "done") return QColor(0x2e, 0x8b, 0x57);
            if (r.statusCat == "indeterminate") return QColor(0x1e, 0x6f, 0xd9);
        }
        if (r.cloud)
            return QColor(Qt::gray);
    } else if (role == Qt::ToolTipRole && r.cloud) {
        return QStringLiteral("Aus der Cloud (nicht im lokalen Sync-Bereich)");
    }
    return {};
}

QVariant IssueListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
    case Key: return "Key";
    case Type: return "Typ";
    case Summary: return "Zusammenfassung";
    case Status: return "Status";
    case Assignee: return "Zugewiesen";
    case Updated: return "Geändert";
    }
    return {};
}
