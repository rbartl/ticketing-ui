#include "IssueListModel.h"

#include <QApplication>
#include <QDateTime>
#include <QHash>
#include <QIcon>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>

static constexpr int kStatusCatRole = Qt::UserRole + 1;

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

QString IssueListModel::columnName(int column)
{
    switch (column) {
    case Key: return "Key";
    case Type: return "Typ";
    case Summary: return "Zusammenfassung";
    case Status: return "Status";
    case Priority: return "Priorität";
    case Assignee: return "Zugewiesen";
    case Reporter: return "Melder";
    case Project: return "Projekt";
    case Labels: return "Labels";
    case Created: return "Erstellt";
    case Updated: return "Geändert";
    }
    return {};
}

QVariant IssueListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return {};
    const IssueRow &r = m_rows[index.row()];
    const int col = index.column();

    switch (role) {
    case Qt::DisplayRole:
        switch (col) {
        case Key: return r.cloud ? QString::fromUtf8("☁ ") + r.key : r.key;
        case Type: return r.issueType;
        case Summary: return r.summary;
        case Status: return r.status;
        case Priority: return r.priority;
        case Assignee: return r.assignee;
        case Reporter: return r.reporter;
        case Project: return r.project;
        case Labels: return r.labels;
        case Created: return IssueStyle::relativeTime(r.created);
        case Updated: return IssueStyle::relativeTime(r.updated);
        }
        break;
    case Qt::DecorationRole:
        if (col == Key || col == Type)
            return IssueStyle::typeIcon(r.issueType);
        if (col == Priority)
            return IssueStyle::priorityIcon(r.priority);
        break;
    case Qt::ToolTipRole:
        if (col == Key)
            return r.issueType + (r.cloud ? QStringLiteral(" · aus der Cloud (nicht lokal gespeichert)") : QString());
        if (col == Summary)
            return r.summary;
        if (col == Created)
            return IssueStyle::fullTime(r.created);
        if (col == Updated)
            return IssueStyle::fullTime(r.updated);
        break;
    case Qt::ForegroundRole:
        if (r.cloud || col == Updated || col == Created)
            return QApplication::palette().color(QPalette::PlaceholderText);
        break;
    case Qt::FontRole:
        if (col == Key) {
            QFont f = QApplication::font();
            f.setWeight(QFont::DemiBold);
            return f;
        }
        break;
    case kStatusCatRole:
        return r.statusCat;
    }
    return {};
}

QVariant IssueListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
        return columnName(section);
    return {};
}

// Style -----------------------------------------------------------------------------

