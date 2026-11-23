#include "MainWindow.h"
#include "Account.h"
#include "AccountContext.h"
#include "AccountPanel.h"
#include "Auth.h"
#include "Backend.h"
#include "IssueListModel.h"
#include "IssueView.h"
#include "SyncEngine.h"
#include "Theme.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QTableView>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

static constexpr int kViewLimit = 5000;
static constexpr int kSearchLimit = 300;

static QString shortcutText(const char *keys)
{
    return QKeySequence(keys).toString(QKeySequence::NativeText);
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle("Ticketing UI");
    setMinimumSize(1000, 600);
    resize(1400, 850);

    // Toolbar: account, sync, search, account management ---------------------------
    auto *tb = addToolBar("Werkzeugleiste");
    tb->setMovable(false);
    tb->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_accountBox = new QComboBox;
    m_accountBox->setMinimumWidth(230);
    m_accountBox->setToolTip(QString("Konto wählen (%1 … %2, %3 = alle)")
                                 .arg(shortcutText("Ctrl+1"), shortcutText("Ctrl+9"), shortcutText("Ctrl+0")));
    tb->addWidget(m_accountBox);
    m_syncAct = tb->addAction(Theme::icon(Theme::Icon::Sync), "Synchronisieren", this, [this] {
        for (AccountContext *ctx : scope())
            ctx->sync().syncNow();
    });
    m_syncAct->setShortcuts({QKeySequence(Qt::Key_F5), QKeySequence("Ctrl+R")});
    m_syncAct->setToolTip(QString("Jetzt synchronisieren (%1)").arg(m_syncAct->shortcut().toString(QKeySequence::NativeText)));
    tb->addSeparator();
    m_search = new QLineEdit;
    m_search->setPlaceholderText("Suchen – Key, Titel, Beschreibung");
    m_search->setClearButtonEnabled(true);
    m_search->setMaximumWidth(560);
    m_search->setMinimumWidth(260);
    m_search->addAction(Theme::icon(Theme::Icon::Search), QLineEdit::LeadingPosition);
    // Shortcut hint inside the field while it is empty.
    auto *chip = new QLabel(shortcutText("Ctrl+K"), m_search);
    chip->setStyleSheet(QString("color: %1; border: 1px solid %2; border-radius: 3px; padding: 0 5px; font-size: 11px;")
                            .arg(Theme::muted().name(), Theme::line().name()));
    auto *chipRow = new QHBoxLayout(m_search);
    chipRow->setContentsMargins(0, 0, 6, 0);
    chipRow->addStretch();
    chipRow->addWidget(chip);
    connect(m_search, &QLineEdit::textChanged, chip, [chip](const QString &t) { chip->setVisible(t.isEmpty()); });
    tb->addWidget(m_search);
    auto *spacer = new QWidget;
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tb->addWidget(spacer);
    auto *manage = new QToolButton;
    manage->setIcon(Theme::icon(Theme::Icon::Gear));
    manage->setToolTip("Konten verwalten");
    manage->setAccessibleName("Konten verwalten");
    tb->addWidget(manage);
    connect(manage, &QToolButton::clicked, this, [this] {
        showAccountPanel();
        m_accountPanel->showManage(currentContext() ? currentContext()->account().id : QString());
    });

    // List + detail --------------------------------------------------------------------
    m_issue = new IssueView;
    auto *split = new QSplitter;
    split->setHandleWidth(1);
    split->setChildrenCollapsible(false);
    split->addWidget(buildList());
    split->addWidget(m_issue);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 0);
    split->setSizes({920, 480});

    m_accountPanel = new AccountPanel;
    m_accountPanel->setBackendLookup([this](const QString &id) {
        AccountContext *ctx = context(id);
        return ctx ? &ctx->backend() : nullptr;
    });
    m_pages = new QStackedWidget;
    m_pages->addWidget(split);
    m_pages->addWidget(m_accountPanel);
    setCentralWidget(m_pages);

    // Status bar ----------------------------------------------------------------------
    m_connection = new QLabel;
    m_syncTimes = new QLabel;
    m_counts = new QLabel;
    statusBar()->addWidget(m_connection);
    statusBar()->addWidget(m_syncTimes, 1);
    statusBar()->addPermanentWidget(m_counts);

    buildMenus();

    // Wiring ----------------------------------------------------------------------------
    m_reloadTimer.setSingleShot(true);
    m_reloadTimer.setInterval(150);
    connect(&m_reloadTimer, &QTimer::timeout, this, [this] {
        reloadList();
        m_issue->refresh();
    });
    connect(m_accountBox, &QComboBox::currentIndexChanged, this, &MainWindow::switchScope);
    connect(m_search, &QLineEdit::textChanged, this, [this] {
        ++m_searchGen;  // invalidate pending cloud responses
        m_cloud.clear();
        m_cloudInfo.clear();
        reloadList();
    });
    connect(m_search, &QLineEdit::returnPressed, this, [this] {
        m_table->setFocus();
        if (m_proxy->rowCount() > 0 && !m_table->currentIndex().isValid())
            m_table->setCurrentIndex(m_proxy->index(0, 0));
    });
    connect(m_issue, &IssueView::read, this, [this](AccountContext *ctx, const QString &key) {
        m_model->setRead(ctx->account().id, key);
        updateListHeader();
    });
    connect(m_issue, &IssueView::changed, this, [this] { m_reloadTimer.start(); });
    connect(m_issue, &IssueView::message, this, [this](const QString &t) { statusBar()->showMessage(t, 6000); });
    connect(m_accountPanel, &AccountPanel::accountReady, this, &MainWindow::addAccount);
    connect(m_accountPanel, &AccountPanel::accountUpdated, this, &MainWindow::updateAccount);
    connect(m_accountPanel, &AccountPanel::accountRemoved, this, &MainWindow::removeAccount);
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

    // Keyboard ------------------------------------------------------------------------
    connect(new QShortcut(QKeySequence(Qt::Key_Escape), m_search, nullptr, nullptr, Qt::WidgetShortcut), &QShortcut::activated,
            m_search, &QLineEdit::clear);
    connect(new QShortcut(QKeySequence(Qt::Key_Escape), m_table, nullptr, nullptr, Qt::WidgetShortcut), &QShortcut::activated, this,
            [this] {
        m_search->setFocus();
        m_search->selectAll();
    });
    for (int i = 0; i <= 9; ++i)
        connect(new QShortcut(QKeySequence(QString("Ctrl+%1").arg(i)), this), &QShortcut::activated, this, [this, i] {
            if (i < m_accountBox->count())
                m_accountBox->setCurrentIndex(i);  // 0 = all accounts
        });

    loadAccounts();
}

