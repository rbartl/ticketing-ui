#include "Account.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <qt6keychain/keychain.h>

static const char *kKeychainService = "jira-client";

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
            {"auth", a.authType},
            {"cloudId", a.cloudId},
            {"oauthUser", a.oauthUser},
        });
    }
    QFile f(dataDir() + "/accounts.json");
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(arr).toJson());
}

QString AccountStore::readToken(const QString &accountId)
{
    QKeychain::ReadPasswordJob job(kKeychainService);
    job.setAutoDelete(false);
    job.setKey(accountId);
    QEventLoop loop;
    QObject::connect(&job, &QKeychain::Job::finished, &loop, &QEventLoop::quit);
    job.start();
    loop.exec();
    return job.error() ? QString() : job.textData();
}

bool AccountStore::writeToken(const QString &accountId, const QString &token, QString *err)
{
    QKeychain::WritePasswordJob job(kKeychainService);
    job.setAutoDelete(false);
    job.setKey(accountId);
    job.setTextData(token);
    QEventLoop loop;
    QObject::connect(&job, &QKeychain::Job::finished, &loop, &QEventLoop::quit);
    job.start();
    loop.exec();
    if (job.error() && err)
        *err = job.errorString();
    return !job.error();
}

void AccountStore::writeSecretAsync(const QString &key, const QString &value)
{
    auto *job = new QKeychain::WritePasswordJob(kKeychainService);
    job->setAutoDelete(true);
    job->setKey(key);
    job->setTextData(value);
    job->start();
}
