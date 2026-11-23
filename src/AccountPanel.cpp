#include "AccountPanel.h"
#include "Backend.h"
#include "ProjectChecklist.h"
#include "Theme.h"

#include <algorithm>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTabWidget>
#include <QToolButton>
#include <QUuid>
#include <QVBoxLayout>

static QLabel *text(const QString &t = {})
{
    auto *l = new QLabel(t);
    l->setWordWrap(true);
    l->setTextInteractionFlags(Qt::TextBrowserInteraction);
    l->setOpenExternalLinks(true);
    return l;
}

static QLabel *muted(const QString &t = {})
{
    QLabel *l = text(t);
    l->setForegroundRole(QPalette::PlaceholderText);
    return l;
}

static QHBoxLayout *buttonRow(std::initializer_list<QPushButton *> buttons, bool right = false)
{
    auto *row = new QHBoxLayout;
    if (right)
        row->addStretch();
    for (auto *b : buttons)
        row->addWidget(b);
    if (!right)
        row->addStretch();
    return row;
}

// Looks like a dialog (title bar, body, footer) but lives in the main window.
static QFrame *dialogFrame(const QString &titleText, QLayout *body, QLayout *footer, QObject *receiver,
                           std::function<void()> close)
{
    auto *frame = new QFrame;
    frame->setObjectName("dialog");
    auto *v = new QVBoxLayout(frame);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    auto *bar = new QWidget;
    bar->setObjectName("dialogTitle");
    bar->setAttribute(Qt::WA_StyledBackground);
    auto *bl = new QHBoxLayout(bar);
    bl->setContentsMargins(14, 6, 8, 6);
    auto *t = new QLabel(titleText);
    QFont f = t->font();
    f.setBold(true);
    t->setFont(f);
    auto *x = new QToolButton;
    x->setObjectName("closeButton");
    x->setIcon(Theme::icon(Theme::Icon::Close));
    x->setAccessibleName("Schließen");
    x->setToolTip("Schließen (Esc)");
    QObject::connect(x, &QToolButton::clicked, receiver, close);
    bl->addWidget(t);
    bl->addStretch();
    bl->addWidget(x);
    v->addWidget(bar);

    body->setContentsMargins(12, 12, 12, 12);
    v->addLayout(body, 1);
    if (footer) {
        auto *foot = new QWidget;
        foot->setObjectName("dialogFooter");
        foot->setAttribute(Qt::WA_StyledBackground);
        foot->setLayout(footer);
        footer->setContentsMargins(12, 10, 12, 12);
        v->addWidget(foot);
    }
    return frame;
}

// The only backend so far; the type selector in the form is prepared for more.
static const BackendType &formType()
{
    return BackendType::jira();
}

AccountPanel::AccountPanel(QWidget *parent) : QWidget(parent)
{
    m_stack = new QStackedWidget;
    m_stack->addWidget(m_choicePage = buildChoice());
    m_stack->addWidget(m_loginPage = buildLogin());
    m_stack->addWidget(m_managePage = buildManage());

    // Centered "dialog" on a slightly darker backdrop.
    setAttribute(Qt::WA_StyledBackground);
    auto *h = new QHBoxLayout;
    h->addStretch(1);
    h->addWidget(m_stack, 8);
    h->addStretch(1);
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(16, 40, 16, 24);
    v->addLayout(h);
    v->addStretch();
    connect(m_stack, &QStackedWidget::currentChanged, this, [this] {
        m_stack->setMaximumWidth(m_stack->currentWidget() == m_managePage ? 920 : 620);
    });

    auto *esc = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    esc->setContext(Qt::WidgetWithChildrenShortcut);
    connect(esc, &QShortcut::activated, this, &AccountPanel::cancelCurrent);
}

void AccountPanel::setCancelable(bool on)
{
    m_cancelable = on;
    m_choiceCancel->setVisible(on);
}

// Pages -----------------------------------------------------------------------------

