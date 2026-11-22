#include "MainWindow.h"
#include "Account.h"
#include "AccountContext.h"
#include "AccountPanel.h"
#include "Auth.h"
#include "IssueListModel.h"
#include "SyncEngine.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QDateTime>
#include <QElapsedTimer>
#include <QHeaderView>
#include <QLabel>
#include <QJsonArray>
#include <QJsonObject>
#include <QLineEdit>
#include <QMenu>
#include <QScrollBar>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTableView>
#include <QTextBrowser>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QHBoxLayout>

static QString fmtTime(const QString &utcIso)
{
    const auto dt = QDateTime::fromString(utcIso, Qt::ISODateWithMs);
    return dt.isValid() ? dt.toLocalTime().toString("dd.MM.yyyy HH:mm") : QString();
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle("Jira Client");
    resize(1400, 850);

    // Toolbar: account switcher, search, sync ---------------------------------
    auto *tb = addToolBar("Main");
    tb->setMovable(false);
    m_accountBox = new QComboBox;
    m_accountBox->setMinimumWidth(180);
    tb->addWidget(m_accountBox);
    auto *addMenu = new QMenu(this);
    addMenu->addAction("Mit API-Token …", this, [this] {
        showAccountPanel();
        m_accountPanel->showTokenForm();
    });
    addMenu->addAction("Mit Atlassian anmelden (Browser) …", this, [this] {
        showAccountPanel();
        m_accountPanel->startOAuth();
    });
    auto *addBtn = new QToolButton;
    addBtn->setText("+ Konto");
    addBtn->setMenu(addMenu);
    addBtn->setPopupMode(QToolButton::InstantPopup);
    tb->addWidget(addBtn);
    auto *editBtn = new QToolButton;
    editBtn->setText(QString::fromUtf8("⚙"));
    editBtn->setToolTip("Konto bearbeiten: Name, Token, was lokal gespeichert wird");
    connect(editBtn, &QToolButton::clicked, this, [this] {
        if (!m_ctx)
            return;
        showAccountPanel();
        m_accountPanel->showEditForm(m_ctx->account());
    });
    tb->addWidget(editBtn);

    tb->addSeparator();
    m_search = new QLineEdit;
    m_search->setPlaceholderText(QString("Suchen – Key, Text, Label  (%1)")
                                     .arg(QKeySequence("Ctrl+L").toString(QKeySequence::NativeText)));
    m_search->setClearButtonEnabled(true);
    tb->addWidget(m_search);
    auto *syncAct = tb->addAction(QString::fromUtf8("⟳"), this, [this] { if (m_ctx) m_ctx->sync().syncNow(); });
    syncAct->setShortcut(QKeySequence("Ctrl+R"));
    syncAct->setToolTip(QString("Jetzt synchronisieren (%1)").arg(syncAct->shortcut().toString(QKeySequence::NativeText)));

    // Left: bar [Tickets|Sprint] + list, right: detail ---------------------------
    m_resultModel = new IssueListModel(this);
    m_resultView = makeTable(m_resultModel);
    m_sprintModel = new IssueListModel(this);
    m_sprintView = makeTable(m_sprintModel);
    m_lists = new QStackedWidget;
    m_lists->addWidget(m_resultView);
    m_lists->addWidget(m_sprintView);

    auto *bar = new QWidget;
    auto *barLayout = new QHBoxLayout(bar);
    barLayout->setContentsMargins(8, 6, 8, 6);
    barLayout->setSpacing(4);
    auto *modes = new QButtonGroup(this);
    for (const QString &label : {QStringLiteral("Tickets"), QStringLiteral("Sprint")}) {
        auto *b = new QToolButton;
        b->setText(label);
        b->setCheckable(true);
        b->setAutoRaise(true);
        b->setToolTip(QString("Tickets ↔ Sprint (%1)").arg(QKeySequence("Ctrl+T").toString(QKeySequence::NativeText)));
        modes->addButton(b, int(modes->buttons().size()));
        barLayout->addWidget(b);
    }
    modes->button(0)->setChecked(true);
    m_modeButtons = modes;
    barLayout->addSpacing(12);
    m_listTitle = new QLabel;
    m_sprintBox = new QComboBox;
    m_sprintBox->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_sprintBox->hide();
    barLayout->addWidget(m_listTitle);
    barLayout->addWidget(m_sprintBox);
    barLayout->addStretch();
    connect(modes, &QButtonGroup::idClicked, this, &MainWindow::setListMode);

    auto *left = new QWidget;
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);
    leftLayout->addWidget(bar);
    leftLayout->addWidget(m_lists);

    m_detail = new QTextBrowser;
    m_detail->setOpenExternalLinks(true);
    m_detail->setFrameShape(QFrame::NoFrame);
    m_detail->document()->setDocumentMargin(18);

    auto *split = new QSplitter;
    split->setHandleWidth(1);
    split->addWidget(left);
    split->addWidget(m_detail);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);

    m_accountPanel = new AccountPanel;
    m_pages = new QStackedWidget;
    m_pages->addWidget(split);
    m_pages->addWidget(m_accountPanel);
    setCentralWidget(m_pages);

    m_status = new QLabel;
    m_searchInfo = new QLabel;
    statusBar()->addWidget(m_status, 1);
    statusBar()->addPermanentWidget(m_searchInfo);

    // Timers --------------------------------------------------------------------
    m_cloudTimer.setSingleShot(true);
    m_cloudTimer.setInterval(350);
    connect(&m_cloudTimer, &QTimer::timeout, this, &MainWindow::runCloudSearch);
    m_refreshTimer.setSingleShot(true);
    connect(&m_refreshTimer, &QTimer::timeout, this, &MainWindow::backgroundRefresh);

    // Wiring --------------------------------------------------------------------
    connect(m_accountBox, &QComboBox::currentIndexChanged, this, &MainWindow::switchAccount);
    connect(m_search, &QLineEdit::textChanged, this, [this] {
        ++m_searchGen;  // invalidate pending cloud responses
        m_cloud.clear();
        m_cloudInfo.clear();
        runLocalSearch();
        if (!m_search->text().trimmed().isEmpty())
            m_cloudTimer.start();
    });
    connect(m_search, &QLineEdit::returnPressed, this, [this] {
        setListMode(0);
        m_resultView->setFocus();
        if (m_resultModel->rowCount() > 0 && !m_resultView->currentIndex().isValid())
            m_resultView->setCurrentIndex(m_resultModel->index(0, 0));
    });
    connect(m_sprintBox, &QComboBox::currentIndexChanged, this, &MainWindow::showSprint);
    m_accountPanel->setSessionProvider([this](const QString &user) { return oauthSession(user); });
    connect(m_accountPanel, &AccountPanel::accountReady, this, &MainWindow::addAccount);
    connect(m_accountPanel, &AccountPanel::accountUpdated, this, &MainWindow::updateAccount);
    connect(m_accountPanel, &AccountPanel::closed, this, [this] {
        if (m_contexts.isEmpty())
            m_accountPanel->showChoice();  // nothing to go back to
        else
            m_pages->setCurrentIndex(0);
    });
    connect(m_accountPanel, &AccountPanel::oauthLoggedIn, this, [this](const QJsonObject &tokens, const OAuthIdentity &who) {
        raise();
        activateWindow();
        oauthSession(who.accountId)->setTokens(tokens);
        // Re-login of a known identity: its sites continue with the new tokens.
        for (AccountContext *ctx : std::as_const(m_contexts))
            if (ctx->account().isOAuth() && ctx->account().oauthUser == who.accountId)
                ctx->sync().syncNow();
        statusBar()->showMessage("Angemeldet als " + who.email, 5000);
    });

    // Keyboard ------------------------------------------------------------------
    connect(new QShortcut(QKeySequence("Ctrl+L"), this), &QShortcut::activated, this, [this] {
        m_search->setFocus();
        m_search->selectAll();
    });
    connect(new QShortcut(QKeySequence("Ctrl+F"), this), &QShortcut::activated, this, [this] {
        m_search->setFocus();
        m_search->selectAll();
    });
    auto widgetShortcut = [](QWidget *w) {
        auto *s = new QShortcut(QKeySequence(Qt::Key_Escape), w);
        s->setContext(Qt::WidgetShortcut);
        return s;
    };
    connect(widgetShortcut(m_search), &QShortcut::activated, m_search, &QLineEdit::clear);
    for (QTableView *v : {m_resultView, m_sprintView})
        connect(widgetShortcut(v), &QShortcut::activated, this, [this] {
            m_search->setFocus();
            m_search->selectAll();
        });
    connect(new QShortcut(QKeySequence("Ctrl+T"), this), &QShortcut::activated, this, [this] {
        setListMode(1 - m_lists->currentIndex());
    });
    for (int i = 0; i < 9; ++i)
        connect(new QShortcut(QKeySequence(QString("Ctrl+%1").arg(i + 1)), this), &QShortcut::activated, this,
                [this, i] { if (i < m_contexts.size()) m_accountBox->setCurrentIndex(i); });

    loadAccounts();
}

