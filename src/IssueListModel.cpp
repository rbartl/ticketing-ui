#include "IssueListModel.h"

#include <QApplication>
#include <QDateTime>
#include <QFontDatabase>
#include <QHash>
#include <QIcon>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QSettings>

void IssueListModel::setRows(QList<IssueRow> rows)
{
    beginResetModel();
    m_rows = std::move(rows);
    endResetModel();
}

int IssueListModel::rowOf(const QString &accountId, const QString &key) const
{
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows[i].key == key && m_rows[i].accountId == accountId)
            return i;
    return -1;
}

void IssueListModel::setRead(const QString &accountId, const QString &key)
{
    const int row = rowOf(accountId, key);
    if (row < 0 || !m_rows[row].unread)
        return;
    m_rows[row].unread = false;
    emit dataChanged(index(row, 0), index(row, ColumnCount - 1));
}

QString IssueListModel::columnName(int column)
{
    switch (column) {
    case Key: return "Key";
    case Priority: return "Prio";
    case Summary: return "Zusammenfassung";
    case Status: return "Status";
    case Assignee: return "Zugewiesen";
    case Account: return "Konto";
    case Updated: return "Geändert";
    case Type: return "Typ";
    case Reporter: return "Melder";
    case Project: return "Projekt";
    case Labels: return "Labels";
    case Created: return "Erstellt";
    }
    return {};
}

// "OPS-142" -> "OPS" + zero padded number, so OPS-9 sorts before OPS-10.
static QString naturalKey(const QString &key)
{
    const int dash = key.lastIndexOf('-');
    if (dash < 0)
        return key;
    return key.left(dash) + QChar(1) + key.mid(dash + 1).rightJustified(12, '0');
}

static int statusOrder(const QString &cat)
{
    if (cat == "new") return 0;
    if (cat == "indeterminate") return 1;
    if (cat == "done") return 2;
    return 3;
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
        case Priority: return {};
        case Summary: return r.summary;
        case Status: return r.status;
        case Assignee: return r.assignee.isEmpty() ? QStringLiteral("–") : r.assignee;
        case Account: return m_accountNames.value(r.accountId);
        case Updated: return IssueStyle::relativeTime(r.updated);
        case Type: return r.issueType;
        case Reporter: return r.reporter;
        case Project: return r.project;
        case Labels: return r.labels;
        case Created: return IssueStyle::relativeTime(r.created);
        }
        break;
    case Qt::DecorationRole:
        if (col == Key)
            return IssueStyle::keyIcon(r.issueType, r.unread);
        if (col == Type)
            return IssueStyle::typeIcon(r.issueType);
        if (col == Priority)
            return IssueStyle::priorityIcon(r.priority);
        break;
    case Qt::ToolTipRole:
        if (col == Key)
            return r.issueType + (r.unread ? QStringLiteral(" · ungelesen") : QString()) +
                   (r.cloud ? QStringLiteral(" · aus der Cloud (nicht lokal gespeichert)") : QString());
        if (col == Priority)
            return IssueStyle::priorityLabel(r.priority);
        if (col == Summary)
            return r.summary;
        if (col == Created)
            return IssueStyle::fullTime(r.created);
        if (col == Updated)
            return IssueStyle::fullTime(r.updated);
        break;
    case Qt::AccessibleTextRole:
        if (col == Priority)
            return IssueStyle::priorityLabel(r.priority);
        break;
    case Qt::ForegroundRole:
        if (r.cloud)
            return QApplication::palette().color(QPalette::PlaceholderText);
        break;
    case Qt::FontRole:
        if (col == Key) {
            QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
            f.setPointSizeF(QApplication::font().pointSizeF() * 0.92);
            f.setBold(r.unread);
            return f;
        }
        if (r.unread) {
            QFont f = QApplication::font();
            f.setBold(true);
            return f;
        }
        break;
    case StatusCatRole:
        return r.statusCat;
    case KeyRole:
        return r.key;
    case AccountRole:
        return r.accountId;
    case SortRole:
        switch (col) {
        case Key: return naturalKey(r.key);
        case Priority: return IssueStyle::priorityRank(r.priority);
        case Status: return QString::number(statusOrder(r.statusCat)) + r.status.toLower();
        case Updated: return r.updated;
        case Created: return r.created;
        case Account: return m_accountNames.value(r.accountId).toLower();
        default: return data(index, Qt::DisplayRole).toString().toLower();
        }
    }
    return {};
}