QWidget *AccountPanel::buildChoice()
{
    auto *l = new QVBoxLayout;
    l->setSpacing(12);
    l->addWidget(text("Wie möchtest du dich bei Jira Cloud anmelden?"));
    auto *token = new QPushButton("Mit API-Token");
    auto *oauth = new QPushButton("Mit Atlassian anmelden (Browser)");
    l->addLayout(buttonRow({token, oauth}));
    l->addWidget(muted("<small>API-Token funktioniert überall. Die Browser-Anmeldung setzt voraus, "
                       "dass die Firma die OAuth-App zulässt.</small>"));
    m_choiceCancel = new QPushButton("Abbrechen");
    auto *page = dialogFrame("Konto hinzufügen", l, buttonRow({m_choiceCancel}, true), this,
                             [this] { cancelCurrent(); });
    page->setMaximumWidth(620);
    connect(token, &QPushButton::clicked, this, &AccountPanel::showTokenForm);
    connect(oauth, &QPushButton::clicked, this, &AccountPanel::startOAuth);
    connect(m_choiceCancel, &QPushButton::clicked, this, &AccountPanel::cancelCurrent);
    return page;
}

QWidget *AccountPanel::buildLogin()
{
    auto *l = new QVBoxLayout;
    m_loginText = text();
    l->addWidget(m_loginText);

    m_linkBox = new QWidget;
    auto *lb = new QVBoxLayout(m_linkBox);
    lb->setContentsMargins(0, 8, 0, 0);
    lb->addWidget(text("Falscher Browser oder falsches Atlassian-Konto? Link kopieren und im richtigen Browser "
                       "(oder in einem privaten Fenster) öffnen. Diese Anmeldung bleibt bis zum Abschluss aktiv."));
    m_link = new QLineEdit;
    m_link->setReadOnly(true);
    lb->addWidget(m_link);
    auto *copy = new QPushButton("Link kopieren");
    auto *open = new QPushButton("Im Browser öffnen");
    lb->addLayout(buttonRow({copy, open}));
    l->addWidget(m_linkBox);

    m_restart = new QPushButton("Neu starten");
    auto *cancel = new QPushButton("Abbrechen");
    auto *page = dialogFrame("Anmeldung bei Atlassian", l, buttonRow({m_restart, cancel}, true), this,
                             [this] { cancelCurrent(); });
    page->setMaximumWidth(620);

    connect(copy, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(m_link->text()); });
    connect(open, &QPushButton::clicked, this, [this] { QDesktopServices::openUrl(QUrl(m_link->text())); });
    connect(m_restart, &QPushButton::clicked, this, &AccountPanel::startOAuth);
    connect(cancel, &QPushButton::clicked, this, &AccountPanel::cancelCurrent);
    return page;
}

QWidget *AccountPanel::buildManage()
{
    // Left: accounts -----------------------------------------------------------------
    m_list = new QListWidget;
    m_list->setObjectName("accounts");
    m_list->setItemDelegate(new AccountItemDelegate(m_list));
    m_list->setMinimumHeight(300);
    m_add = new QPushButton(Theme::icon(Theme::Icon::Plus), "Hinzufügen");
    m_remove = new QPushButton("Entfernen");
    auto *left = new QVBoxLayout;
    left->setContentsMargins(0, 0, 0, 0);
    left->setSpacing(8);
    left->addWidget(m_list, 1);
    auto *listButtons = new QHBoxLayout;
    listButtons->setSpacing(8);
    listButtons->addWidget(m_add);
    listButtons->addWidget(m_remove);
    left->addLayout(listButtons);
    auto *leftBox = new QWidget;
    leftBox->setLayout(left);
    leftBox->setFixedWidth(240);

    // Right: editor ------------------------------------------------------------------
    m_tabs = new QTabWidget;
    m_tabs->addTab(buildAccessTab(), "Zugang");
    m_tabs->addTab(buildScopeTab(), "Lokal speichern");
    m_tabs->addTab(buildViewTab(), "Anzeigen");

    auto *row = new QHBoxLayout;
    row->setSpacing(12);
    row->addWidget(leftBox);
    row->addWidget(m_tabs, 1);

    m_formError = text();
    m_formError->setStyleSheet("color: #c53030");
    m_formCancel = new QPushButton("Abbrechen");
    m_submit = new QPushButton("Speichern");
    m_submit->setDefault(true);
    Theme::setPrimary(m_submit);
    auto *footer = new QHBoxLayout;
    footer->setSpacing(8);
    footer->addWidget(m_formError, 1);
    footer->addWidget(m_formCancel);
    footer->addWidget(m_submit);

    auto *page = dialogFrame("Konten verwalten", row, footer, this, [this] { cancelCurrent(); });
    page->setMaximumWidth(920);

    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0 || m_mode != Mode::Edit)
            return;
        const QString id = m_list->item(row)->data(Qt::UserRole).toString();
        if (id == m_account.id)
            return;
        for (const auto &a : AccountStore::load())
            if (a.id == id)
                edit(Mode::Edit, a);
    });
    connect(m_add, &QPushButton::clicked, this, &AccountPanel::showChoice);
    connect(m_remove, &QPushButton::clicked, this, &AccountPanel::removeCurrent);
    connect(m_submit, &QPushButton::clicked, this, &AccountPanel::submitForm);
    connect(m_formCancel, &QPushButton::clicked, this, &AccountPanel::cancelCurrent);
    return page;
}

