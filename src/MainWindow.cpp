#include "MainWindow.h"
#include "Account.h"
#include "AccountContext.h"
#include "AccountDialog.h"
#include "Auth.h"
#include "IssueListModel.h"
#include "SyncEngine.h"

#include <algorithm>
#include <QComboBox>
#include <QDateTime>
#include <QElapsedTimer>
#include <QHeaderView>
#include <QLabel>
#include <QJsonArray>
#include <QJsonObject>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QMessageBox>
#include <QScrollBar>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableView>
#include <QTextBrowser>
#include <QToolBar>
#include <QToolButton>
#include <QUuid>
#include <QVBoxLayout>

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
    addMenu->addAction("Mit Atlassian anmelden (Browser) …", this, &MainWindow::oauthLogin);
    addMenu->addAction("Mit API-Token …", this, &MainWindow::addAccountDialog);
    auto *addBtn = new QToolButton;
    addBtn->setText("+ Konto");
    addBtn->setMenu(addMenu);
    addBtn->setPopupMode(QToolButton::InstantPopup);
    tb->addWidget(addBtn);

    tb->addSeparator();
    m_search = new QLineEdit;
    m_search->setPlaceholderText("Suchen …  (Strg+L)   Key, Text, Label – leer = meine offenen Issues");
    m_search->setClearButtonEnabled(true);
    tb->addWidget(m_search);
    auto *syncAct = tb->addAction("Sync", this, [this] { if (m_ctx) m_ctx->sync().syncNow(); });
    syncAct->setShortcut(QKeySequence("Ctrl+R"));

    // Left: search results + sprint view, right: detail -------------------------
    m_resultModel = new IssueListModel(this);
    m_resultView = makeTable(m_resultModel);

    auto *sprintPage = new QWidget;
    auto *sprintLayout = new QVBoxLayout(sprintPage);
    sprintLayout->setContentsMargins(0, 4, 0, 0);
    m_sprintBox = new QComboBox;
    m_sprintModel = new IssueListModel(this);
    m_sprintView = makeTable(m_sprintModel);
    sprintLayout->addWidget(m_sprintBox);
    sprintLayout->addWidget(m_sprintView);

    m_tabs = new QTabWidget;
    m_tabs->addTab(m_resultView, "Suche");
    m_tabs->addTab(sprintPage, "Sprint");

    m_detail = new QTextBrowser;
    m_detail->setOpenExternalLinks(true);

    auto *split = new QSplitter;
    split->addWidget(m_tabs);
    split->addWidget(m_detail);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    setCentralWidget(split);

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
        m_tabs->setCurrentIndex(0);
        m_resultView->setFocus();
        if (m_resultModel->rowCount() > 0 && !m_resultView->currentIndex().isValid())
            m_resultView->setCurrentIndex(m_resultModel->index(0, 0));
    });
    connect(m_sprintBox, &QComboBox::currentIndexChanged, this, &MainWindow::showSprint);

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
        m_tabs->setCurrentIndex(1 - m_tabs->currentIndex());
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
    v->setAlternatingRowColors(true);
    v->setShowGrid(false);
    v->setWordWrap(false);
    v->verticalHeader()->hide();
    v->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    v->verticalHeader()->setDefaultSectionSize(v->fontMetrics().height() + 8);
    auto *h = v->horizontalHeader();
    h->setSectionResizeMode(QHeaderView::Interactive);
    h->setSectionResizeMode(IssueListModel::Summary, QHeaderView::Stretch);
    h->resizeSection(IssueListModel::Key, 110);
    h->resizeSection(IssueListModel::Type, 80);
    h->resizeSection(IssueListModel::Status, 120);
    h->resizeSection(IssueListModel::Assignee, 140);
    h->resizeSection(IssueListModel::Updated, 110);
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
    if (m_contexts.isEmpty())
        QTimer::singleShot(0, this, &MainWindow::firstRun);
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

