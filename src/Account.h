#pragma once
#include <QList>
#include <QString>
#include <QStringList>

// One ticket system site (see Backend). The API token lives in secrets.json (see AccountStore), never in accounts.json.
struct Account {
    QString id;        // uuid, also the name of the local db file
    QString name;      // display name
    QString baseUrl;   // https://firma.atlassian.net
    QString email;          // API-token login, or the Atlassian identity's e-mail (OAuth)
    QString backend = "jira";    // BackendType::id
    QString authType = "token";  // "token" | "oauth"
    QString cloudId;        // only for OAuth accounts
    QString oauthUser;      // only for OAuth: Atlassian account id = which login this site uses
    // What is kept locally (see BackendType::scopeQuery):
    QStringList projects;   // project keys; empty = all projects
    int maxAgeDays = 90;    // only tickets changed within that many days; 0 = any age
    QString query;          // own query in the backend's language; when set it replaces projects + maxAgeDays
    QStringList interest;   // project keys the list shows with "Nur meine Projekte"; empty = everything
    bool isOAuth() const { return authType == "oauth"; }
    QString label() const { return email.isEmpty() ? name : name + "  ·  " + email; }
    // REST base: the site itself (API token) or the api.atlassian.com gateway (OAuth).
    QString apiBase() const;
};

class AccountStore {
public:
    static QString dataDir();
    static QList<Account> load();
    static void save(const QList<Account> &accounts);
    // Secrets by key: account id (API token) or "oauth:<atlassian account id>" (OAuth session).
    static QString readToken(const QString &key);
    static bool writeToken(const QString &key, const QString &token, QString *err = nullptr);
    static void removeToken(const QString &key);
};