QTableView *MainWindow::makeTable(IssueListModel *model)
{
    auto *v = new QTableView;
    v->setModel(model);
    v->setSelectionBehavior(QAbstractItemView::SelectRows);
    v->setSelectionMode(QAbstractItemView::SingleSelection);
    v->setEditTriggers(QAbstractItemView::NoEditTriggers);
    v->setItemDelegate(new IssueDelegate(v));
    v->setShowGrid(false);
    v->setWordWrap(false);
    v->setFrameShape(QFrame::NoFrame);
    v->setIconSize(QSize(16, 16));
    v->verticalHeader()->hide();
    v->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    v->verticalHeader()->setDefaultSectionSize(v->fontMetrics().height() + 14);
    auto *h = v->horizontalHeader();
    h->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    h->setHighlightSections(false);
    h->setSectionResizeMode(QHeaderView::Interactive);
    h->setSectionResizeMode(IssueListModel::Summary, QHeaderView::Stretch);
    h->resizeSection(IssueListModel::Key, 130);
    h->resizeSection(IssueListModel::Type, 90);
    h->resizeSection(IssueListModel::Status, 130);
    h->resizeSection(IssueListModel::Priority, 90);
    h->resizeSection(IssueListModel::Assignee, 140);
    h->resizeSection(IssueListModel::Reporter, 140);
    h->resizeSection(IssueListModel::Project, 80);
    h->resizeSection(IssueListModel::Labels, 140);
    h->resizeSection(IssueListModel::Created, 100);
    h->resizeSection(IssueListModel::Updated, 100);

    // Columns: right click on the header, same choice for all lists, remembered.
    const QStringList hidden = QSettings().value("list/hiddenColumns", [] {
        QStringList names;
        for (int c : IssueListModel::defaultHidden())
            names << IssueListModel::columnName(c);
        return names;
    }()).toStringList();
    for (int c = 0; c < IssueListModel::ColumnCount; ++c)
        h->setSectionHidden(c, hidden.contains(IssueListModel::columnName(c)));
    h->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(h, &QHeaderView::customContextMenuRequested, this, [this, h](const QPoint &pos) {
        QMenu menu;
        menu.addSection("Spalten");
        for (int c = 0; c < IssueListModel::ColumnCount; ++c) {
            if (c == IssueListModel::Key || c == IssueListModel::Summary)
                continue;
            QAction *a = menu.addAction(IssueListModel::columnName(c));
            a->setCheckable(true);
            a->setChecked(!h->isSectionHidden(c));
            connect(a, &QAction::toggled, this, [this, c](bool on) {
                QStringList names;
                for (QTableView *t : {m_resultView, m_sprintView}) {
                    t->horizontalHeader()->setSectionHidden(c, !on);
                }
                for (int i = 0; i < IssueListModel::ColumnCount; ++i)
                    if (m_resultView->horizontalHeader()->isSectionHidden(i))
                        names << IssueListModel::columnName(i);
                QSettings().setValue("list/hiddenColumns", names);
            });
        }
        menu.exec(h->mapToGlobal(pos));
    });
    connect(v->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [this, model](const QModelIndex &cur) { if (cur.isValid()) openIssue(model->keyAt(cur.row())); });
    return v;
}

