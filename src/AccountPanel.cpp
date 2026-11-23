#include "AccountPanel.h"
#include "Backend.h"

#include <algorithm>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QShortcut>
#include <QStackedWidget>
#include <QUuid>
#include <QVBoxLayout>

static const char *kApiTokenUrl = "https://id.atlassian.com/manage-profile/security/api-tokens";

static QLabel *title(const QString &text = {})
{
    auto *l = new QLabel(text);
    QFont f = l->font();
    f.setPointSizeF(f.pointSizeF() * 1.5);
    f.setBold(true);
    l->setFont(f);
    return l;
}

static QLabel *text(const QString &t = {})
{
    auto *l = new QLabel(t);
    l->setWordWrap(true);
    l->setTextInteractionFlags(Qt::TextBrowserInteraction);
    l->setOpenExternalLinks(true);
    return l;
}

// Templates for "Lokal speichern" of the account's backend; the last combo entry is "own query".
static const QList<BackendType::Preset> &presets()
{
    return BackendType::jira().presets;  // the only backend so far; the form gets a type selector later
}

static int customPreset()
{
    return int(presets().size());
}

static int presetFor(const QString &query)
{
    for (int i = 0; i < presets().size(); ++i)
        if (query.trimmed() == presets()[i].query)
            return i;
    return customPreset();
}

static QHBoxLayout *buttonRow(std::initializer_list<QPushButton *> buttons)
{
    auto *row = new QHBoxLayout;
    for (auto *b : buttons)
        row->addWidget(b);
    row->addStretch();
    return row;
}

AccountPanel::AccountPanel(QWidget *parent) : QWidget(parent)
{
    m_stack = new QStackedWidget;
    m_stack->setMaximumWidth(620);
    m_stack->addWidget(m_choicePage = buildChoice());
    m_stack->addWidget(m_loginPage = buildLogin());
    m_stack->addWidget(m_formPage = buildForm());

    // Centered column
    auto *h = new QHBoxLayout;
    h->addStretch(1);
    h->addWidget(m_stack, 4);
    h->addStretch(1);
    auto *v = new QVBoxLayout(this);
    v->addStretch(1);
    v->addLayout(h);
    v->addStretch(2);

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
    auto *page = new QWidget;
    auto *l = new QVBoxLayout(page);
    l->addWidget(title("Jira-Konto hinzufügen"));
    l->addWidget(text("Wie möchtest du dich bei Jira Cloud anmelden?"));
    auto *token = new QPushButton("Mit API-Token");
    auto *oauth = new QPushButton("Mit Atlassian anmelden (Browser)");
    m_choiceCancel = new QPushButton("Abbrechen");
    l->addLayout(buttonRow({token, oauth, m_choiceCancel}));
    l->addWidget(text("<small>API-Token funktioniert überall. Die Browser-Anmeldung setzt voraus, "
                      "dass die Firma die OAuth-App zulässt.</small>"));
    connect(token, &QPushButton::clicked, this, &AccountPanel::showTokenForm);
    connect(oauth, &QPushButton::clicked, this, &AccountPanel::startOAuth);
    connect(m_choiceCancel, &QPushButton::clicked, this, &AccountPanel::leave);
    return page;
}

QWidget *AccountPanel::buildLogin()
{
    auto *page = new QWidget;
    auto *l = new QVBoxLayout(page);
    l->addWidget(title("Anmeldung bei Atlassian"));
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
    l->addSpacing(8);
    l->addLayout(buttonRow({m_restart, cancel}));

    connect(copy, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(m_link->text()); });
    connect(open, &QPushButton::clicked, this, [this] { QDesktopServices::openUrl(QUrl(m_link->text())); });
    connect(m_restart, &QPushButton::clicked, this, &AccountPanel::startOAuth);
    connect(cancel, &QPushButton::clicked, this, &AccountPanel::cancelCurrent);
    return page;
}

