#include "IssueView.h"
#include "AccountContext.h"
#include "Backend.h"
#include "IssueListModel.h"
#include "Theme.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QFontDatabase>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>

enum Tab { DetailsTab, CommentsTab, HistoryTab };

static constexpr int kLabelWidth = 120;  // + spacing = 132 px label column

// Scrollable white page holding `content`.
static QScrollArea *page(QWidget *content)
{
    auto *s = new QScrollArea;
    s->setObjectName("plain");
    s->setWidgetResizable(true);
    s->setFrameShape(QFrame::NoFrame);
    s->setWidget(content);
    return s;
}

static QLabel *label(const QString &text, const char *objectName = nullptr)
{
    auto *l = new QLabel(text);
    if (objectName)
        l->setObjectName(objectName);
    return l;
}

static QLabel *richText(const QString &html)
{
    auto *l = new QLabel(html);
    l->setTextFormat(Qt::RichText);
    l->setWordWrap(true);
    l->setOpenExternalLinks(true);
    l->setTextInteractionFlags(Qt::TextBrowserInteraction);
    l->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    return l;
}

static void clearLayout(QLayout *layout)
{
    while (QLayoutItem *item = layout->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        else if (QLayout *l = item->layout())
            clearLayout(l);
        delete item;
    }
}

static QString initials(const QString &name)
{
    QString s;
    for (const QString &part : name.split(' ', Qt::SkipEmptyParts))
        if (s.size() < 2 && part[0].isLetter())
            s += part[0].toUpper();
    return s.isEmpty() ? QStringLiteral("?") : s;
}