// Accounts ------------------------------------------------------------------------

void MainWindow::loadAccounts()
{
    const auto accounts = AccountStore::load();
    for (const auto &a : accounts) {
        if (a.isOAuth())
            addContext(a, oauthSession(a.oauthUser));
        else
            addContext(a, new BasicAuth(a.email, AccountStore::readToken(a.id), this));
    }
    // Tokens used to be in the OS keychain -> enter once again.
    for (int i = 0; i < m_contexts.size(); ++i) {
        const Account &a = m_contexts[i]->account();
        if (!a.isOAuth() && AccountStore::readToken(a.id).isEmpty()) {
            switchAccount(i);
            showAccountPanel();
            m_accountPanel->showEditForm(a);
            break;
        }
    }
    if (m_contexts.isEmpty()) {
        showAccountPanel();
        m_accountPanel->showChoice();
    }
}

OAuthSession *MainWindow::oauthSession(const QString &atlassianAccountId)
{
    if (auto *s = m_sessions.value(atlassianAccountId))
        return s;
    auto *s = new OAuthSession(atlassianAccountId, this);
    s->load();
    connect(s, &OAuthSession::loginRequired, this, [this, atlassianAccountId] {
        QString who = atlassianAccountId;
        for (auto *ctx : std::as_const(m_contexts))
            if (ctx->account().oauthUser == atlassianAccountId && !ctx->account().email.isEmpty())
                who = ctx->account().email;
        statusBar()->showMessage(QString::fromUtf8("Anmeldung für %1 abgelaufen – „+ Konto → Mit Atlassian anmelden“")
                                     .arg(who));
    });
    m_sessions.insert(atlassianAccountId, s);
    return s;
}