QWidget *AccountPanel::buildForm()
{
    auto *page = new QWidget;
    auto *l = new QVBoxLayout(page);
    m_formTitle = title();
    l->addWidget(m_formTitle);

    m_form = new QFormLayout;
    m_name = new QLineEdit;
    m_url = new QLineEdit;
    m_url->setPlaceholderText("https://firma.atlassian.net");
    m_email = new QLineEdit;
    m_token = new QLineEdit;
    m_token->setEchoMode(QLineEdit::Password);
    m_tokenHint = text(QString("<small>Token erstellen: <a href=\"%1\">id.atlassian.com → Sicherheit → API-Token</a>. "
                               "Wird nur lokal auf diesem Rechner gespeichert.</small>").arg(kApiTokenUrl));
    m_form->addRow("Name", m_name);
    m_form->addRow("URL", m_url);
    m_form->addRow("E-Mail", m_email);
    m_form->addRow("API-Token", m_token);
    m_form->addRow(QString(), m_tokenHint);
    m_loginCheck = text();
    m_form->addRow(QString(), m_loginCheck);

    // What is kept locally (offline, instant search) -----------------------------
    auto *section = new QLabel("<b>Lokal speichern</b>");
    section->setContentsMargins(0, 12, 0, 0);
    m_form->addRow(section);

    m_preset = new QComboBox;
    for (const auto &p : presets())
        m_preset->addItem(p.label);
    m_preset->addItem("Eigenes " + BackendType::jira().queryLabel + " …");
    m_jql = new QLineEdit;
    m_jql->setPlaceholderText("leer = alle Tickets");
    m_jql->setClearButtonEnabled(true);
    m_form->addRow("Vorlage", m_preset);
    m_form->addRow(BackendType::jira().queryLabel, m_jql);
    m_form->addRow(QString(), text("<small>Diese Tickets liegen lokal: offline verfügbar, Suche ohne Wartezeit. "
                                   "Leeres JQL = alle Tickets der Site. Alles andere findet die Suche weiterhin in "
                                   "der Cloud (☁) und lädt es beim Öffnen.<br>Beispiele: <tt>project in (ABC, OPS)</tt>, "
                                   "<tt>updated &gt;= -90d</tt>, <tt>labels = ops</tt></small>"));
    l->addLayout(m_form);

    m_formError = text();
    m_formError->setStyleSheet("color: #c0392b");
    l->addWidget(m_formError);

    m_submit = new QPushButton;
    m_formCancel = new QPushButton;
    l->addLayout(buttonRow({m_submit, m_formCancel}));

    // Template -> fills the JQL; editing the JQL selects the matching template (or "own JQL").
    connect(m_preset, &QComboBox::activated, this, [this](int i) {
        if (i != customPreset())
            m_jql->setText(presets()[i].query);
        m_jql->setFocus();
    });
    connect(m_jql, &QLineEdit::textEdited, this, [this](const QString &t) { m_preset->setCurrentIndex(presetFor(t)); });
    // Token accounts: check the login as soon as the credentials are complete.
    for (QLineEdit *e : {m_url, m_email, m_token})
        connect(e, &QLineEdit::editingFinished, this, &AccountPanel::checkLogin);
    connect(m_submit, &QPushButton::clicked, this, &AccountPanel::submitForm);
    connect(m_formCancel, &QPushButton::clicked, this, &AccountPanel::cancelCurrent);
    for (QLineEdit *e : {m_name, m_url, m_email, m_token, m_jql})
        connect(e, &QLineEdit::returnPressed, this, &AccountPanel::submitForm);
    return page;
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
    showForm(Mode::NewToken, {});
}

void AccountPanel::showEditForm(const Account &account)
{
    stopLogin();
    showForm(Mode::Edit, account);
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
    m_loginText->setStyleSheet(error ? "color: #c0392b" : "");
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
    showForm(Mode::NewOAuth, a);
}

