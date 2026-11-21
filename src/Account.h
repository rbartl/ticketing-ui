#pragma once
#include <QList>
#include <QString>
#include <QStringList>

// One Jira Cloud site. The API token lives in the OS keychain, never in accounts.json.
struct Account {
    QString id;        // uuid, also the name of the local db file
    QString name;      // display name
    QString baseUrl;   // https://firma.atlassian.net
    QString email;          // API-token login, or the Atlassian identity's e-mail (OAuth)
    QString authType = "token";  // "token" | "oauth"
    QString cloudId;        // only for OAuth accounts
    QString oauthUser;      // only for OAuth: Atlassian account id = which login this site uses
    QStringList projects;  // project keys synced completely
    QString extraJql;      // additionally synced, e.g. "assignee = currentUser()"

    // JQL describing everything that is cloned locally.
    QString scopeJql() const;
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
    static QString readToken(const QString &accountId);
    static bool writeToken(const QString &accountId, const QString &token, QString *err = nullptr);
    static void writeSecretAsync(const QString &key, const QString &value);
};
