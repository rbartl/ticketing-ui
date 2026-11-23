#include "Theme.h"
#include "IssueListModel.h"

#include <QApplication>
#include <QFrame>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>
#include <QtMath>

namespace {

bool isDark()
{
    const QPalette pal = QApplication::palette();
    return pal.color(QPalette::Window).lightness() < pal.color(QPalette::WindowText).lightness();
}

// Away from the background: darker in light mode, lighter in dark mode.
QColor shade(const QColor &c, int factor)
{
    return isDark() ? c.lighter(100 + (factor - 100) * 2) : c.darker(factor);
}

QColor mix(const QColor &a, const QColor &b, qreal t)
{
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t, a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t);
}

QString hex(const QColor &c)
{
    return c.name(QColor::HexRgb);
}

} // namespace

namespace Theme {

QColor line() { return shade(QApplication::palette().color(QPalette::Window), 120); }
QColor softLine() { return shade(QApplication::palette().color(QPalette::Base), 107); }
QColor side()
{
    const QPalette pal = QApplication::palette();
    return mix(pal.color(QPalette::Window), pal.color(QPalette::Base), 0.55);
}
QColor muted()
{
    const QPalette pal = QApplication::palette();
    return mix(pal.color(QPalette::WindowText), pal.color(QPalette::Window), 0.42);
}

void apply(QApplication &app)
{
    const QPalette pal = app.palette();
    const QColor window = pal.color(QPalette::Window), base = pal.color(QPalette::Base);
    const QColor text = pal.color(QPalette::WindowText);
    const QColor accent = IssueStyle::accent();
    const QColor control = mix(base, window, 0.25);
    const QColor controlBorder = shade(window, 138);
    const QColor tab = shade(window, 104);

    QString qss = R"(
QMainWindow::separator, QSplitter::handle { background: LINE; }
QToolBar { background: WINDOW; border: 0; border-bottom: 1px solid LINE; padding: 5px 8px; spacing: 8px; }
QToolBar::separator { background: LINE; width: 1px; margin: 4px 4px; }
QStatusBar { background: WINDOW; border-top: 1px solid LINE; color: MUTED; }
QStatusBar QLabel { color: MUTED; padding: 0 6px; }
QStatusBar::item { border: 0; }

QPushButton, QToolBar > QToolButton, QToolButton#plain {
    background: CONTROL; color: TEXT; border: 1px solid CBORDER; border-radius: 3px;
    padding: 3px 12px; min-height: 20px;
}
QToolBar > QToolButton { padding: 2px 10px; }
QPushButton:hover, QToolBar > QToolButton:hover { background: HOVER; }
QPushButton:pressed, QToolBar > QToolButton:pressed { background: PRESSED; }
QPushButton:disabled { color: MUTED; background: WINDOW; border-color: LINE; }
QPushButton:focus, QToolBar > QToolButton:focus { border-color: ACCENT; }
QPushButton[primary="true"] { background: ACCENT; color: white; border-color: ACCENTDARK; font-weight: 600; }
QPushButton[primary="true"]:hover { background: ACCENTHOVER; }
QPushButton[primary="true"]:disabled { background: ACCENTSOFT; border-color: ACCENTSOFT; color: white; }
QPushButton[flat="true"] { background: transparent; border: 0; color: ACCENT; padding: 6px 12px; text-align: left; }
QPushButton[flat="true"]:hover { text-decoration: underline; }

QLineEdit, QPlainTextEdit, QComboBox {
    background: BASE; color: TEXT; border: 1px solid CBORDER; border-radius: 3px; padding: 3px 7px;
    selection-background-color: ACCENT; selection-color: white;
}
QComboBox { background: CONTROL; padding-right: 22px; min-height: 20px; }
QComboBox:disabled, QLineEdit:read-only { background: WINDOW; color: MUTED; }
QLineEdit:focus, QPlainTextEdit:focus, QComboBox:focus { border-color: ACCENT; }
QComboBox::drop-down { border: 0; width: 20px; }
QComboBox::down-arrow { image: url(:/icons/chevron-down.svg); width: 12px; height: 12px; }
QToolBar QComboBox, QToolBar QLineEdit, QToolBar > QToolButton { min-height: 22px; }

QTabWidget::pane { border: 1px solid LINE; top: -1px; background: SIDE; border-radius: 3px; }
QTabWidget#detailTabs::pane { background: BASE; border-left: 0; border-right: 0; border-bottom: 0; border-radius: 0; }
QTabBar::tab {
    background: TAB; color: TEXT; border: 1px solid LINE; border-bottom-color: LINE;
    border-top-left-radius: 3px; border-top-right-radius: 3px; padding: 5px 16px; margin-right: 2px;
}
QTabBar::tab:selected { background: SIDE; border-bottom-color: SIDE; font-weight: 600; }
QTabWidget#detailTabs QTabBar::tab:selected { background: BASE; border-bottom-color: BASE; }
QTabBar::tab:!selected:hover { background: HOVER; }
QTabWidget#detailTabs > QTabBar { margin-left: 18px; }

QHeaderView::section {
    background: SIDE; color: TEXT; border: 0; border-right: 1px solid SOFTLINE; border-bottom: 1px solid LINE;
    padding: 5px 8px;
}
QTableView { background: BASE; border: 0; selection-background-color: ACCENT; selection-color: white; }
QTableView::item { border-bottom: 1px solid SOFTLINE; padding-left: 4px; }
QTableView::item:selected { background: ACCENT; color: white; border-bottom-color: ACCENT; }

QListWidget#accounts { background: BASE; border: 1px solid CBORDER; border-radius: 3px; outline: 0; padding: 3px; }

QFrame#card { background: SIDE; border: 1px solid CARDLINE; border-radius: 3px; }
QFrame#card QLabel { background: transparent; }
QFrame#dialog { background: WINDOW; border: 1px solid CBORDER; border-radius: 6px; }
QWidget#dialogTitle { background: TAB; border-bottom: 1px solid LINE; border-top-left-radius: 6px; border-top-right-radius: 6px; }
QWidget#dialogFooter { border-top: 1px solid LINE; }
QToolButton#closeButton { border: 0; background: transparent; padding: 2px; }
QToolButton#closeButton:hover { background: HOVER; border-radius: 3px; }
AccountPanel { background: BACKDROP; }

QScrollArea#plain, QScrollArea#plain > QWidget > QWidget { background: BASE; border: 0; }
QLabel#muted { color: MUTED; }
QLabel#avatar { background: AVATAR; color: TEXT; border-radius: 4px; font-weight: 700; font-size: 11px; }
)";
    const QList<QPair<const char *, QColor>> vars{
        {"ACCENTDARK", accent.darker(125)},
        {"ACCENTHOVER", accent.lighter(110)},
        {"ACCENTSOFT", mix(accent, base, 0.55)},
        {"ACCENT", accent},
        {"CBORDER", controlBorder},
        {"CONTROL", control},
        {"HOVER", shade(control, 104)},
        {"PRESSED", shade(control, 110)},
        {"SOFTLINE", softLine()},
        {"CARDLINE", shade(side(), 115)},
        {"LINE", line()},
        {"WINDOW", window},
        {"BASE", base},
        {"TEXT", text},
        {"MUTED", muted()},
        {"SIDE", side()},
        {"TAB", tab},
        {"BACKDROP", shade(window, 107)},
        {"AVATAR", shade(side(), 110)},
    };
    for (const auto &[name, color] : vars)
        qss.replace(name, hex(color));
    app.setStyleSheet(qss);
}