namespace IssueStyle {

QColor statusColor(const QString &cat)
{
    if (cat == "done") return QColor(0x1f, 0x84, 0x5a);
    if (cat == "indeterminate") return QColor(0x0c, 0x66, 0xe4);
    return QColor(0x6b, 0x77, 0x8c);  // new / unknown
}

QColor typeColor(const QString &type)
{
    const QString t = type.toLower();
    if (t.contains("bug") || t.contains("defect") || t.contains("fehler")) return QColor(0xe5, 0x49, 0x3a);
    if (t.contains("story")) return QColor(0x22, 0xa0, 0x6b);
    if (t.contains("epic")) return QColor(0x8f, 0x5b, 0xd6);
    if (t.contains("sub") || t.contains("unter")) return QColor(0x4b, 0xad, 0xe8);
    if (t.contains("task") || t.contains("aufgabe")) return QColor(0x2f, 0x7c, 0xf6);
    return QColor(0x8a, 0x94, 0xa6);
}

static QPixmap canvas(qreal dpr)
{
    QPixmap pm(QSize(16, 16) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    return pm;
}

QIcon typeIcon(const QString &type)
{
    static QHash<QString, QIcon> cache;
    if (auto it = cache.constFind(type); it != cache.constEnd())
        return *it;
    QPixmap pm = canvas(qApp->devicePixelRatio());
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(typeColor(type));
    p.drawRoundedRect(QRectF(1, 1, 14, 14), 3.5, 3.5);
    QFont f = QApplication::font();
    f.setPixelSize(10);
    f.setBold(true);
    p.setFont(f);
    p.setPen(Qt::white);
    p.drawText(QRectF(1, 1, 14, 14), Qt::AlignCenter, type.left(1).toUpper());
    p.end();
    return cache[type] = QIcon(pm);
}

QIcon priorityIcon(const QString &priority)
{
    static QHash<QString, QIcon> cache;
    if (auto it = cache.constFind(priority); it != cache.constEnd())
        return *it;
    const QString pr = priority.toLower();
    int dir = 0;  // 1 up, -1 down, 0 equal
    QColor c(0xe0, 0x8a, 0x00);
    if (pr.contains("highest") || pr.contains("blocker") || pr.contains("critical") || pr.contains("high") ||
        pr.contains("major") || pr.contains("hoch")) {
        dir = 1;
        c = QColor(0xe5, 0x49, 0x3a);
    } else if (pr.contains("low") || pr.contains("minor") || pr.contains("trivial") || pr.contains("niedrig")) {
        dir = -1;
        c = QColor(0x2f, 0x7c, 0xf6);
    }
    QPixmap pm = canvas(qApp->devicePixelRatio());
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(c, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    if (dir == 0) {
        p.drawLine(QPointF(3.5, 6), QPointF(12.5, 6));
        p.drawLine(QPointF(3.5, 10), QPointF(12.5, 10));
    } else {
        const qreal tip = dir > 0 ? 4 : 12, base = dir > 0 ? 10 : 6;
        p.drawPolyline(QPolygonF{QPointF(3.5, base), QPointF(8, tip), QPointF(12.5, base)});
        if (pr.contains("highest") || pr.contains("blocker") || pr.contains("lowest"))
            p.drawPolyline(QPolygonF{QPointF(3.5, base + 4 * dir), QPointF(8, tip + 4 * dir), QPointF(12.5, base + 4 * dir)});
    }
    p.end();
    return cache[priority] = QIcon(pm);
}

QString relativeTime(const QString &utcIso)
{
    const QDateTime dt = QDateTime::fromString(utcIso, Qt::ISODateWithMs).toLocalTime();
    if (!dt.isValid())
        return {};
    const QDate today = QDate::currentDate();
    const qint64 days = dt.date().daysTo(today);
    const QLocale de(QLocale::German);
    if (days == 0) return dt.toString("HH:mm");
    if (days == 1) return "gestern " + dt.toString("HH:mm");
    if (days > 1 && days < 7) return de.toString(dt, "ddd HH:mm");
    if (dt.date().year() == today.year()) return de.toString(dt.date(), "d. MMM");
    return dt.toString("dd.MM.yy");
}

QString fullTime(const QString &utcIso)
{
    const QDateTime dt = QDateTime::fromString(utcIso, Qt::ISODateWithMs);
    return dt.isValid() ? dt.toLocalTime().toString("dd.MM.yyyy HH:mm") : QString();
}

} // namespace IssueStyle

// Delegate --------------------------------------------------------------------------

void IssueDelegate::paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    if (index.column() != IssueListModel::Status) {
        QStyledItemDelegate::paint(p, option, index);
        return;
    }
    // Background/selection as usual, then the pill instead of the text.
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);
    const QString text = opt.text;
    opt.text.clear();
    QApplication::style()->drawControl(QStyle::CE_ItemViewItem, &opt, p, opt.widget);
    if (text.isEmpty())
        return;

    QFont f = opt.font;
    f.setPointSizeF(f.pointSizeF() * 0.85);
    f.setWeight(QFont::DemiBold);
    const QFontMetrics fm(f);
    const QString label = fm.elidedText(text.toUpper(), Qt::ElideRight, opt.rect.width() - 20);
    const int h = fm.height() + 4;
    const QRectF pill(opt.rect.left() + 6, opt.rect.center().y() - h / 2.0 + 0.5, fm.horizontalAdvance(label) + 14, h);
    const QColor c = IssueStyle::statusColor(index.data(Qt::UserRole + 1).toString());

    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    p->setPen(Qt::NoPen);
    const bool selected = opt.state & QStyle::State_Selected;
    p->setBrush(selected ? QColor(255, 255, 255, 60) : QColor(c.red(), c.green(), c.blue(), 38));
    p->drawRoundedRect(pill, h / 2.0, h / 2.0);
    p->setFont(f);
    p->setPen(selected ? opt.palette.color(QPalette::HighlightedText) : c);
    p->drawText(pill, Qt::AlignCenter, label);
    p->restore();
}
