#include "AccountPanel.h"
#include "JiraClient.h"

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
#include <QListWidget>
#include <QPushButton>
#include <QRegularExpression>
#include <QShortcut>
#include <QSignalBlocker>
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

// Presets for "which tickets besides whole projects"; nullptr = own JQL.
static const struct {
    const char *label;
    const char *jql;
} kPresets[] = {
    {"Woran ich beteiligt bin (zugewiesen, gemeldet, beobachtet)",
     "assignee = currentUser() OR reporter = currentUser() OR watcher = currentUser()"},
    {"Mir zugewiesen oder von mir beobachtet", "assignee = currentUser() OR watcher = currentUser()"},
    {"Mir zugewiesen", "assignee = currentUser()"},
    {"Keine – nur die ausgewählten Projekte", ""},
    {"Eigenes JQL …", nullptr},
};
static constexpr int kCustomPreset = int(std::size(kPresets)) - 1;

static const struct {
    const char *label;
    int days;
} kAges[] = {{"3 Monate", 90}, {"6 Monate", 180}, {"1 Jahr", 365}, {"2 Jahre", 730}, {"Unbegrenzt", 0}};
static constexpr int kDefaultAgeDays = 365;

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

    // What is kept locally (offline, instant search) -----------------------------
    auto *section = new QLabel("<b>Lokal speichern</b>");
    section->setContentsMargins(0, 12, 0, 0);
    m_form->addRow(section);

    m_preset = new QComboBox;
    for (const auto &p : kPresets)
        m_preset->addItem(p.label);
    m_jql = new QLineEdit;
    m_jql->setPlaceholderText("z. B. assignee = currentUser() OR labels = ops");
    m_form->addRow("Tickets", m_preset);
    m_form->addRow(QString(), m_jql);

    auto *projects = new QWidget;
    auto *pl = new QVBoxLayout(projects);
    pl->setContentsMargins(0, 0, 0, 0);
    m_projectFilter = new QLineEdit;
    m_projectFilter->setPlaceholderText("Projekte filtern …");
    m_projectFilter->setClearButtonEnabled(true);
    m_projectList = new QListWidget;
    m_projectList->setMinimumHeight(140);
    m_projectInfo = text();
    pl->addWidget(m_projectFilter);
    pl->addWidget(m_projectList);
    pl->addWidget(m_projectInfo);
    m_form->addRow("+ ganze Projekte", projects);

    m_age = new QComboBox;
    for (const auto &a : kAges)
        m_age->addItem(a.label, a.days);
    m_form->addRow("Zuletzt geändert", m_age);
    m_form->addRow(QString(), text("<small>Nur Tickets, die in diesem Zeitraum geändert wurden. Alles andere findet die "
                                   "Suche weiterhin in der Cloud (☁) und lädt es beim Öffnen.</small>"));
    l->addLayout(m_form);

    m_formError = text();
    m_formError->setStyleSheet("color: #c0392b");
    l->addWidget(m_formError);

    m_submit = new QPushButton;
    m_formCancel = new QPushButton;
    l->addLayout(buttonRow({m_submit, m_formCancel}));

    connect(m_preset, &QComboBox::currentIndexChanged, this, [this](int i) {
        m_form->setRowVisible(m_jql, i == kCustomPreset);
        if (i == kCustomPreset)
            m_jql->setFocus();
    });
    connect(m_projectFilter, &QLineEdit::textChanged, this, &AccountPanel::filterProjects);
    connect(m_projectList, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
        const QString key = item->data(Qt::UserRole).toString();
        if (item->checkState() == Qt::Checked)
            m_selectedProjects.insert(key);
        else
            m_selectedProjects.remove(key);
        updateProjectInfo();
    });
    // Token accounts: load the project list as soon as the credentials are complete (also checks them).
    for (QLineEdit *e : {m_url, m_email, m_token})
        connect(e, &QLineEdit::editingFinished, this, &AccountPanel::loadProjects);
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

    // Sync scope: new accounts start with the broadest useful preset and one year.
    int preset = 0;
    if (mode == Mode::Edit) {
        preset = kCustomPreset;
        for (int i = 0; i < kCustomPreset; ++i)
            if (account.extraJql.trimmed() == QString::fromUtf8(kPresets[i].jql))
                preset = i;
    }
    m_jql->setText(mode == Mode::Edit ? account.extraJql : QString());
    m_preset->setCurrentIndex(preset);
    m_form->setRowVisible(m_jql, preset == kCustomPreset);
    const int days = mode == Mode::Edit ? account.maxAgeDays : kDefaultAgeDays;
    int age = m_age->findData(days);
    if (age < 0) {
        m_age->addItem(QString("%1 Tage").arg(days), days);
        age = m_age->count() - 1;
    }
    m_age->setCurrentIndex(age);

    m_selectedProjects = QSet<QString>(account.projects.begin(), account.projects.end());
    m_projectsLoadedFor.clear();
    m_projectFilter->clear();
    m_projectList->clear();
    updateProjectInfo(token && mode == Mode::NewToken
                          ? "Die Projekte erscheinen, sobald URL, E-Mail und API-Token eingetragen sind."
                          : QString());

    m_formError->clear();
    m_stack->setCurrentWidget(m_formPage);
    (mode == Mode::NewToken ? m_url : m_name)->setFocus();
    if (mode != Mode::NewToken)
        loadProjects();
}