void MainWindow::buildMenus()
{
    auto *file = menuBar()->addMenu("&Datei");
    file->addAction("Konto hinzufügen …", this, [this] {
        showAccountPanel();
        m_accountPanel->showChoice();
    });
    file->addAction("Konten verwalten …", this, [this] {
        showAccountPanel();
        m_accountPanel->showManage(currentContext() ? currentContext()->account().id : QString());
    });
    file->addSeparator();
    auto *quit = file->addAction("Beenden", qApp, &QApplication::quit);
    quit->setShortcut(QKeySequence::Quit);
    quit->setMenuRole(QAction::QuitRole);

    auto *edit = menuBar()->addMenu("&Bearbeiten");
    auto *find = edit->addAction("Suchen", this, [this] {
        m_pages->setCurrentIndex(0);
        m_search->setFocus();
        m_search->selectAll();
    });
    find->setShortcuts({QKeySequence("Ctrl+F"), QKeySequence("Ctrl+K")});

    auto *view = menuBar()->addMenu("&Ansicht");
    view->addAction(m_syncAct);
    m_hideDoneAct = view->addAction("Erledigte ausblenden");
    m_hideDoneAct->setCheckable(true);
    m_hideDoneAct->setChecked(QSettings().value("list/hideDone", true).toBool());
    connect(m_hideDoneAct, &QAction::toggled, this, [this](bool on) {
        QSettings().setValue("list/hideDone", on);
        QSignalBlocker b(m_hideDone);
        m_hideDone->setChecked(on);
        m_proxy->setHideDone(on);  // cloud hits
        reloadList();
    });
    m_hideDone->setChecked(m_hideDoneAct->isChecked());
    m_proxy->setHideDone(m_hideDoneAct->isChecked());
    connect(m_hideDone, &QCheckBox::toggled, m_hideDoneAct, &QAction::setChecked);

    auto *ticket = menuBar()->addMenu("&Ticket");
    auto *cloud = ticket->addAction("In der Cloud weitersuchen", this, &MainWindow::runCloudSearch);
    cloud->setShortcuts({QKeySequence("Ctrl+Return"), QKeySequence("Ctrl+Enter")});
    ticket->addSeparator();
    ticket->addAction("Im Browser öffnen", m_issue, &IssueView::openInBrowser)->setShortcut(QKeySequence("Ctrl+O"));
    ticket->addAction("Mir zuweisen", m_issue, &IssueView::assignToMe);
    ticket->addAction("Kommentieren", m_issue, &IssueView::startComment);

    auto *help = menuBar()->addMenu("&Hilfe");
    auto *about = help->addAction("Über Ticketing UI", this, [this] {
        statusBar()->showMessage(QString("Ticketing UI %1 · Tickets lokal, Suche ohne Wartezeit")
                                     .arg(QApplication::applicationVersion()), 8000);
    });
    about->setMenuRole(QAction::AboutRole);
}