void AccountPanel::showForm(Mode mode, const Account &account)
{
    m_mode = mode;
    m_account = account;
    const bool token = mode == Mode::NewToken || (mode == Mode::Edit && !account.isOAuth());
    switch (mode) {
    case Mode::NewToken:
        m_formTitle->setText("Jira-Konto mit API-Token");
        m_submit->setText("Hinzufügen");
        m_formCancel->setText("Abbrechen");
        break;
    case Mode::NewOAuth:
        m_formTitle->setText("Jira-Site einrichten: " + account.name);
        m_submit->setText("Hinzufügen");
        m_formCancel->setText("Überspringen");
        break;
    case Mode::Edit:
        m_formTitle->setText("Konto bearbeiten: " + account.name);
        m_submit->setText("Speichern");
        m_formCancel->setText("Abbrechen");
        break;
    }
    m_name->setText(account.name);
    m_url->setText(account.baseUrl);
    m_email->setText(account.email);
    m_token->clear();
    const bool tokenMissing = mode == Mode::Edit && token && AccountStore::readToken(account.id).isEmpty();
    m_token->setPlaceholderText(mode != Mode::Edit ? QString() : tokenMissing ? "fehlt – bitte eintragen" : "unverändert");
    m_url->setReadOnly(mode != Mode::NewToken);  // other URL = other site = new account
    m_email->setReadOnly(!token);
    m_form->setRowVisible(m_token, token);
    m_form->setRowVisible(m_tokenHint, token);

    // Sync scope (older accounts are converted when loading, see AccountStore::load).
    const QString jql = mode == Mode::Edit ? account.query : presets().first().query;
    m_jql->setText(jql);
    m_preset->setCurrentIndex(presetFor(jql));

    m_checkedFor.clear();
    setLoginCheck({});

    m_formError->clear();
    m_stack->setCurrentWidget(m_formPage);
    (mode == Mode::NewToken ? m_url : m_name)->setFocus();
    if (mode == Mode::Edit)
        checkLogin();
}

// Login check (API-token accounts) ----------------------------------------------------

void AccountPanel::checkLogin()
{
    if (m_stack->currentWidget() != m_formPage || m_account.isOAuth())
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
    setLoginCheck("Prüfe Anmeldung …");
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
        setLoginCheck("✓ Angemeldet als " + name);
    });
}

void AccountPanel::setLoginCheck(const QString &t, bool error)
{
    m_form->setRowVisible(m_loginCheck, !t.isEmpty());
    m_loginCheck->setText("<small>" + t.toHtmlEscaped() + "</small>");
    m_loginCheck->setStyleSheet(error ? "color: #c0392b" : "");
}

// Submit ----------------------------------------------------------------------------

void AccountPanel::submitForm()
{
    const bool token = !m_account.isOAuth();
    if (m_mode == Mode::NewToken && (m_url->text().trimmed().isEmpty() || m_email->text().trimmed().isEmpty() ||
                                     m_token->text().isEmpty())) {
        m_formError->setText("URL, E-Mail und API-Token sind Pflicht.");
        return;
    }
    if (m_mode == Mode::Edit && token && m_token->text().isEmpty() && AccountStore::readToken(m_account.id).isEmpty()) {
        m_formError->setText("Bitte den API-Token eintragen.");
        return;
    }
    Account a = m_account;
    if (m_mode == Mode::NewToken) {
        a.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        a.authType = "token";
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
    a.query = m_jql->text().trimmed();

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
    if (m_stack->currentWidget() == m_formPage && m_mode == Mode::NewOAuth)
        nextSite();  // skip this site
    else if (m_stack->currentWidget() == m_choicePage && !m_cancelable)
        return;
    else
        leave();
}

void AccountPanel::leave()
{
    stopLogin();
    delete m_probe;
    m_probe = nullptr;
    m_token->clear();
    emit closed();
}