IssueView::IssueView(QWidget *parent) : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground);
    setAutoFillBackground(true);
    setBackgroundRole(QPalette::Base);

    m_empty = label("Kein Ticket ausgewählt.", "muted");
    m_empty->setAlignment(Qt::AlignCenter);

    // Header ------------------------------------------------------------------------
    m_typeIcon = new QLabel;
    m_keyLabel = new QLabel;
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setBold(true);
    m_keyLabel->setFont(mono);
    m_keyLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_account = label({}, "muted");
    m_link = new QLabel;
    m_link->setTextInteractionFlags(Qt::LinksAccessibleByMouse | Qt::LinksAccessibleByKeyboard);
    connect(m_link, &QLabel::linkActivated, this, &IssueView::openInBrowser);
    auto *meta = new QHBoxLayout;
    meta->setSpacing(8);
    meta->addWidget(m_typeIcon);
    meta->addWidget(m_keyLabel);
    meta->addWidget(m_account);
    meta->addStretch();
    meta->addWidget(m_link);

    m_title = new QLabel;
    m_title->setWordWrap(true);
    m_title->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QFont tf = m_title->font();
    tf.setPointSizeF(tf.pointSizeF() * 1.3);
    tf.setBold(true);
    m_title->setFont(tf);

    // Actions -----------------------------------------------------------------------
    m_status = new QComboBox;
    m_status->setMinimumWidth(150);
    m_status->setToolTip("Status ändern");
    auto *statusLabel = new QLabel("Status");
    statusLabel->setBuddy(m_status);
    m_assign = new QPushButton;
    m_comment = new QPushButton("Kommentieren");
    auto *actions = new QHBoxLayout;
    actions->setSpacing(8);
    actions->addWidget(statusLabel);
    actions->addWidget(m_status);
    actions->addWidget(m_assign);
    actions->addWidget(m_comment);
    actions->addStretch();

    // Tab Details: property card + description ----------------------------------------
    auto *fieldsCard = Theme::card();
    m_fields = new QFormLayout(fieldsCard);
    m_fields->setContentsMargins(14, 10, 14, 10);
    m_fields->setHorizontalSpacing(12);
    m_fields->setVerticalSpacing(5);
    m_fields->setLabelAlignment(Qt::AlignRight | Qt::AlignTop);
    m_description = richText({});
    auto *details = new QWidget;
    auto *dl = new QVBoxLayout(details);
    dl->setContentsMargins(18, 16, 18, 16);
    dl->setSpacing(8);
    dl->addWidget(fieldsCard);
    dl->addSpacing(10);
    QLabel *descTitle = label("Beschreibung");
    QFont bold = descTitle->font();
    bold.setBold(true);
    descTitle->setFont(bold);
    dl->addWidget(descTitle);
    dl->addWidget(m_description);
    dl->addStretch();
    m_detailsPage = page(details);

    // Tab Kommentare: list + composer ---------------------------------------------------
    auto *list = new QWidget;
    m_commentList = new QVBoxLayout(list);
    m_commentList->setContentsMargins(18, 14, 18, 8);
    m_commentList->setSpacing(14);
    m_commentsScroll = page(list);
    m_draft = new QPlainTextEdit;
    m_draft->setPlaceholderText("Kommentar schreiben …");
    m_draft->setFixedHeight(m_draft->fontMetrics().lineSpacing() * 4 + 14);
    m_draft->setTabChangesFocus(true);
    m_draft->installEventFilter(this);  // Ctrl+Return sends (and wins over the window's Ctrl+Return)
    m_send = new QPushButton("Kommentar senden");
    Theme::setPrimary(m_send);
    m_send->setEnabled(false);
    auto *hint = label(QKeySequence("Ctrl+Return").toString(QKeySequence::NativeText), "muted");
    auto *separator = new QFrame;
    separator->setFrameShape(QFrame::HLine);
    separator->setStyleSheet(QString("color: %1").arg(Theme::softLine().name()));
    auto *commentPage = new QWidget;
    commentPage->setAutoFillBackground(true);
    commentPage->setBackgroundRole(QPalette::Base);
    auto *cl = new QVBoxLayout(commentPage);
    cl->setContentsMargins(0, 0, 0, 14);
    cl->setSpacing(10);
    cl->addWidget(m_commentsScroll, 1);
    auto *composer = new QVBoxLayout;
    composer->setContentsMargins(18, 0, 18, 0);
    composer->setSpacing(8);
    composer->addWidget(separator);
    composer->addWidget(m_draft);
    auto *sendRow = new QHBoxLayout;
    sendRow->setSpacing(10);
    sendRow->addWidget(m_send);
    sendRow->addWidget(hint);
    sendRow->addStretch();
    composer->addLayout(sendRow);
    cl->addLayout(composer);

    // Tab Verlauf ---------------------------------------------------------------------
    auto *history = new QWidget;
    auto *hl = new QVBoxLayout(history);
    hl->setContentsMargins(18, 14, 18, 14);
    m_historyGrid = new QGridLayout;
    m_historyGrid->setHorizontalSpacing(12);
    m_historyGrid->setVerticalSpacing(12);
    m_historyGrid->setColumnMinimumWidth(0, kLabelWidth);
    m_historyGrid->setColumnStretch(1, 1);
    hl->addLayout(m_historyGrid);
    hl->addStretch();
    m_historyPage = page(history);

    m_tabs = new QTabWidget;
    m_tabs->setObjectName("detailTabs");
    m_tabs->addTab(m_detailsPage, "Details");
    m_tabs->addTab(commentPage, "Kommentare");
    m_tabs->addTab(m_historyPage, "Verlauf");

    auto *head = new QVBoxLayout;
    head->setContentsMargins(18, 14, 18, 12);
    head->setSpacing(8);
    head->addLayout(meta);
    head->addWidget(m_title);
    head->addSpacing(2);
    head->addLayout(actions);
    auto *content = new QWidget;
    auto *v = new QVBoxLayout(content);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    v->addLayout(head);
    v->addWidget(m_tabs, 1);

    m_stack = new QStackedWidget;
    m_stack->addWidget(m_empty);
    m_stack->addWidget(content);
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(m_stack);

    // Wiring ------------------------------------------------------------------------
    m_refreshTimer.setSingleShot(true);
    connect(&m_refreshTimer, &QTimer::timeout, this, &IssueView::backgroundRefresh);
    connect(m_status, &QComboBox::activated, this, &IssueView::applyTransition);
    connect(m_assign, &QPushButton::clicked, this, &IssueView::assignToMe);
    connect(m_comment, &QPushButton::clicked, this, &IssueView::startComment);
    connect(m_send, &QPushButton::clicked, this, &IssueView::sendComment);
    connect(m_draft, &QPlainTextEdit::textChanged, this, &IssueView::updateActions);
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int tab) {
        if (tab == HistoryTab)
            loadHistory();
    });
}