QWidget *AccountPanel::buildAccessTab()
{
    const BackendType &type = formType();
    auto *page = new QWidget;
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(18, 18, 18, 18);

    m_form = new QFormLayout;
    m_form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_type = new QComboBox;
    m_type->addItem(type.name, type.id);
    m_name = new QLineEdit;
    m_url = new QLineEdit;
    m_url->setPlaceholderText(type.urlPlaceholder);
    m_email = new QLineEdit;
    m_token = new QLineEdit;
    m_token->setEchoMode(QLineEdit::Password);
    m_tokenHint = muted("<small>" + type.secretHint + "</small>");
    m_oauthInfo = muted("<small>Angemeldet über das Atlassian-Konto im Browser. Abgelaufene Anmeldungen erneuerst du "
                        "über „Hinzufügen → Mit Atlassian anmelden“.</small>");
    m_form->addRow("Typ", m_type);
    m_form->addRow("Name", m_name);
    m_form->addRow("URL", m_url);
    m_form->addRow("E-Mail", m_email);
    m_form->addRow(type.secretLabel, m_token);
    m_form->addRow(QString(), m_tokenHint);
    m_form->addRow(QString(), m_oauthInfo);
    v->addLayout(m_form);
    v->addSpacing(8);
    auto *line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet(QString("color: %1").arg(Theme::line().name()));
    v->addWidget(line);
    v->addSpacing(4);

    m_test = new QPushButton("Verbindung testen");
    m_loginCheck = muted("Noch nicht getestet");
    auto *test = new QHBoxLayout;
    test->setSpacing(12);
    test->addWidget(m_test);
    test->addWidget(m_loginCheck, 1);
    v->addLayout(test);
    v->addStretch();

    connect(m_test, &QPushButton::clicked, this, [this] {
        m_checkedFor.clear();
        checkLogin();
    });
    // Check the login as soon as the credentials are complete.
    for (QLineEdit *e : {m_url, m_email, m_token}) {
        connect(e, &QLineEdit::editingFinished, this, &AccountPanel::checkLogin);
        connect(e, &QLineEdit::textEdited, this, [this] { setLoginCheck("Noch nicht getestet"); });
    }
    for (QLineEdit *e : {m_name, m_url, m_email, m_token})
        connect(e, &QLineEdit::returnPressed, this, &AccountPanel::submitForm);
    return page;
}

