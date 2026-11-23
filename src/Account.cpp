#include "Account.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

// Scope of accounts saved in older formats.
static void loadScope(Account &a, const QJsonObject &o)
{
    a.query = o["query"].toString().trimmed();
    if (o.contains("projects") || o.contains("maxAgeDays")) {
        for (const auto &p : o["projects"].toArray())
            a.projects << p.toString().trimmed();
        a.maxAgeDays = o["maxAgeDays"].toInt(0);
        // Very old format: extra JQL OR-ed with the projects -> keep the combination as own query.
        const QString extra = o["extraJql"].toString().trimmed();
        if (!o.contains("query") && !extra.isEmpty()) {
            QStringList quoted;
            for (const auto &p : std::as_const(a.projects))
                quoted << '"' + p + '"';
            QStringList parts;
            if (!quoted.isEmpty())
                parts << "project in (" + quoted.join(", ") + ")";
            parts << "(" + extra + ")";
            a.query = "(" + parts.join(" OR ") + ")";
            if (a.maxAgeDays > 0)
                a.query += QString(" AND updated >= -%1d").arg(a.maxAgeDays);
        }
        return;
    }
    // Query-only format: plain "project in (…) AND updated >= -Nd" back to projects + age.
    a.maxAgeDays = 0;
    static const QRegularExpression simple(
        R"(^(?:\(?project in \(([^()]*)\)\)?)?\s*(?:(?<=\))\s*AND\s+)?(?:updated >= -(\d+)d)?$)");
    const auto m = simple.match(a.query);
    if (!m.hasMatch())
        return;  // stays an own query
    for (const QString &p : m.captured(1).split(',', Qt::SkipEmptyParts))
        a.projects << p.trimmed().remove('"');
    a.maxAgeDays = m.captured(2).toInt();
    a.query.clear();
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
        loadScope(a, o);
        for (const auto &p : o["interest"].toArray())
            a.interest << p.toString();
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
            {"projects", QJsonArray::fromStringList(a.projects)},
            {"maxAgeDays", a.maxAgeDays},
            {"query", a.query},
            {"interest", QJsonArray::fromStringList(a.interest)},
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

static bool writeSecrets(const QJsonObject &all, QString *err);

bool AccountStore::writeToken(const QString &key, const QString &token, QString *err)
{
    QJsonObject all = readSecrets();
    all[key] = token;
    return writeSecrets(all, err);
}

void AccountStore::removeToken(const QString &key)
{
    QJsonObject all = readSecrets();
    if (all.contains(key)) {
        all.remove(key);
        writeSecrets(all, nullptr);
    }
}

static bool writeSecrets(const QJsonObject &all, QString *err)
{
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