void MainWindow::showAccountPanel()
{
    m_accountPanel->setCancelable(!m_contexts.isEmpty());
    m_pages->setCurrentWidget(m_accountPanel);
}

void MainWindow::addAccount(const Account &account, const QString &token)
{
    auto accounts = AccountStore::load();
    accounts << account;
    AccountStore::save(accounts);
    if (account.isOAuth())
        addContext(account, oauthSession(account.oauthUser));
    else
        addContext(account, new BasicAuth(account.email, token, this));
    switchAccount(int(m_contexts.size()) - 1);
}

void MainWindow::updateAccount(const Account &account, const QString &token)
{
    auto accounts = AccountStore::load();
    for (auto &a : accounts)
        if (a.id == account.id)
            a = account;
    AccountStore::save(accounts);
    for (int i = 0; i < m_contexts.size(); ++i) {
        AccountContext *ctx = m_contexts[i];
        if (ctx->account().id != account.id)
            continue;
        if (auto *basic = qobject_cast<BasicAuth *>(ctx->auth()))
            basic->setCredentials(account.email, token.isEmpty() ? AccountStore::readToken(account.id) : token);
        ctx->update(account);  // changed scope -> full resync
        m_accountBox->setItemText(i, account.label());
        if (ctx == m_ctx)
            setWindowTitle("Jira Client – " + account.name);
    }
}