bool IssueView::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_draft && (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress)) {
        auto *k = static_cast<QKeyEvent *>(event);
        const bool send = (k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) &&
                          (k->modifiers() & Qt::ControlModifier);
        if (send) {
            event->accept();
            if (event->type() == QEvent::KeyPress)
                sendComment();
            return event->type() == QEvent::KeyPress;
        }
    }
    return QWidget::eventFilter(watched, event);
}

// Showing ---------------------------------------------------------------------------

void IssueView::showIssue(AccountContext *ctx, const QString &key)
{
    if (ctx == m_ctx && key == m_key)
        return;
    m_ctx = ctx;
    m_key = key;
    m_draft->clear();
    m_historyKey.clear();
    clearLayout(m_historyGrid);
    m_status->clear();
    if (!ctx || key.isEmpty()) {
        m_refreshTimer.stop();
        m_stack->setCurrentWidget(m_empty);
        return;
    }
    m_stack->setCurrentIndex(1);
    render();
    for (QWidget *w : {m_detailsPage, m_commentsScroll, m_historyPage})
        static_cast<QScrollArea *>(w)->verticalScrollBar()->setValue(0);
    m_refreshTimer.start(ctx->db().issue(key) ? 250 : 0);
}

void IssueView::refresh()
{
    if (!m_ctx || m_key.isEmpty())
        return;
    render();
    // Changed while being looked at -> it has been seen.
    m_ctx->db().markRead(m_key);
    emit read(m_ctx, m_key);
}

void IssueView::backgroundRefresh()
{
    AccountContext *ctx = m_ctx;
    const QString key = m_key;
    if (!ctx || key.isEmpty())
        return;
    ctx->db().touchViewed(key);
    ctx->db().markRead(key);
    emit read(ctx, key);

    const auto d = ctx->db().issue(key);
    if (!d || !d->inScope) {  // not synced -> fetch into cache / refresh cache
        ctx->fetchIssue(key, [this, ctx, key](bool ok, const QString &err) {
            if (ctx != m_ctx || key != m_key)
                return;
            if (ok) {
                ctx->db().markRead(key);
                render();
            } else if (!ctx->db().issue(key)) {
                m_title->setText(key + " konnte nicht geladen werden: " + err);
            }
        });
    }
    ctx->refreshComments(key, [this, ctx, key](bool changed) {
        if (changed && ctx == m_ctx && key == m_key)
            renderComments();
    });
    loadTransitions();
    if (m_tabs->currentIndex() == HistoryTab)
        loadHistory();
}

void IssueView::render()
{
    const auto d = m_ctx->db().issue(m_key);
    const Backend &backend = m_ctx->backend();
    m_keyLabel->setText(m_key);
    m_account->setText("·  " + m_ctx->account().name);
    m_link->setText(QString("<a href=\"open\" style=\"color:%1; text-decoration:none\">%2 ↗</a>")
                        .arg(IssueStyle::accent().name(), backend.type().openLabel.toHtmlEscaped()));
    while (m_fields->rowCount() > 0)
        m_fields->removeRow(0);
    if (!d) {
        m_typeIcon->clear();
        m_title->setText("Lade aus der Cloud …");
        m_description->clear();
        renderComments();
        updateActions();
        return;
    }
    m_typeIcon->setPixmap(IssueStyle::typeIcon(d->issueType).pixmap(16, 16));
    m_typeIcon->setToolTip(d->issueType);
    m_title->setText(d->summary);
    if (m_status->count() == 0 || m_status->itemData(0).toString() != d->status) {
        m_status->clear();
        m_status->addItem(d->status, d->status);  // current; transitions follow when loaded
    }
    m_assign->setText(backend.assignToMeLabel());

    auto addRow = [this](const QString &name, const QString &value) {
        QLabel *l = label(name, "muted");
        l->setMinimumWidth(kLabelWidth);
        l->setAlignment(Qt::AlignRight | Qt::AlignTop);
        QLabel *v = label(value.isEmpty() ? QStringLiteral("–") : value);
        v->setWordWrap(true);
        v->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_fields->addRow(l, v);
    };
    for (const DetailField &f : backend.detailFields(*d)) {
        QString value = f.value;
        if (f.kind == DetailField::Time)
            value = IssueStyle::fullTime(f.value);
        else if (f.kind == DetailField::Tags)
            value = f.value.split(' ', Qt::SkipEmptyParts).join(", ");
        addRow(f.label, value);
    }
    if (!d->inScope)
        addRow("Lokal", "Cache – nicht im Bereich „Lokal speichern“");
    m_description->setText(d->descriptionHtml.isEmpty()
                               ? QString("<span style='color:%1'>Keine Beschreibung</span>").arg(Theme::muted().name())
                               : d->descriptionHtml);

    renderComments();
    updateActions();
}