QWidget *AccountPanel::buildScopeTab()
{
    const BackendType &type = formType();
    auto *page = new QWidget;
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(18, 18, 18, 18);
    v->setSpacing(8);
    v->addWidget(muted("Diese Tickets werden lokal gespeichert und sind offline und sofort durchsuchbar. Alles andere "
                       "findet die Suche weiterhin in der Cloud und lädt es beim Öffnen."));
    v->addSpacing(4);

    // Projects + age
    m_simpleScope = new QWidget;
    auto *simple = new QVBoxLayout(m_simpleScope);
    simple->setContentsMargins(0, 0, 0, 0);
    simple->setSpacing(6);
    m_scopeProjects = new ProjectChecklist("Projekte", "Keins gewählt = alle Projekte");
    simple->addWidget(m_scopeProjects, 1);
    simple->addSpacing(6);

    m_age = new QComboBox;
    for (const auto &[days, label] : {std::pair{30, "in den letzten 30 Tagen"}, {90, "in den letzten 90 Tagen"},
                                      {365, "in den letzten 12 Monaten"}, {730, "in den letzten 2 Jahren"},
                                      {0, "egal wann"}})
        m_age->addItem(label, days);
    auto *ageRow = new QHBoxLayout;
    ageRow->setSpacing(8);
    auto *ageLabel = new QLabel("<b>Geändert</b>");
    ageLabel->setBuddy(m_age);
    ageRow->addWidget(ageLabel);
    ageRow->addWidget(m_age);
    ageRow->addStretch();
    simple->addLayout(ageRow);
    v->addWidget(m_simpleScope, 1);

    // Own query instead
    v->addSpacing(6);
    m_ownQuery = new QCheckBox("Stattdessen eigene " + type.queryLabel + " verwenden");
    v->addWidget(m_ownQuery);
    m_query = new QPlainTextEdit;
    m_query->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_query->setTabChangesFocus(true);
    m_query->setFixedHeight(m_query->fontMetrics().lineSpacing() * 3 + 14);
    v->addWidget(m_query);
    m_scopePreview = muted();
    m_scopePreview->setTextInteractionFlags(Qt::TextSelectableByMouse);
    v->addWidget(m_scopePreview);

    m_scopeProjects->changed = [this] { updateScope(); };
    connect(m_age, &QComboBox::currentIndexChanged, this, &AccountPanel::updateScope);
    connect(m_query, &QPlainTextEdit::textChanged, this, &AccountPanel::updateScope);
    connect(m_ownQuery, &QCheckBox::toggled, this, [this](bool on) {
        if (on && m_query->toPlainText().trimmed().isEmpty()) {
            Account a = m_account;
            readScope(a);
            a.query.clear();
            m_query->setPlainText(formType().scopeQuery(a));  // start from what projects + age give
        }
        updateScope();
        if (on)
            m_query->setFocus();
    });
    return page;
}

QWidget *AccountPanel::buildViewTab()
{
    auto *page = new QWidget;
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(18, 18, 18, 18);
    v->setSpacing(8);
    v->addWidget(muted("Was dich grundsätzlich interessiert. Mit „Nur meine Projekte“ zeigt die Liste nur diese "
                       "Projekte; die Suche findet weiterhin alle lokal gespeicherten Tickets."));
    v->addSpacing(4);
    m_interestProjects = new ProjectChecklist("Meine Projekte", "Keins gewählt = alle anzeigen");
    v->addWidget(m_interestProjects, 1);
    return page;
}

void AccountPanel::loadProjects(Backend *backend)
{
    const int request = ++m_projectsRequest;
    const QString status = backend ? "Lade Projekte …" : "Die Projektliste erscheint, sobald die Verbindung steht.";
    m_scopeProjects->setStatus(status);
    m_interestProjects->setStatus(status);
    if (!backend)
        return;
    backend->fetchProjects([this, request](const QList<ProjectInfo> &all, const QString &err) {
        if (request != m_projectsRequest)
            return;
        const QString status = err.isEmpty() ? QString() : "Projekte konnten nicht geladen werden: " + err;
        for (ProjectChecklist *list : {m_scopeProjects, m_interestProjects}) {
            list->setStatus(status);
            if (err.isEmpty())
                list->setProjects(all);
        }
    });
}

void AccountPanel::readScope(Account &a) const
{
    a.projects = m_scopeProjects->checked();
    a.maxAgeDays = m_age->currentData().toInt();
    a.query = m_ownQuery->isChecked() ? m_query->toPlainText().trimmed() : QString();
}

void AccountPanel::updateScope()
{
    const bool own = m_ownQuery->isChecked();
    m_simpleScope->setEnabled(!own);
    m_query->setVisible(own);
    Account a = m_account;
    readScope(a);
    m_scopePreview->setText(own ? QString() : "<small>" + formType().queryLabel + ": <code>" +
                                                  formType().scopeQuery(a).toHtmlEscaped() + "</code></small>");
    m_scopePreview->setVisible(!own);
}

// Flow ------------------------------------------------------------------------------

void AccountPanel::showChoice()
{
    stopLogin();
    m_stack->setCurrentWidget(m_choicePage);
}