QFrame *card()
{
    auto *f = new QFrame;
    f->setObjectName("card");
    return f;
}

void setPrimary(QWidget *button)
{
    button->setProperty("primary", true);
}

QIcon icon(Icon which)
{
    const qreal dpr = qApp->devicePixelRatio();
    QPixmap pm(QSize(16, 16) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor c = QApplication::palette().color(QPalette::WindowText);
    p.setPen(QPen(c, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    switch (which) {
    case Icon::Sync: {
        QPainterPath arc;
        arc.arcMoveTo(QRectF(2.5, 2.5, 11, 11), 60);
        arc.arcTo(QRectF(2.5, 2.5, 11, 11), 60, 300);
        p.drawPath(arc);
        p.drawPolyline(QPolygonF{QPointF(13.5, 1.5), QPointF(13.5, 5), QPointF(10, 5)});
        break;
    }
    case Icon::Search:
        p.drawEllipse(QRectF(2.5, 2.5, 8.5, 8.5));
        p.drawLine(QPointF(10, 10), QPointF(13.5, 13.5));
        break;
    case Icon::Gear: {
        const QPointF m(8, 8);
        for (int i = 0; i < 8; ++i) {
            const qreal a = qDegreesToRadians(i * 45.0);
            p.drawLine(m + QPointF(qCos(a), qSin(a)) * 5.0, m + QPointF(qCos(a), qSin(a)) * 7.0);
        }
        p.drawEllipse(m, 4.8, 4.8);
        p.drawEllipse(m, 1.8, 1.8);
        break;
    }
    case Icon::Plus:
        p.drawLine(QPointF(8, 3), QPointF(8, 13));
        p.drawLine(QPointF(3, 8), QPointF(13, 8));
        break;
    case Icon::Close:
        p.drawLine(QPointF(4, 4), QPointF(12, 12));
        p.drawLine(QPointF(12, 4), QPointF(4, 12));
        break;
    }
    p.end();
    return QIcon(pm);
}

} // namespace Theme

// AccountItemDelegate -----------------------------------------------------------------

QSize AccountItemDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &) const
{
    return QSize(option.rect.width(), QFontMetrics(option.font).height() * 2 + 14);
}

void AccountItemDelegate::paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    const QStringList parts = index.data(Qt::DisplayRole).toString().split('\n');
    const QString name = parts.value(0), tag = parts.value(1), host = parts.value(2);
    const bool selected = option.state & QStyle::State_Selected;
    const QColor fg = selected ? QColor(Qt::white) : option.palette.color(QPalette::Text);
    const QColor sub = selected ? QColor(Qt::white) : Theme::muted();

    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    const QRect r = option.rect.adjusted(1, 1, -1, -1);
    if (selected) {
        p->setPen(Qt::NoPen);
        p->setBrush(IssueStyle::accent());
        p->drawRoundedRect(r, 3, 3);
    }
    QFont bold = option.font;
    bold.setBold(true);
    QFont small = option.font;
    small.setPointSizeF(option.font.pointSizeF() * 0.88);
    const QRect inner = r.adjusted(8, 5, -8, -5);
    const int lineH = QFontMetrics(bold).height();

    // Tag chip at the right of the first line.
    int nameRight = inner.right();
    if (!tag.isEmpty()) {
        const int w = QFontMetrics(small).horizontalAdvance(tag) + 10;
        const QRectF chip(inner.right() - w, inner.top() + 1, w, lineH - 2);
        p->setPen(QPen(selected ? QColor(Qt::white) : Theme::line(), 1));
        p->setBrush(Qt::NoBrush);
        p->drawRoundedRect(chip.adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
        p->setFont(small);
        p->setPen(sub);
        p->drawText(chip, Qt::AlignCenter, tag);
        nameRight = int(chip.left()) - 6;
    }
    p->setFont(bold);
    p->setPen(fg);
    const QRect nameRect(inner.left(), inner.top(), nameRight - inner.left(), lineH);
    p->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter,
                QFontMetrics(bold).elidedText(name, Qt::ElideRight, nameRect.width()));
    p->setFont(small);
    p->setPen(sub);
    const QRect hostRect(inner.left(), inner.top() + lineH, inner.width(), inner.height() - lineH);
    p->drawText(hostRect, Qt::AlignLeft | Qt::AlignVCenter,
                QFontMetrics(small).elidedText(host, Qt::ElideRight, hostRect.width()));
    p->restore();
}