void MainWindow::addContext(const Account &account, AuthProvider *auth)
{
    auto *ctx = new AccountContext(account, auth, this);
    QString err;
    if (!ctx->open(&err)) {
        statusBar()->showMessage("Datenbank " + account.name + ": " + err);
        delete ctx;
        return;
    }
    m_contexts << ctx;
    connect(&ctx->sync(), &SyncEngine::statusChanged, this, [this, ctx](const QString &s) {
        if (ctx == m_ctx)
            m_status->setText(ctx->account().name + " · " + s);
    });
    connect(&ctx->sync(), &SyncEngine::dataChanged, this, [this, ctx] {
        if (ctx != m_ctx)
            return;
        runLocalSearch();
        reloadSprints();
        if (!m_currentKey.isEmpty())
            renderDetail();
    });
    {
        QSignalBlocker b(m_accountBox);
        m_accountBox->addItem(account.label());
    }
    if (!m_ctx)
        switchAccount(0);
    ctx->sync().start();
}

void MainWindow::switchAccount(int index)
{
    if (index < 0 || index >= m_contexts.size())
        return;
    m_ctx = m_contexts[index];
    {
        QSignalBlocker b(m_accountBox);
        m_accountBox->setCurrentIndex(index);
    }
    setWindowTitle("Jira Client – " + m_ctx->account().name);
    m_status->setText(m_ctx->account().name + " · " + m_ctx->sync().status());
    m_currentKey.clear();
    m_renderedKey.clear();
    m_detail->clear();
    ++m_searchGen;
    m_cloud.clear();
    m_cloudInfo.clear();
    runLocalSearch();
    if (!m_search->text().trimmed().isEmpty())
        m_cloudTimer.start();
    reloadSprints();
}

// Search: local first, cloud fallback appended ---------------------------------

void MainWindow::runLocalSearch()
{
    if (!m_ctx)
        return;
    QElapsedTimer t;
    t.start();
    const QString q = m_search->text().trimmed();
    m_local = q.isEmpty() ? m_ctx->db().myOpen(m_ctx->me(), 500) : m_ctx->db().search(q, 300);
    m_localMs = t.nsecsElapsed() / 1e6;
    applyResults();
}

void MainWindow::runCloudSearch()
{
    const QString q = m_search->text().trimmed();
    if (!m_ctx || q.isEmpty())
        return;
    const quint64 gen = ++m_searchGen;
    AccountContext *ctx = m_ctx;
    QElapsedTimer timer;
    timer.start();
    m_cloudInfo = QString::fromUtf8("☁ …");
    updateSearchInfo();
    ctx->searchCloud(q, [this, gen, ctx, timer](QList<IssueRow> rows, const QString &err) {
        const qint64 ms = timer.elapsed();
        if (gen != m_searchGen || ctx != m_ctx)
            return;  // outdated
        if (!err.isEmpty()) {
            m_cloudInfo = QString::fromUtf8("☁ nicht erreichbar");
            m_cloud.clear();
        } else {
            m_cloud = rows;
            m_cloudInfo = QString::fromUtf8("☁ %1 (%2 ms)").arg(rows.size()).arg(ms);
        }
        applyResults();
    });
}

void MainWindow::applyResults()
{
    QList<IssueRow> merged = m_local;
    QSet<QString> keys;
    for (const auto &r : m_local)
        keys.insert(r.key);
    for (IssueRow r : m_cloud) {
        if (keys.contains(r.key))
            continue;
        r.cloud = true;
        merged << r;
    }
    m_resultModel->setRows(merged);
    reselect(m_resultView, m_resultModel);
    updateSearchInfo();
    updateListTitle();
}

void MainWindow::setListMode(int mode)
{
    m_lists->setCurrentIndex(mode);
    m_modeButtons->button(mode)->setChecked(true);
    m_listTitle->setVisible(mode == 0);
    m_sprintBox->setVisible(mode == 1);
    if (mode == 1 && m_sprintBox->count() == 0)
        m_sprintBox->setPlaceholderText("Kein offener Sprint");
    updateListTitle();
}

void MainWindow::updateListTitle()
{
    const int n = m_resultModel->rowCount();
    const QString q = m_search->text().trimmed();
    m_listTitle->setText(q.isEmpty() ? QString("<b>Meine offenen Tickets</b> · %1").arg(n)
                                     : QString("<b>%1 Treffer</b>").arg(n));
}