void MainWindow::firstRun()
{
    QMessageBox box(QMessageBox::Question, "Jira Client", "Wie möchtest du dich bei Jira Cloud anmelden?",
                    QMessageBox::NoButton, this);
    QPushButton *oauth = box.addButton("Mit Atlassian anmelden (Browser)", QMessageBox::AcceptRole);
    QPushButton *token = box.addButton("Mit API-Token", QMessageBox::ActionRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(oauth);
    box.exec();
    if (box.clickedButton() == oauth)
        oauthLogin();
    else if (box.clickedButton() == token)
        addAccountDialog();
}

void MainWindow::oauthLogin()
{
    auto *login = new OAuthLogin(this);
    QString err;
    if (!login->start(&err)) {
        delete login;
        QMessageBox::warning(this, "Atlassian-Anmeldung", err);
        return;
    }
    statusBar()->showMessage(QString::fromUtf8(
        "Browser geöffnet – bei Atlassian anmelden und „Zulassen“ klicken. "
        "Anderes Konto (z. B. Kunden-Adresse)? Im Browser oben rechts Konto wechseln oder privates Fenster nutzen."));

    connect(login, &OAuthLogin::failed, this, [this](const QString &e) {
        statusBar()->clearMessage();
        QMessageBox::warning(this, "Atlassian-Anmeldung", e);
    });
    connect(login, &OAuthLogin::succeeded, this,
            [this](const QJsonObject &tokens, const OAuthIdentity &who, const QJsonArray &sites) {
        statusBar()->clearMessage();
        raise();
        activateWindow();
        OAuthSession *session = oauthSession(who.accountId);
        session->setTokens(tokens);

        // Re-login of a known identity: its sites continue with the new tokens.
        const QList<AccountContext *> existing = m_contexts;
        for (AccountContext *ctx : existing)
            if (ctx->account().isOAuth() && ctx->account().oauthUser == who.accountId)
                ctx->sync().syncNow();

        if (sites.isEmpty()) {
            QMessageBox::information(this, "Atlassian-Anmeldung",
                                     "Angemeldet als " + who.email +
                                         ", aber für dieses Konto ist keine Jira-Site freigegeben.");
            return;
        }
        auto accounts = AccountStore::load();
        int added = 0;
        for (const auto &v : sites) {
            const auto site = v.toObject();
            const QString cloudId = site["id"].toString();
            const bool known = std::any_of(accounts.begin(), accounts.end(), [&](const Account &a) {
                return a.isOAuth() && a.cloudId == cloudId && a.oauthUser == who.accountId;
            });
            if (known)
                continue;
            AccountDialog dlg(site["name"].toString(), site["url"].toString(), who.email, this);
            if (dlg.exec() != QDialog::Accepted)
                continue;
            Account a = dlg.account();
            a.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            a.cloudId = cloudId;
            a.baseUrl = site["url"].toString();
            a.oauthUser = who.accountId;
            a.email = who.email;
            accounts << a;
            AccountStore::save(accounts);
            addContext(a, session);
            ++added;
        }
        if (added)
            switchAccount(int(m_contexts.size()) - 1);
    });
}

void MainWindow::addContext(const Account &account, AuthProvider *auth)
{
    auto *ctx = new AccountContext(account, auth, this);
    QString err;
    if (!ctx->open(&err)) {
        QMessageBox::warning(this, "Datenbank", account.name + ": " + err);
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

void MainWindow::addAccountDialog()
{
    AccountDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    Account a = dlg.account();
    a.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString err;
    if (!AccountStore::writeToken(a.id, dlg.token(), &err)) {
        QMessageBox::warning(this, "Schlüsselbund", "Token konnte nicht gespeichert werden: " + err);
        return;
    }
    auto accounts = AccountStore::load();
    accounts << a;
    AccountStore::save(accounts);
    addContext(a, new BasicAuth(a.email, dlg.token(), this));
    switchAccount(int(m_contexts.size()) - 1);
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

    QString h;
    h += QString("<h2><a href=\"%1\">%2</a> %3</h2>")
             .arg(m_ctx->browseUrl(key).toHtmlEscaped(), key.toHtmlEscaped(), d->summary.toHtmlEscaped());
    h += "<table cellspacing='0' cellpadding='2'>";
    auto row = [&h](const QString &label, const QString &value) {
        if (!value.isEmpty())
            h += "<tr><td><font color='gray'>" + label + "</font></td><td>" + value.toHtmlEscaped() + "</td></tr>";
    };
    row("Status", d->status);
    row("Typ", d->issueType);
    row("Priorität", d->priority);
    row("Zugewiesen", d->assignee.isEmpty() ? QStringLiteral("—") : d->assignee);
    row("Melder", d->reporter);
    row("Labels", d->labels);
    row("Erstellt", fmtTime(d->created));
    row("Geändert", fmtTime(d->updated));
    if (!d->inScope)
        row("Quelle", QString::fromUtf8("Cache (außerhalb des Sync-Bereichs)"));
    h += "</table><hr>";
    h += d->descriptionHtml.isEmpty() ? QString("<p><i>Keine Beschreibung</i></p>") : d->descriptionHtml;

    const auto comments = m_ctx->db().comments(key);
    const bool fetched = m_ctx->db().commentsFetchedAt(key) > 0;
    h += QString("<h3>Kommentare (%1)</h3>").arg(fetched ? QString::number(comments.size()) : QStringLiteral("…"));
    if (!fetched)
        h += QString::fromUtf8("<p><i>Lade Kommentare …</i></p>");
    for (const auto &c : comments) {
        h += "<p><b>" + c.author.toHtmlEscaped() + "</b> <font color='gray'>" + fmtTime(c.created) +
             "</font></p>" + c.html + "<hr>";
    }

    m_detail->setHtml(h);
    if (sameIssue)
        m_detail->verticalScrollBar()->setValue(scroll);
    m_renderedKey = key;
}