void AccountPanel::showTokenForm()
{
    stopLogin();
    edit(Mode::NewToken, {});
}

void AccountPanel::showManage(const QString &accountId)
{
    stopLogin();
    const auto accounts = AccountStore::load();
    if (accounts.isEmpty()) {
        showChoice();
        return;
    }
    Account a = accounts.first();
    for (const auto &acc : accounts)
        if (acc.id == accountId)
            a = acc;
    edit(Mode::Edit, a);
}

void AccountPanel::startOAuth()
{
    stopLogin();
    m_pendingSites = {};
    m_stack->setCurrentWidget(m_loginPage);

    auto *login = new OAuthLogin(this);
    QString err;
    if (!login->start(&err)) {
        delete login;
        setLoginText(err, true);
        return;
    }
    m_login = login;
    m_link->setText(login->url().toString(QUrl::FullyEncoded));
    m_link->setCursorPosition(0);
    setLoginText("Browser geöffnet – bei Atlassian anmelden und „Zulassen“ klicken.", false);
    QDesktopServices::openUrl(login->url());

    connect(login, &OAuthLogin::callbackReceived, this, [this] {
        setLoginText("Antwort vom Browser erhalten – Konto wird geladen …", false);
        m_linkBox->hide();
    });
    connect(login, &OAuthLogin::failed, this, [this](const QString &e) { setLoginText(e, true); });
    connect(login, &OAuthLogin::succeeded, this,
            [this](const QJsonObject &tokens, const OAuthIdentity &who, const QJsonArray &sites) {
        emit oauthLoggedIn(tokens, who);
        if (sites.isEmpty()) {
            setLoginText("Angemeldet als " + who.email + ", aber für dieses Konto ist keine Jira-Site freigegeben.",
                         true);
            return;
        }
        m_who = who;
        m_pendingSites = {};
        const auto accounts = AccountStore::load();
        for (const auto &v : sites) {
            const QString cloudId = v.toObject()["id"].toString();
            const bool known = std::any_of(accounts.begin(), accounts.end(), [&](const Account &a) {
                return a.isOAuth() && a.cloudId == cloudId && a.oauthUser == who.accountId;
            });
            if (!known)
                m_pendingSites.append(v);
        }
        nextSite();  // re-login of a known identity: nothing to set up -> done
    });
}

// Login page text; the link stays visible only while the login is still waiting for the browser.
void AccountPanel::setLoginText(const QString &t, bool error)
{
    m_loginText->setText(t.toHtmlEscaped().replace("\n", "<br>"));
    m_loginText->setStyleSheet(error ? "color: #c53030" : "");
    const bool waiting = !error && m_login;
    m_linkBox->setVisible(waiting);
    m_restart->setText(waiting ? "Neu starten" : "Erneut versuchen");
}

void AccountPanel::stopLogin()
{
    if (m_login)
        m_login->cancel();
    m_login = nullptr;
}

void AccountPanel::nextSite()
{
    if (m_pendingSites.isEmpty()) {
        leave();
        return;
    }
    const auto site = m_pendingSites.takeAt(0).toObject();
    Account a;
    a.authType = "oauth";
    a.name = site["name"].toString();
    a.baseUrl = site["url"].toString();
    a.cloudId = site["id"].toString();
    a.oauthUser = m_who.accountId;
    a.email = m_who.email;
    edit(Mode::NewOAuth, a);
}

// Account list + editor -------------------------------------------------------------

void AccountPanel::fillList(const QString &selectId)
{
    QSignalBlocker b(m_list);
    m_list->clear();
    auto add = [this](const QString &id, const QString &name, const QString &tag, const QString &sub) {
        auto *item = new QListWidgetItem(name + "\n" + tag + "\n" + sub, m_list);
        item->setData(Qt::UserRole, id);
        item->setData(Qt::AccessibleTextRole, name + ", " + sub);
        item->setToolTip(sub);
        return item;
    };
    for (const auto &a : AccountStore::load()) {
        const BackendType &type = BackendType::byId(a.backend);
        auto *item = add(a.id, a.name, type.name.section(' ', 0, 0), QUrl(a.baseUrl).host());
        if (a.id == selectId)
            m_list->setCurrentItem(item);
    }
    if (m_mode != Mode::Edit) {  // the one being set up
        auto *item = add({}, m_account.name.isEmpty() ? QStringLiteral("Neues Konto") : m_account.name,
                         formType().name.section(' ', 0, 0), "nicht gespeichert");
        m_list->setCurrentItem(item);
    }
}