QWidget *MainWindow::buildList()
{
    m_listTitle = new QLabel;
    QFont tf = m_listTitle->font();
    tf.setBold(true);
    tf.setPointSizeF(tf.pointSizeF() * 1.08);
    m_listTitle->setFont(tf);
    m_listSub = new QLabel;
    m_listSub->setForegroundRole(QPalette::PlaceholderText);
    m_hideDone = new QCheckBox("Erledigte ausblenden");
    m_interestOnly = new QCheckBox("Nur meine Projekte");
    m_interestOnly->setToolTip("Projekte unter ⚙ → Konto → Anzeigen");
    m_interestOnly->setChecked(QSettings().value("list/interestOnly", true).toBool());
    connect(m_interestOnly, &QCheckBox::toggled, this, [this](bool on) {
        QSettings().setValue("list/interestOnly", on);
        reloadList();
    });
    auto *head = new QHBoxLayout;
    head->setContentsMargins(12, 8, 12, 8);
    head->setSpacing(12);
    head->addWidget(m_listTitle);
    head->addWidget(m_listSub);
    head->addStretch();
    head->addWidget(m_interestOnly);
    head->addWidget(m_hideDone);

    m_model = new IssueListModel(this);
    m_proxy = new IssueProxyModel(this);
    m_proxy->setSourceModel(m_model);
    m_table = new QTableView;
    setupTable();
    auto *empty = new QLabel("Keine Tickets in dieser Ansicht.");
    empty->setAlignment(Qt::AlignCenter);
    empty->setForegroundRole(QPalette::PlaceholderText);
    m_listStack = new QStackedWidget;
    m_listStack->addWidget(m_table);
    m_listStack->addWidget(empty);

    m_cloudInfoLabel = new QLabel;
    m_cloudInfoLabel->setForegroundRole(QPalette::PlaceholderText);
    auto *cloudBtn = new QPushButton("In der Cloud weitersuchen");
    cloudBtn->setToolTip(shortcutText("Ctrl+Return"));
    connect(cloudBtn, &QPushButton::clicked, this, &MainWindow::runCloudSearch);
    m_cloudBar = new QWidget;
    auto *cb = new QHBoxLayout(m_cloudBar);
    cb->setContentsMargins(12, 6, 12, 6);
    cb->addWidget(m_cloudInfoLabel);
    cb->addWidget(cloudBtn);
    cb->addStretch();
    m_cloudBar->hide();

    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    v->addLayout(head);
    v->addWidget(m_listStack, 1);
    v->addWidget(m_cloudBar);
    w->setMinimumWidth(420);
    return w;
}

