#include "Account.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

QString Account::scopeJql() const
{
    QStringList parts;
    if (!projects.isEmpty()) {
        QStringList quoted;
        for (const auto &p : projects)
            quoted << '"' + p.trimmed() + '"';
        parts << "project in (" + quoted.join(", ") + ")";
    }
    if (!extraJql.trimmed().isEmpty())
        parts << "(" + extraJql.trimmed() + ")";
    if (parts.isEmpty())
        parts << "assignee = currentUser()";
    if (maxAgeDays > 0)  // relative => the scope string stays the same, no resync every day
        return "(" + parts.join(" OR ") + QString(") AND updated >= -%1d").arg(maxAgeDays);
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
        for (const auto &p : o["projects"].toArray())
            a.projects << p.toString();
        a.extraJql = o["extraJql"].toString();
        a.maxAgeDays = o["maxAgeDays"].toInt(0);
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
            {"projects", QJsonArray::fromStringList(a.projects)},
            {"extraJql", a.extraJql},
            {"maxAgeDays", a.maxAgeDays},
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
