#include "Account.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

// Accounts saved before the query field: project list + extra JQL + age limit -> one JQL.
static QString legacyJql(const QJsonObject &o)
{
    QStringList parts;
    QStringList quoted;
    for (const auto &p : o["projects"].toArray())
        quoted << '"' + p.toString().trimmed() + '"';
    if (!quoted.isEmpty())
        parts << "project in (" + quoted.join(", ") + ")";
    const QString extra = o["extraJql"].toString().trimmed();
    if (!extra.isEmpty())
        parts << "(" + extra + ")";
    const int days = o["maxAgeDays"].toInt(0);
    if (parts.isEmpty() && days > 0)
        return QString("updated >= -%1d").arg(days);
    if (days > 0)
        return "(" + parts.join(" OR ") + QString(") AND updated >= -%1d").arg(days);
    return parts.join(" OR ");
}

QString Account::apiBase() const
{
    return isOAuth() ? "https://api.atlassian.com/ex/jira/" + cloudId : baseUrl;
}

QString AccountStore::dataDir()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir;
}

QList<Account> AccountStore::load()
{
    QFile f(dataDir() + "/accounts.json");
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QList<Account> out;
    for (const auto &v : QJsonDocument::fromJson(f.readAll()).array()) {
        const auto o = v.toObject();
        Account a;
        a.id = o["id"].toString();
        a.name = o["name"].toString();
        a.baseUrl = o["baseUrl"].toString();
        a.email = o["email"].toString();
        a.backend = o["backend"].toString("jira");
        a.query = o.contains("query") ? o["query"].toString() : legacyJql(o);
        a.authType = o["auth"].toString("token");
        a.cloudId = o["cloudId"].toString();
        a.oauthUser = o["oauthUser"].toString();
        out << a;
    }
    return out;
}

void AccountStore::save(const QList<Account> &accounts)
{
    QJsonArray arr;
    for (const auto &a : accounts) {
        arr.append(QJsonObject{
            {"id", a.id},
            {"name", a.name},
            {"baseUrl", a.baseUrl},
            {"email", a.email},
            {"backend", a.backend},
            {"query", a.query},
            {"auth", a.authType},
            {"cloudId", a.cloudId},
            {"oauthUser", a.oauthUser},
        });
    }
    QFile f(dataDir() + "/accounts.json");
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(arr).toJson());
}

// Secrets (API tokens, OAuth sessions) live in <AppData>/secrets.json, readable only by the user.
// Not in the OS keychain: on macOS every rebuild changes the code signature and the keychain asks
// for the password again and again.
static QString secretsPath() { return AccountStore::dataDir() + "/secrets.json"; }

static QJsonObject readSecrets()
{
    QFile f(secretsPath());
    return f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject();
}

QString AccountStore::readToken(const QString &key)
{
    return readSecrets()[key].toString();
}

bool AccountStore::writeToken(const QString &key, const QString &token, QString *err)
{
    QJsonObject all = readSecrets();
    all[key] = token;
    QSaveFile f(secretsPath());
    if (!f.open(QIODevice::WriteOnly)) {
        if (err)
            *err = f.errorString();
        return false;
    }
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    f.write(QJsonDocument(all).toJson());
    if (!f.commit()) {
        if (err)
            *err = f.errorString();
        return false;
    }
    return true;
}