void IssueView::renderComments()
{
    if (!m_ctx)
        return;
    const auto comments = m_ctx->db().comments(m_key);
    const bool fetched = m_ctx->db().commentsFetchedAt(m_key) > 0;
    m_tabs->setTabText(CommentsTab, fetched ? QString("Kommentare · %1").arg(comments.size()) : "Kommentare");

    clearLayout(m_commentList);
    if (!fetched || comments.isEmpty())
        m_commentList->addWidget(label(fetched ? "Noch keine Kommentare." : "Lade …", "muted"));
    const QString muted = Theme::muted().name();
    for (const auto &c : comments) {
        auto *avatar = label(initials(c.author), "avatar");
        avatar->setFixedSize(28, 28);
        avatar->setAlignment(Qt::AlignCenter);
        auto *body = new QVBoxLayout;
        body->setSpacing(2);
        body->addWidget(richText(QString("<b>%1</b> <span style='color:%2'>· %3</span>")
                                     .arg(c.author.toHtmlEscaped(), muted, IssueStyle::relativeTime(c.created))));
        body->addWidget(richText(c.html));
        auto *row = new QHBoxLayout;
        row->setSpacing(10);
        row->addWidget(avatar, 0, Qt::AlignTop);
        row->addLayout(body, 1);
        m_commentList->addLayout(row);
    }
    m_commentList->addStretch();
}

void IssueView::setHistoryMessage(const QString &text)
{
    clearLayout(m_historyGrid);
    m_historyGrid->addWidget(label(text, "muted"), 0, 0, 1, 2);
}

void IssueView::loadHistory()
{
    if (!m_ctx || m_key.isEmpty() || m_historyKey == m_key)
        return;
    m_historyKey = m_key;
    setHistoryMessage("Lade …");
    AccountContext *ctx = m_ctx;
    const QString key = m_key;
    ctx->backend().fetchHistory(key, [this, ctx, key](const QList<HistoryEntry> &entries, const QString &err) {
        if (ctx != m_ctx || key != m_key)
            return;
        if (!err.isEmpty()) {
            m_historyKey.clear();  // retry next time
            setHistoryMessage("Verlauf konnte nicht geladen werden: " + err);
            return;
        }
        if (entries.isEmpty()) {
            setHistoryMessage("Keine Änderungen.");
            return;
        }
        clearLayout(m_historyGrid);
        for (int i = 0; i < entries.size(); ++i) {
            const auto &e = entries[i];
            QLabel *when = label(IssueStyle::fullTime(e.when), "muted");
            when->setAlignment(Qt::AlignRight | Qt::AlignTop);
            m_historyGrid->addWidget(when, i, 0);
            m_historyGrid->addWidget(richText("<b>" + e.who.toHtmlEscaped() + "</b><br>" +
                                              e.text.toHtmlEscaped().replace("\n", "<br>")),
                                     i, 1);
        }
    });
}

// Actions ---------------------------------------------------------------------------

void IssueView::updateActions()
{
    const auto d = m_ctx ? m_ctx->db().issue(m_key) : std::nullopt;
    const QString me = m_ctx ? m_ctx->backend().myId() : QString();
    m_status->setEnabled(d && !m_busy && m_status->count() > 1);
    m_assign->setEnabled(d && !m_busy && !me.isEmpty() && d->assigneeId != me);
    m_assign->setToolTip(d && !me.isEmpty() && d->assigneeId == me ? "Ist dir bereits zugewiesen" : QString());
    m_comment->setEnabled(d.has_value());
    m_send->setEnabled(d && !m_busy && !m_draft->toPlainText().trimmed().isEmpty());
}