// Projects --------------------------------------------------------------------------

void AccountPanel::loadProjects()
{
    if (m_stack->currentWidget() != m_formPage)
        return;
    QString base, credentials, token;
    if (m_account.isOAuth()) {
        base = "https://api.atlassian.com/ex/jira/" + m_account.cloudId;
        credentials = m_account.oauthUser;
    } else {
        base = m_url->text().trimmed();
        if (!base.isEmpty() && !base.startsWith("http"))
            base.prepend("https://");
        token = m_token->text();
        if (token.isEmpty() && m_mode == Mode::Edit)
            token = AccountStore::readToken(m_account.id);
        if (base.isEmpty() || m_email->text().trimmed().isEmpty() || token.isEmpty())
            return;
        credentials = m_email->text().trimmed() + '\n' + token;
    }
    if (base + '\n' + credentials == m_projectsLoadedFor)
        return;  // already loaded / loading
    delete m_probe;  // aborts a running load
    m_probe = nullptr;
    AuthProvider *auth = nullptr;
    if (m_account.isOAuth()) {
        auth = m_sessionFor ? m_sessionFor(m_account.oauthUser) : nullptr;
    } else {
        delete m_probeAuth;
        auth = m_probeAuth = new BasicAuth(m_email->text().trimmed(), token, this);
    }
    if (!auth)
        return;
    m_projectsLoadedFor = base + '\n' + credentials;
    m_probe = new JiraClient(base, auth, this);
    updateProjectInfo("Lade Projekte …");
    fetchProjectPage(0, std::make_shared<QList<QPair<QString, QString>>>());
}

void AccountPanel::fetchProjectPage(int startAt, std::shared_ptr<QList<QPair<QString, QString>>> acc)
{
    JiraClient *client = m_probe;
    const QUrlQuery q{{"startAt", QString::number(startAt)}, {"maxResults", "100"}, {"orderBy", "name"}};
    client->get("/rest/api/3/project/search", q, [this, client, startAt, acc](const QJsonDocument &doc, const QString &err) {
        if (client != m_probe)
            return;
        if (!err.isEmpty()) {
            m_projectsLoadedFor.clear();  // retry on the next edit
            const bool auth = err.startsWith("HTTP 401") || err.startsWith("HTTP 403");
            updateProjectInfo(auth ? "Anmeldung fehlgeschlagen – URL, E-Mail und API-Token prüfen."
                                   : "Projekte konnten nicht geladen werden: " + err);
            return;
        }
        const auto values = doc["values"].toArray();
        for (const auto &v : values) {
            const auto o = v.toObject();
            acc->append({o["key"].toString(), o["name"].toString()});
        }
        if (!doc["isLast"].toBool(true) && !values.isEmpty())
            fetchProjectPage(startAt + int(values.size()), acc);
        else
            fillProjects(*acc);
    });
}

void AccountPanel::fillProjects(const QList<QPair<QString, QString>> &projects)
{
    QSignalBlocker b(m_projectList);
    m_projectList->clear();
    QSet<QString> missing = m_selectedProjects;
    auto add = [this](const QString &key, const QString &label) {
        auto *item = new QListWidgetItem(label, m_projectList);
        item->setData(Qt::UserRole, key);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        item->setCheckState(m_selectedProjects.contains(key) ? Qt::Checked : Qt::Unchecked);
    };
    for (const auto &[key, name] : projects) {
        add(key, key + "  –  " + name);
        missing.remove(key);
    }
    for (const auto &key : missing)  // selected earlier, no longer visible (archived, no access)
        add(key, key + "  –  (nicht gefunden)");
    filterProjects();
    updateProjectInfo();
}

void AccountPanel::filterProjects()
{
    const QString f = m_projectFilter->text().trimmed();
    for (int i = 0; i < m_projectList->count(); ++i) {
        auto *item = m_projectList->item(i);
        item->setHidden(!f.isEmpty() && !item->text().contains(f, Qt::CaseInsensitive));
    }
}

void AccountPanel::updateProjectInfo(const QString &t)
{
    QString s = t;
    if (s.isEmpty() && m_projectList->count() > 0)
        s = m_selectedProjects.isEmpty() ? QString("Optional: Projekte anhaken, die komplett lokal liegen sollen "
                                                   "(z. B. das eigene Team-Board mit Sprints).")
                                         : QString("%1 ausgewählt: %2")
                                               .arg(m_selectedProjects.size())
                                               .arg(QStringList(m_selectedProjects.values()).join(", "));
    m_projectInfo->setText("<small>" + s.toHtmlEscaped() + "</small>");
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
    const int preset = m_preset->currentIndex();
    const QString jql = preset == kCustomPreset ? m_jql->text().trimmed() : QString::fromUtf8(kPresets[preset].jql);
    if (jql.isEmpty() && m_selectedProjects.isEmpty()) {
        m_formError->setText(preset == kCustomPreset ? "Bitte JQL eintragen oder Projekte auswählen."
                                                     : "Bitte mindestens ein Projekt auswählen.");
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
    a.projects = m_selectedProjects.values();
    a.projects.sort();
    a.extraJql = jql;
    a.maxAgeDays = m_age->currentData().toInt();

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