void AccountPanel::edit(Mode mode, const Account &account)
{
    m_mode = mode;
    m_account = account;
    const bool token = !account.isOAuth();
    const bool isNew = mode != Mode::Edit;
    fillList(account.id);
    m_list->setEnabled(!isNew);  // finish or cancel the new one first
    m_add->setEnabled(!isNew);
    m_remove->setEnabled(!isNew);
    m_confirmRemove = false;
    m_remove->setText("Entfernen");

    m_type->setEnabled(mode == Mode::NewToken);
    m_name->setText(account.name);
    m_url->setText(account.baseUrl);
    m_url->setReadOnly(mode != Mode::NewToken);  // other URL = other site = new account
    m_email->setText(account.email);
    m_email->setReadOnly(!token);
    m_token->clear();
    const bool tokenMissing = mode == Mode::Edit && token && AccountStore::readToken(account.id).isEmpty();
    m_token->setPlaceholderText(mode != Mode::Edit ? QString() : tokenMissing ? "fehlt – bitte eintragen" : "unverändert");
    m_form->setRowVisible(m_token, token);
    m_form->setRowVisible(m_tokenHint, token);
    m_form->setRowVisible(m_oauthInfo, !token);
    m_test->setVisible(token);
    m_loginCheck->setVisible(token);
    m_checkedFor.clear();
    setLoginCheck(mode == Mode::NewToken ? QString("Noch nicht getestet") : QString());

    {
        QSignalBlocker b1(m_age), b2(m_ownQuery), b3(m_query);
        int age = m_age->findData(account.maxAgeDays);
        if (age < 0) {  // e.g. 180 days from an older setting
            m_age->insertItem(m_age->count() - 1, QString("in den letzten %1 Tagen").arg(account.maxAgeDays),
                              account.maxAgeDays);
            age = m_age->findData(account.maxAgeDays);
        }
        m_age->setCurrentIndex(age);
        m_ownQuery->setChecked(!account.query.isEmpty());
        m_query->setPlainText(account.query);
    }
    m_scopeProjects->setProjects({});
    m_scopeProjects->setChecked(account.projects);
    m_interestProjects->setProjects({});
    m_interestProjects->setChecked(account.interest);
    loadProjects(mode == Mode::Edit && m_backendFor ? m_backendFor(account.id) : nullptr);

    m_submit->setText(isNew ? "Hinzufügen" : "Speichern");
    m_formCancel->setText(mode == Mode::NewOAuth ? "Überspringen" : "Abbrechen");
    m_formError->clear();
    m_tabs->setCurrentIndex(AccessTab);
    m_stack->setCurrentWidget(m_managePage);
    (mode == Mode::NewToken ? m_url : m_name)->setFocus();
    if (mode == Mode::Edit)
        checkLogin();
}

void AccountPanel::removeCurrent()
{
    if (m_mode != Mode::Edit || m_account.id.isEmpty())
        return;
    if (!m_confirmRemove) {  // inline confirmation instead of a dialog
        m_confirmRemove = true;
        m_remove->setText("Wirklich entfernen?");
        m_formError->setText("Entfernt das Konto und seine lokalen Daten von diesem Rechner. In "
                             + BackendType::byId(m_account.backend).name + " selbst ändert sich nichts.");
        return;
    }
    const QString id = m_account.id;
    m_account = {};
    emit accountRemoved(id);
    showManage();
}

// Login check (API-token accounts) ----------------------------------------------------