void MainWindow::setupTable()
{
    QTableView *v = m_table;
    v->setModel(m_proxy);
    v->setSelectionBehavior(QAbstractItemView::SelectRows);
    v->setSelectionMode(QAbstractItemView::SingleSelection);
    v->setEditTriggers(QAbstractItemView::NoEditTriggers);
    v->setItemDelegate(new IssueDelegate(v));
    v->setShowGrid(false);
    v->setWordWrap(false);
    v->setFrameShape(QFrame::NoFrame);
    v->setIconSize(QSize(26, 16));
    v->setSortingEnabled(true);
    v->setAccessibleName("Tickets");
    v->verticalHeader()->hide();
    v->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    v->verticalHeader()->setDefaultSectionSize(28);
    auto *h = v->horizontalHeader();
    h->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    h->setHighlightSections(false);
    h->setSectionResizeMode(QHeaderView::Interactive);
    h->setSectionResizeMode(IssueListModel::Summary, QHeaderView::Stretch);
    h->setMinimumSectionSize(36);
    h->resizeSection(IssueListModel::Key, 108);
    h->resizeSection(IssueListModel::Priority, 44);
    h->resizeSection(IssueListModel::Status, 124);
    h->resizeSection(IssueListModel::Assignee, 104);
    h->resizeSection(IssueListModel::Account, 88);
    h->resizeSection(IssueListModel::Updated, 104);
    h->resizeSection(IssueListModel::Type, 90);
    h->resizeSection(IssueListModel::Reporter, 120);
    h->resizeSection(IssueListModel::Project, 80);
    h->resizeSection(IssueListModel::Labels, 140);
    h->resizeSection(IssueListModel::Created, 96);
    v->sortByColumn(IssueListModel::Updated, Qt::DescendingOrder);

    // Columns: right click on the header, remembered. "Konto" follows the account selection.
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
            if (c == IssueListModel::Key || c == IssueListModel::Summary || c == IssueListModel::Account)
                continue;
            QAction *a = menu.addAction(IssueListModel::columnName(c));
            a->setCheckable(true);
            a->setChecked(!h->isSectionHidden(c));
            connect(a, &QAction::toggled, this, [h, c](bool on) {
                h->setSectionHidden(c, !on);
                QStringList names;
                for (int i = 0; i < IssueListModel::ColumnCount; ++i)
                    if (i != IssueListModel::Account && h->isSectionHidden(i))
                        names << IssueListModel::columnName(i);
                QSettings().setValue("list/hiddenColumns", names);
            });
        }
        menu.exec(h->mapToGlobal(pos));
    });
    connect(v->selectionModel(), &QItemSelectionModel::currentRowChanged, this, &MainWindow::selectionChanged);
}

// Accounts --------------------------------------------------------------------------