QVariant IssueListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
        return columnName(section);
    return {};
}

// Proxy -----------------------------------------------------------------------------

IssueProxyModel::IssueProxyModel(QObject *parent) : QSortFilterProxyModel(parent)
{
    setSortRole(IssueListModel::SortRole);
    setSortLocaleAware(true);
}

template <typename F> void IssueProxyModel::changeFilter(F change)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
    change();
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
    change();
    invalidateRowsFilter();
#endif
}

void IssueProxyModel::setHideDone(bool on)
{
    if (on != m_hideDone)
        changeFilter([&] { m_hideDone = on; });
}

bool IssueProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    const QModelIndex i = sourceModel()->index(sourceRow, 0, sourceParent);
    return !m_hideDone || i.data(IssueListModel::StatusCatRole).toString() != "done";
}

// Style -----------------------------------------------------------------------------

namespace IssueStyle {

QColor accent()
{
    static const QColor c(QSettings().value("ui/accent", "#2b6cb0").toString());
    return c.isValid() ? c : QColor(0x2b, 0x6c, 0xb0);
}

QColor statusColor(const QString &cat, const QString &status)
{
    if (cat == "done") return QColor(0x2f, 0x85, 0x5a);
    const QString s = status.toLower();
    if (s.contains("wart") || s.contains("wait") || s.contains("hold"))
        return QColor(0xa1, 0x6a, 0x07);  // waiting for someone else
    if (cat == "indeterminate") return QColor(0x2b, 0x6c, 0xb0);
    return QColor(0x71, 0x7a, 0x86);  // new / unknown
}

QColor typeColor(const QString &type)
{
    const QString t = type.toLower();
    if (t.contains("bug") || t.contains("defect") || t.contains("fehler")) return QColor(0xc5, 0x30, 0x30);
    if (t.contains("story")) return QColor(0x2f, 0x85, 0x5a);
    if (t.contains("epic")) return QColor(0x80, 0x5a, 0xd5);
    if (t.contains("sub") || t.contains("unter")) return QColor(0x31, 0x82, 0xce);
    if (t.contains("task") || t.contains("aufgabe")) return QColor(0x2b, 0x6c, 0xb0);
    return QColor(0x71, 0x7a, 0x86);
}

static QPixmap canvas(int w, qreal dpr)
{
    QPixmap pm(QSize(w, 16) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    return pm;
}

static void drawTile(QPainter &p, qreal x, const QString &type)
{
    p.setPen(Qt::NoPen);
    p.setBrush(typeColor(type));
    p.drawRoundedRect(QRectF(x + 1, 1, 14, 14), 3, 3);
    QFont f = QApplication::font();
    f.setPixelSize(10);
    f.setBold(true);
    p.setFont(f);
    p.setPen(Qt::white);
    p.drawText(QRectF(x + 1, 1, 14, 14), Qt::AlignCenter, type.left(1).toUpper());
}

QIcon typeIcon(const QString &type)
{
    static QHash<QString, QIcon> cache;
    if (auto it = cache.constFind(type); it != cache.constEnd())
        return *it;
    QPixmap pm = canvas(16, qApp->devicePixelRatio());
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    drawTile(p, 0, type);
    p.end();
    return cache[type] = QIcon(pm);
}

QIcon keyIcon(const QString &type, bool unread)
{
    static QHash<QString, QIcon> cache;
    const QString id = type + (unread ? "\n1" : "\n0");
    if (auto it = cache.constFind(id); it != cache.constEnd())
        return *it;
    QPixmap pm = canvas(26, qApp->devicePixelRatio());
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    if (unread) {
        p.setPen(Qt::NoPen);
        p.setBrush(accent());
        p.drawEllipse(QRectF(1, 5, 6, 6));
    }
    drawTile(p, 10, type);
    p.end();
    return cache[id] = QIcon(pm);
}

int priorityRank(const QString &priority)
{
    const QString p = priority.toLower();
    if (p.contains("highest") || p.contains("blocker") || p.contains("höchst") || p.contains("kritisch")) return 0;
    if (p.contains("critical") || p.contains("high") || p.contains("major") || p.contains("hoch")) return 1;
    if (p.contains("lowest") || p.contains("trivial") || p.contains("niedrigst")) return 4;
    if (p.contains("low") || p.contains("minor") || p.contains("niedrig")) return 3;
    return 2;
}

QString priorityLabel(const QString &priority)
{
    return priority.isEmpty() ? QStringLiteral("Keine Priorität") : "Priorität: " + priority;
}

QIcon priorityIcon(const QString &priority)
{
    static QHash<QString, QIcon> cache;
    if (auto it = cache.constFind(priority); it != cache.constEnd())
        return *it;
    const int rank = priorityRank(priority);
    QColor c(0x9a, 0x67, 0x00);  // medium
    if (rank <= 1) c = QColor(0xc5, 0x30, 0x30);
    if (rank >= 3) c = QColor(0x2b, 0x6c, 0xb0);
    auto draw = [rank](const QColor &color) {
        QPixmap pm = canvas(16, qApp->devicePixelRatio());
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(color, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        auto chevron = [&p](qreal tip, qreal base) {
            p.drawPolyline(QPolygonF{QPointF(3.5, base), QPointF(8, tip), QPointF(12.5, base)});
        };
        if (rank <= 1) {  // high: double chevron up
            chevron(2.5, 7);
            chevron(7.5, 12);
        } else if (rank == 2) {  // medium: two bars
            p.drawLine(QPointF(3.5, 6), QPointF(12.5, 6));
            p.drawLine(QPointF(3.5, 10), QPointF(12.5, 10));
        } else {  // low: chevron down (lowest: two)
            chevron(rank == 4 ? 8.5 : 10.5, rank == 4 ? 4 : 6);
            if (rank == 4)
                chevron(13.5, 9);
        }
        p.end();
        return pm;
    };
    QIcon icon(draw(c));
    icon.addPixmap(draw(Qt::white), QIcon::Selected);  // readable on the selection color
    return cache[priority] = icon;
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
    // Background/selection as usual, then square + text. The color only adds to the text.
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);
    const QString text = opt.text;
    opt.text.clear();
    QApplication::style()->drawControl(QStyle::CE_ItemViewItem, &opt, p, opt.widget);
    if (text.isEmpty())
        return;

    const bool selected = opt.state & QStyle::State_Selected;
    const QColor c = IssueStyle::statusColor(index.data(IssueListModel::StatusCatRole).toString(), text);
    const QRect r = opt.rect.adjusted(8, 0, -4, 0);
    const QRectF square(r.left(), r.center().y() - 3.5, 8, 8);

    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    p->setPen(selected ? QPen(opt.palette.color(QPalette::HighlightedText), 1) : Qt::NoPen);
    p->setBrush(c);
    p->drawRoundedRect(square, 1.5, 1.5);
    p->setFont(opt.font);
    p->setPen(opt.palette.color(selected ? QPalette::HighlightedText : QPalette::Text));
    const QRect textRect = r.adjusted(15, 0, 0, 0);
    p->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                QFontMetrics(opt.font).elidedText(text, Qt::ElideRight, textRect.width()));
    p->restore();
}