void AccountPanel::checkLogin()
{
    if (m_stack->currentWidget() != m_managePage || m_account.isOAuth())
        return;
    QString base = m_url->text().trimmed();
    if (!base.isEmpty() && !base.startsWith("http"))
        base.prepend("https://");
    QString token = m_token->text();
    if (token.isEmpty() && m_mode == Mode::Edit)
        token = AccountStore::readToken(m_account.id);
    const QString email = m_email->text().trimmed();
    if (base.isEmpty() || email.isEmpty() || token.isEmpty())
        return;
    const QString key = base + '\n' + email + '\n' + token;
    if (key == m_checkedFor)
        return;  // already checked / checking
    m_checkedFor = key;
    delete m_probe;  // aborts a running check
    delete m_probeAuth;
    m_probeAuth = new BasicAuth(email, token, this);
    Account probe = m_account;
    probe.baseUrl = base;
    m_probe = Backend::create(probe, m_probeAuth, nullptr, this);
    setLoginCheck("Prüfe …");
    Backend *backend = m_probe;
    backend->checkLogin([this, backend](const QString &name, const QString &err) {
        if (backend != m_probe)
            return;
        if (!err.isEmpty()) {
            m_checkedFor.clear();  // retry on the next edit
            const bool auth = err.startsWith("HTTP 401") || err.startsWith("HTTP 403");
            setLoginCheck(auth ? "Anmeldung fehlgeschlagen – URL, E-Mail und API-Token prüfen."
                               : "Nicht erreichbar: " + err,
                          true);
            return;
        }
        setLoginCheck("✓ Verbunden · angemeldet als " + name);
        if (m_mode == Mode::NewToken)
            loadProjects(backend);
    });
}

void AccountPanel::setLoginCheck(const QString &t, bool error)
{
    m_loginCheck->setText(t.toHtmlEscaped());
    m_loginCheck->setStyleSheet(error ? "color: #c53030" : t.startsWith("✓") ? "color: #2f855a" : "");
}

// Submit ----------------------------------------------------------------------------

void AccountPanel::submitForm()
{
    if (m_stack->currentWidget() != m_managePage)
        return;
    const bool token = !m_account.isOAuth();
    if (m_mode == Mode::NewToken && (m_url->text().trimmed().isEmpty() || m_email->text().trimmed().isEmpty() ||
                                     m_token->text().isEmpty())) {
        m_formError->setText("URL, E-Mail und API-Token sind Pflicht.");
        m_tabs->setCurrentIndex(AccessTab);
        return;
    }
    if (m_mode == Mode::Edit && token && m_token->text().isEmpty() && AccountStore::readToken(m_account.id).isEmpty()) {
        m_formError->setText("Bitte den API-Token eintragen.");
        m_tabs->setCurrentIndex(AccessTab);
        return;
    }

    Account a = m_account;
    if (m_mode == Mode::NewToken) {
        a.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        a.authType = "token";
        a.backend = m_type->currentData().toString();
        a.baseUrl = m_url->text().trimmed();
        if (!a.baseUrl.startsWith("http"))
            a.baseUrl.prepend("https://");
    } else if (m_mode == Mode::NewOAuth) {
        a.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    a.name = m_name->text().trimmed();
    if (a.name.isEmpty())
        a.name = QUrl(a.baseUrl).host().section('.', 0, 0);
    if (token)
        a.email = m_email->text().trimmed();
    readScope(a);
    a.interest = m_interestProjects->checked();

    const QString newToken = token ? m_token->text() : QString();
    if (!newToken.isEmpty()) {
        QString err;
        if (!AccountStore::writeToken(a.id, newToken, &err)) {
            m_formError->setText("Token konnte nicht gespeichert werden: " + err);
            return;
        }
    }
    switch (m_mode) {
    case Mode::NewToken:
        emit accountReady(a, newToken);
        leave();
        break;
    case Mode::NewOAuth:
        emit accountReady(a, {});
        nextSite();
        break;
    case Mode::Edit:
        emit accountUpdated(a, newToken);
        leave();
        break;
    }
}

void AccountPanel::cancelCurrent()
{
    // Without any account there is nothing to go back to: stay at the choice.
    if (m_stack->currentWidget() != m_managePage)
        m_cancelable ? showManage() : showChoice();
    else if (m_mode == Mode::NewOAuth)
        nextSite();  // skip this site
    else if (m_mode == Mode::NewToken)
        m_cancelable ? showManage() : showChoice();
    else
        leave();
}

void AccountPanel::leave()
{
    stopLogin();
    delete m_probe;
    m_probe = nullptr;
    m_token->clear();
    m_mode = Mode::Edit;
    emit closed();
}