void MainWindow::updateSearchInfo()
{
    QString s = QString("Lokal: %1 (%2 ms)").arg(m_local.size()).arg(m_localMs, 0, 'f', 1);
    if (!m_cloudInfo.isEmpty())
        s += "   " + m_cloudInfo;
    m_searchInfo->setText(s);
}

void MainWindow::reselect(QTableView *view, IssueListModel *model)
{
    const int row = model->rowOf(m_currentKey);
    if (row < 0)
        return;
    {
        QSignalBlocker b(view->selectionModel());
        view->selectionModel()->setCurrentIndex(model->index(row, 0),
                                                QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    }
    view->viewport()->update();
}

// Sprint view -------------------------------------------------------------------

void MainWindow::reloadSprints()
{
    if (!m_ctx)
        return;
    const qint64 current = m_sprintBox->currentData().toLongLong();
    {
        QSignalBlocker b(m_sprintBox);
        m_sprintBox->clear();
        int select = 0;
        const auto sprints = m_ctx->db().openSprints();
        for (int i = 0; i < sprints.size(); ++i) {
            const auto &s = sprints[i];
            QString label = s.name;
            if (s.state == "active")
                label = QString::fromUtf8("▶ ") + label + "  (bis " + fmtTime(s.end).left(10) + ")";
            else
                label += "  (geplant)";
            m_sprintBox->addItem(label, s.id);
            if (s.id == current)
                select = i;
        }
        if (m_sprintBox->count() > 0)
            m_sprintBox->setCurrentIndex(select);
    }
    showSprint();
}

void MainWindow::showSprint()
{
    if (!m_ctx)
        return;
    const qint64 id = m_sprintBox->currentData().toLongLong();
    m_sprintModel->setRows(id ? m_ctx->db().sprintIssues(id) : QList<IssueRow>{});
    reselect(m_sprintView, m_sprintModel);
}

// Detail ------------------------------------------------------------------------

void MainWindow::openIssue(const QString &key)
{
    if (!m_ctx || key.isEmpty())
        return;
    m_currentKey = key;
    renderDetail();  // always from the local db -> instant
    const bool local = m_ctx->db().issue(key).has_value();
    m_refreshTimer.start(local ? 250 : 0);  // debounce while scrolling through the list
}

void MainWindow::backgroundRefresh()
{
    AccountContext *ctx = m_ctx;
    const QString key = m_currentKey;
    if (!ctx || key.isEmpty())
        return;
    ctx->db().touchViewed(key);

    const auto d = ctx->db().issue(key);
    if (!d || !d->inScope) {  // not synced -> fetch into cache / refresh cache
        ctx->fetchIssue(key, [this, ctx, key](bool ok, const QString &err) {
            if (ctx != m_ctx || key != m_currentKey)
                return;
            if (ok)
                renderDetail();
            else if (!ctx->db().issue(key))
                m_detail->setHtml("<p>" + key.toHtmlEscaped() + " konnte nicht geladen werden: " +
                                  err.toHtmlEscaped() + "</p>");
        });
    }
    ctx->refreshComments(key, [this, ctx, key](bool changed) {
        if (changed && ctx == m_ctx && key == m_currentKey)
            renderDetail();
    });
}

void MainWindow::renderDetail()
{
    if (!m_ctx || m_currentKey.isEmpty())
        return;
    const QString key = m_currentKey;
    const auto d = m_ctx->db().issue(key);
    if (!d) {
        m_detail->setHtml(QString::fromUtf8("<p>Lade <b>%1</b> aus der Cloud …</p>").arg(key.toHtmlEscaped()));
        m_renderedKey.clear();
        return;
    }

    const bool sameIssue = m_renderedKey == key;
    const int scroll = m_detail->verticalScrollBar()->value();

    // Colors from the palette -> works in light and dark mode.
    const QPalette pal = m_detail->palette();
    const QString muted = pal.color(QPalette::PlaceholderText).name();
    const QString card = pal.color(QPalette::AlternateBase).name();
    auto heading = [&muted](const QString &t) {
        return "<p style='margin-top:22px; margin-bottom:6px; color:" + muted +
               "; font-size:small; font-weight:600'>" + t.toUpper() + "</p>";
    };
    auto pill = [](const QString &text, const QColor &c) {
        return QString("<span style='background-color:%1; color:white; font-size:small; font-weight:600'>"
                       "&nbsp;%2&nbsp;</span>").arg(c.name(), text.toUpper().toHtmlEscaped());
    };
    auto label = [&muted](const QString &t) {
        return "<td width='18%' style='color:" + muted + "'>" + t + "</td>";
    };
    auto value = [](const QString &v) {
        return "<td width='32%'>" + (v.isEmpty() ? QStringLiteral("—") : v) + "</td>";
    };

    QString h;
    h += QString("<p style='margin:0; color:%1'><span style='color:%2'>&#9632;</span> %3 &nbsp;·&nbsp; "
                 "<a href=\"%4\">%5</a>%6</p>")
             .arg(muted, IssueStyle::typeColor(d->issueType).name(), d->issueType.toHtmlEscaped(),
                  m_ctx->browseUrl(key).toHtmlEscaped(), key.toHtmlEscaped(),
                  d->inScope ? QString() : QString::fromUtf8(" &nbsp;·&nbsp; Cache (nicht im Sync-Bereich)"));
    h += "<p style='margin-top:6px; margin-bottom:14px; font-size:17pt; font-weight:600'>" +
         d->summary.toHtmlEscaped() + "</p>";

    h += "<table width='100%' cellspacing='0' cellpadding='4'>";
    h += "<tr>" + label("Status") + value(pill(d->status, IssueStyle::statusColor(d->statusCat))) +
         label("Priorität") + value(d->priority.toHtmlEscaped()) + "</tr>";
    h += "<tr>" + label("Zugewiesen") + value(d->assignee.toHtmlEscaped()) + label("Melder") +
         value(d->reporter.toHtmlEscaped()) + "</tr>";
    h += "<tr>" + label("Erstellt") + value(IssueStyle::fullTime(d->created)) + label("Geändert") +
         value(IssueStyle::fullTime(d->updated)) + "</tr>";
    if (!d->labels.isEmpty()) {
        QString chips;
        for (const auto &l : d->labels.split(' ', Qt::SkipEmptyParts))
            chips += "<span style='background-color:" + card + "'>&nbsp;" + l.toHtmlEscaped() + "&nbsp;</span> ";
        h += "<tr>" + label("Labels") + "<td colspan='3'>" + chips + "</td></tr>";
    }
    h += "</table>";

    h += heading("Beschreibung");
    h += d->descriptionHtml.isEmpty() ? "<p style='color:" + muted + "'>Keine Beschreibung</p>" : d->descriptionHtml;

    const auto comments = m_ctx->db().comments(key);
    const bool fetched = m_ctx->db().commentsFetchedAt(key) > 0;
    h += heading(fetched ? QString("Kommentare · %1").arg(comments.size()) : QStringLiteral("Kommentare"));
    if (!fetched)
        h += QString::fromUtf8("<p style='color:%1'>Lade …</p>").arg(muted);
    else if (comments.isEmpty())
        h += "<p style='color:" + muted + "'>Noch keine Kommentare</p>";
    for (const auto &c : comments) {
        h += "<table width='100%' cellspacing='0' cellpadding='10' style='margin-bottom:8px'><tr><td bgcolor='" + card +
             "'><b>" + c.author.toHtmlEscaped() + "</b> &nbsp;<span style='color:" + muted + "'>" +
             IssueStyle::relativeTime(c.created) + "</span>" + c.html + "</td></tr></table>";
    }

    m_detail->setHtml(h);
    if (sameIssue)
        m_detail->verticalScrollBar()->setValue(scroll);
    m_renderedKey = key;
}