void MainWindow::loadAccounts()
{
    m_scopeId = QSettings().value("ui/account").toString();
    for (const auto &a : AccountStore::load()) {
        if (a.isOAuth())
            addContext(a, oauthSession(a.oauthUser));
        else
            addContext(a, new BasicAuth(a.email, AccountStore::readToken(a.id), this));
    }
    if (!context(m_scopeId))
        m_scopeId.clear();
    rebuildAccountBox();
    switchScope(m_accountBox->currentIndex());

    // Tokens used to be in the OS keychain -> enter once again.
    for (AccountContext *ctx : std::as_const(m_contexts)) {
        const Account &a = ctx->account();
        if (!a.isOAuth() && AccountStore::readToken(a.id).isEmpty()) {
            showAccountPanel();
            m_accountPanel->showManage(a.id);
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
        statusBar()->showMessage(
            QString::fromUtf8("Anmeldung für %1 abgelaufen – „Datei → Konto hinzufügen → Mit Atlassian anmelden“")
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
    m_scopeId = account.id;
    rebuildAccountBox();
    switchScope(m_accountBox->currentIndex());
}

void MainWindow::updateAccount(const Account &account, const QString &token)
{
    auto accounts = AccountStore::load();
    for (auto &a : accounts)
        if (a.id == account.id)
            a = account;
    AccountStore::save(accounts);
    if (AccountContext *ctx = context(account.id)) {
        if (auto *basic = qobject_cast<BasicAuth *>(ctx->auth()))
            basic->setCredentials(account.email, token.isEmpty() ? AccountStore::readToken(account.id) : token);
        ctx->update(account);  // changed scope -> full resync
    }
    rebuildAccountBox();
    switchScope(m_accountBox->currentIndex());
}

void MainWindow::removeAccount(const QString &accountId)
{
    AccountContext *ctx = context(accountId);
    if (!ctx)
        return;
    if (m_issue->context() == ctx)
        m_issue->showIssue(nullptr, {});
    m_contexts.removeOne(ctx);
    const QString dbPath = AccountStore::dataDir() + "/" + accountId + ".db";
    delete ctx;  // closes the db
    for (const char *suffix : {"", "-wal", "-shm"})
        QFile::remove(dbPath + suffix);
    AccountStore::removeToken(accountId);
    auto accounts = AccountStore::load();
    accounts.removeIf([&](const Account &a) { return a.id == accountId; });
    AccountStore::save(accounts);
    if (m_scopeId == accountId)
        m_scopeId.clear();
    m_accountPanel->setCancelable(!m_contexts.isEmpty());
    rebuildAccountBox();
    switchScope(m_accountBox->currentIndex());
    statusBar()->showMessage("Konto entfernt.", 5000);
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
    connect(&ctx->sync(), &SyncEngine::statusChanged, this, &MainWindow::updateStatus);
    connect(&ctx->sync(), &SyncEngine::dataChanged, this, [this, ctx] {
        if (scope().contains(ctx) || m_issue->context() == ctx)
            m_reloadTimer.start();
    });
    ctx->sync().start();
}

void MainWindow::rebuildAccountBox()
{
    QSignalBlocker b(m_accountBox);
    m_accountBox->clear();
    m_accountBox->addItem("Alle Konten", QString());
    for (AccountContext *ctx : std::as_const(m_contexts))
        m_accountBox->addItem(ctx->account().label(), ctx->account().id);
    const int i = m_accountBox->findData(m_scopeId);
    m_accountBox->setCurrentIndex(i < 0 ? 0 : i);
}

void MainWindow::switchScope(int index)
{
    m_scopeId = m_accountBox->itemData(index).toString();
    QSettings().setValue("ui/account", m_scopeId);
    AccountContext *ctx = context(m_scopeId);
    setWindowTitle(ctx ? "Ticketing UI – " + ctx->account().name : QStringLiteral("Ticketing UI"));
    m_table->horizontalHeader()->setSectionHidden(IssueListModel::Account, !m_scopeId.isEmpty());
    ++m_searchGen;
    m_cloud.clear();
    m_cloudInfo.clear();
    reloadList();
    updateStatus();
}

QList<AccountContext *> MainWindow::scope() const
{
    if (m_scopeId.isEmpty())
        return m_contexts;
    AccountContext *ctx = context(m_scopeId);
    return ctx ? QList<AccountContext *>{ctx} : QList<AccountContext *>{};
}

AccountContext *MainWindow::context(const QString &accountId) const
{
    for (AccountContext *ctx : m_contexts)
        if (ctx->account().id == accountId)
            return ctx;
    return nullptr;
}

AccountContext *MainWindow::currentContext() const
{
    if (AccountContext *ctx = context(m_scopeId))
        return ctx;
    return m_issue->context() ? m_issue->context() : m_contexts.value(0);
}

// List ------------------------------------------------------------------------------

void MainWindow::reloadList()
{
    QElapsedTimer t;
    t.start();
    const QString q = m_search->text().trimmed();
    const bool hideDone = m_hideDoneAct->isChecked();
    const bool mine = interestOnly();
    QList<IssueRow> rows;
    for (AccountContext *ctx : scope()) {
        // Filter in the query, not in the view: otherwise the limit fills up with hidden rows.
        QList<IssueRow> part = q.isEmpty() ? ctx->db().localIssues(kViewLimit, hideDone, mine ? ctx->account().interest : QStringList())
                                           : ctx->db().search(q, kSearchLimit, hideDone);
        for (IssueRow &r : part)
            r.accountId = ctx->account().id;
        rows << part;
    }
    m_rows = rows;
    m_localMs = t.nsecsElapsed() / 1e6;
    m_cloudBar->setVisible(!q.isEmpty());

    applyRows();
}

void MainWindow::runCloudSearch()
{
    const QString q = m_search->text().trimmed();
    if (q.isEmpty()) {
        m_search->setFocus();
        return;
    }
    const quint64 gen = ++m_searchGen;
    const auto contexts = scope();
    auto pending = std::make_shared<int>(int(contexts.size()));
    auto failed = std::make_shared<int>(0);
    auto timer = std::make_shared<QElapsedTimer>();
    timer->start();
    m_cloud.clear();
    m_cloudInfo = QString::fromUtf8("Suche in der Cloud …");
    updateListHeader();
    for (AccountContext *ctx : contexts) {
        ctx->searchCloud(q, [this, gen, ctx, pending, failed, timer](QList<IssueRow> rows, const QString &err) {
            if (gen != m_searchGen)
                return;  // outdated
            if (!err.isEmpty())
                ++*failed;
            for (IssueRow &r : rows) {
                r.accountId = ctx->account().id;
                r.cloud = true;
            }
            m_cloud << rows;
            if (--*pending > 0)
                return;
            m_cloudInfo = *failed ? QString::fromUtf8("Cloud nicht erreichbar")
                                  : QString::fromUtf8("Cloud: %1 weitere (%2 ms)").arg(m_cloud.size()).arg(timer->elapsed());
            applyRows();
        });
    }
}

void MainWindow::applyRows()
{
    QList<IssueRow> merged = m_rows;
    QSet<QString> keys;
    for (const auto &r : m_rows)
        keys.insert(r.accountId + '\n' + r.key);
    for (const IssueRow &r : m_cloud)
        if (!keys.contains(r.accountId + '\n' + r.key))
            merged << r;

    QHash<QString, QString> names;
    for (AccountContext *ctx : std::as_const(m_contexts))
        names.insert(ctx->account().id, ctx->account().name);
    m_model->setAccountNames(names);
    m_model->setRows(merged);
    reselect();
    updateListHeader();
}

bool MainWindow::interestOnly()
{
    bool available = false;
    for (AccountContext *ctx : scope())
        available |= !ctx->account().interest.isEmpty();
    available &= m_search->text().trimmed().isEmpty();  // the search covers everything
    m_interestOnly->setVisible(available);
    return available && m_interestOnly->isChecked();
}

void MainWindow::updateListHeader()
{
    const QString q = m_search->text().trimmed();
    QString title = QString::fromUtf8("Suche „%1“").arg(q);
    if (q.isEmpty()) {
        AccountContext *ctx = context(m_scopeId);
        const QString what = m_interestOnly->isHidden() || !m_interestOnly->isChecked() ? "Alle Tickets" : "Meine Projekte";
        title = ctx ? ctx->account().name + " · " + what : what;
    }
    m_listTitle->setText(title);

    const int shown = m_proxy->rowCount();
    int unread = 0;
    for (int r = 0; r < shown; ++r)  // only what the list shows
        unread += m_model->rowAt(m_proxy->mapToSource(m_proxy->index(r, 0)).row())->unread ? 1 : 0;
    QString sub = q.isEmpty() ? QString("%1 Tickets").arg(shown) : QString("%1 Treffer").arg(shown);
    if (unread)
        sub += QString(" · %1 ungelesen").arg(unread);
    m_listSub->setText(sub);
    m_listStack->setCurrentIndex(shown > 0 ? 0 : 1);
    m_cloudInfoLabel->setText(m_cloudInfo.isEmpty() ? QString("Lokal: %1 Treffer").arg(m_rows.size()) : m_cloudInfo);
    updateStatus();
}

void MainWindow::updateStatus()
{
    QStringList times;
    bool offline = false;
    for (AccountContext *ctx : std::as_const(m_contexts)) {
        QString s = ctx->sync().status();
        offline |= s.startsWith("Offline");
        if (s.startsWith("Aktuell · "))
            s = s.mid(10);
        times << ctx->account().name + ": " + s;
    }
    const QString color = offline ? "#c53030" : "#2f855a";
    m_connection->setText(QString("<span style='color:%1; font-size:10px'>●</span>&nbsp;%2").arg(color, offline ? "Offline" : "Verbunden"));
    m_connection->setVisible(!m_contexts.isEmpty());
    m_syncTimes->setText(times.join("   "));

    int unread = 0;
    for (AccountContext *ctx : scope())
        unread += ctx->db().unreadCount();
    const QString local = m_search->text().trimmed().isEmpty()
                              ? QString("Lokal: %1").arg(m_rows.size())
                              : QString("Lokal: %1 Treffer (%2 ms)").arg(m_rows.size()).arg(m_localMs, 0, 'f', 1);
    m_counts->setText(QString("%1 ungelesen · %2").arg(unread).arg(local));
}

void MainWindow::selectionChanged()
{
    const QModelIndex cur = m_proxy->mapToSource(m_table->currentIndex());
    const IssueRow *r = m_model->rowAt(cur.row());
    if (!r)
        return;
    m_selAccount = r->accountId;
    m_selKey = r->key;
    m_issue->showIssue(context(r->accountId), r->key);
}

void MainWindow::reselect()
{
    const int row = m_model->rowOf(m_selAccount, m_selKey);
    if (row < 0)
        return;
    const QModelIndex i = m_proxy->mapFromSource(m_model->index(row, 0));
    if (!i.isValid())
        return;
    QSignalBlocker b(m_table->selectionModel());
    m_table->selectionModel()->setCurrentIndex(i, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    m_table->viewport()->update();
}

// Development -----------------------------------------------------------------------

void MainWindow::saveSnapshots(const QString &dir)
{
    resize(1400, 850);
    auto shot = [this, &dir](const QString &name) {
        for (int i = 0; i < 5; ++i)
            QApplication::processEvents();
        grab().save(dir + "/" + name + ".png");
    };
    m_pages->setCurrentIndex(0);
    if (m_accountBox->count() > 1)
        m_accountBox->setCurrentIndex(1);
    if (m_proxy->rowCount() > 0)
        m_table->setCurrentIndex(m_proxy->index(0, 0));
    shot("main-details");
    m_issue->startComment();
    shot("main-kommentare");
    m_accountBox->setCurrentIndex(0);
    if (m_proxy->rowCount() > 0)
        m_table->setCurrentIndex(m_proxy->index(0, 0));
    shot("main-alle-konten");
    m_search->setText("drucker");
    QEventLoop wait;  // clear button fades in
    QTimer::singleShot(400, &wait, &QEventLoop::quit);
    wait.exec();
    shot("main-suche");
    m_search->clear();
    showAccountPanel();
    m_accountPanel->showManage();
    shot("konto-zugang");
    if (auto *tabs = m_accountPanel->findChild<QTabWidget *>())
        tabs->setCurrentIndex(1);
    shot("konto-lokal-speichern");
    if (auto *tabs = m_accountPanel->findChild<QTabWidget *>())
        tabs->setCurrentIndex(2);
    shot("konto-anzeigen");
}