void IssueView::loadTransitions()
{
    AccountContext *ctx = m_ctx;
    const QString key = m_key;
    ctx->backend().transitions(key, [this, ctx, key](const QList<Transition> &list, const QString &err) {
        if (ctx != m_ctx || key != m_key || m_status->count() == 0)
            return;
        while (m_status->count() > 1)
            m_status->removeItem(1);
        if (!err.isEmpty())
            m_status->setToolTip("Statusübergänge nicht verfügbar: " + err);
        for (const auto &t : list)
            if (t.name != m_status->itemText(0))
                m_status->addItem("→ " + t.name, t.id);
        updateActions();
    });
}

void IssueView::applyTransition(int index)
{
    if (index <= 0 || !m_ctx)
        return;
    const QString id = m_status->itemData(index).toString();
    const QString name = m_status->itemText(index).mid(2);
    m_status->setCurrentIndex(0);
    m_busy = true;
    updateActions();
    AccountContext *ctx = m_ctx;
    const QString key = m_key;
    ctx->backend().transition(key, id, [this, ctx, key, name](const QString &err) {
        if (ctx != m_ctx || key != m_key)
            return;
        if (!err.isEmpty()) {
            m_busy = false;
            updateActions();
            emit message("Status konnte nicht geändert werden: " + err);
            return;
        }
        reloadAfterAction(key + ": Status → " + name);
    });
}

void IssueView::assignToMe()
{
    if (!m_ctx || !m_assign->isEnabled())
        return;
    m_busy = true;
    updateActions();
    AccountContext *ctx = m_ctx;
    const QString key = m_key;
    ctx->backend().assignToMe(key, [this, ctx, key](const QString &err) {
        if (ctx != m_ctx || key != m_key)
            return;
        if (!err.isEmpty()) {
            m_busy = false;
            updateActions();
            emit message("Zuweisen fehlgeschlagen: " + err);
            return;
        }
        reloadAfterAction(key + ": dir zugewiesen");
    });
}

void IssueView::startComment()
{
    if (!m_ctx)
        return;
    m_tabs->setCurrentIndex(CommentsTab);
    m_draft->setFocus();
}

void IssueView::sendComment()
{
    const QString text = m_draft->toPlainText().trimmed();
    if (!m_ctx || text.isEmpty() || m_busy)
        return;
    m_busy = true;
    m_send->setText("Sende …");
    updateActions();
    AccountContext *ctx = m_ctx;
    const QString key = m_key;
    ctx->backend().addComment(key, text, [this, ctx, key](const QString &err) {
        if (ctx != m_ctx || key != m_key)
            return;
        m_send->setText("Kommentar senden");
        if (!err.isEmpty()) {
            m_busy = false;
            updateActions();
            emit message("Kommentar konnte nicht gesendet werden: " + err);
            return;
        }
        m_draft->clear();
        ctx->refreshComments(key, [this, ctx, key](bool) {
            if (ctx == m_ctx && key == m_key)
                renderComments();
        }, true);
        reloadAfterAction(key + ": Kommentar gesendet");
    });
}

// The issue changed in the backend: fetch it again, own changes don't count as unread.
void IssueView::reloadAfterAction(const QString &done)
{
    AccountContext *ctx = m_ctx;
    const QString key = m_key;
    ctx->fetchIssue(key, [this, ctx, key, done](bool, const QString &) {
        ctx->db().markRead(key);
        if (ctx == m_ctx && key == m_key) {
            m_busy = false;
            m_historyKey.clear();
            render();
            loadTransitions();
            if (m_tabs->currentIndex() == HistoryTab)
                loadHistory();
        }
        emit message(done);
        emit changed(ctx, key);
    });
}

void IssueView::openInBrowser()
{
    if (m_ctx && !m_key.isEmpty())
        QDesktopServices::openUrl(QUrl(m_ctx->backend().browseUrl(m_key)));
}
