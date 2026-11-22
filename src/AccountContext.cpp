#include "AccountContext.h"
#include "Adf.h"
#include "JiraClient.h"
#include "SyncEngine.h"

#include <QDateTime>
#include <QJsonArray>
#include <QRegularExpression>

static constexpr qint64 kCommentMinAgeSecs = 30;  // don't refetch faster than that

AccountContext::AccountContext(const Account &account, AuthProvider *auth, QObject *parent)
    : QObject(parent), m_account(account), m_auth(auth)
{
    m_client = new JiraClient(account.apiBase(), auth, this);
}

bool AccountContext::open(QString *err)
{
    if (!m_db.open(AccountStore::dataDir() + "/" + m_account.id + ".db", m_account.id, err))
        return false;
    m_sync = new SyncEngine(m_account.scopeJql(), m_client, &m_db, this);
    return true;
}

void AccountContext::update(const Account &account)
{
    m_account = account;
    m_sync->setScope(account.scopeJql());
}

QString AccountContext::browseUrl(const QString &key) const
{
    QString base = m_account.baseUrl;
    while (base.endsWith('/'))
        base.chop(1);
    return base + "/browse/" + key;
}

void AccountContext::searchCloud(const QString &text, std::function<void(QList<IssueRow>, QString)> cb)
{
    static const QRegularExpression keyRe("^[A-Za-z][A-Za-z0-9_]*-\\d+$");
    const QString t = text.trimmed();

    if (keyRe.match(t).hasMatch()) {
        const QString key = t.toUpper();
        m_client->get("/rest/api/3/issue/" + key, {{"fields", "summary,status,assignee,priority,issuetype,updated,created,reporter,labels,project"}},
                      [cb](const QJsonDocument &doc, const QString &err) {
            if (!err.isEmpty())
                cb({}, err.startsWith("HTTP 404") ? QString() : err);
            else
                cb({Database::rowFromJson(doc.object())}, {});
        });
        return;
    }

    QString escaped = t;
    escaped.replace('\\', "\\\\").replace('"', "\\\"");
    const QString jql = QString("text ~ \"%1\" AND NOT (%2) ORDER BY updated DESC").arg(escaped, m_sync->scopeJql());
    m_client->searchJql(jql, {"summary", "status", "assignee", "priority", "issuetype", "updated", "created", "reporter",
                                "labels", "project"}, 50, {},
                        [cb](const QJsonDocument &doc, const QString &err) {
        if (!err.isEmpty()) {
            cb({}, err);
            return;
        }
        QList<IssueRow> rows;
        for (const auto &v : doc["issues"].toArray())
            rows << Database::rowFromJson(v.toObject());
        cb(rows, {});
    });
}

void AccountContext::fetchIssue(const QString &key, std::function<void(bool, QString)> cb)
{
    m_client->get("/rest/api/3/issue/" + key, {{"fields", m_sync->fields().join(',')}},
                  [this, cb](const QJsonDocument &doc, const QString &err) {
        if (!err.isEmpty()) {
            cb(false, err);
            return;
        }
        m_db.upsertIssues(QJsonArray{doc.object()}, m_sync->fieldIds(), false);
        cb(true, {});
    });
}

void AccountContext::refreshComments(const QString &key, std::function<void(bool)> cb)
{
    const qint64 age = QDateTime::currentSecsSinceEpoch() - m_db.commentsFetchedAt(key);
    if (age < kCommentMinAgeSecs || m_commentsInFlight.contains(key)) {
        cb(false);
        return;
    }
    m_commentsInFlight.insert(key);
    auto acc = std::make_shared<QList<CommentRow>>();
    fetchCommentPage(key, 0, acc, [this, key, acc, cb](bool ok) {
        m_commentsInFlight.remove(key);
        cb(ok && m_db.replaceComments(key, *acc));
    });
}

void AccountContext::fetchCommentPage(const QString &key, int startAt, std::shared_ptr<QList<CommentRow>> acc,
                                      std::function<void(bool)> done)
{
    QUrlQuery q{{"startAt", QString::number(startAt)}, {"maxResults", "100"}, {"orderBy", "created"}};
    m_client->get("/rest/api/3/issue/" + key + "/comment", q,
                  [=, this](const QJsonDocument &doc, const QString &err) {
        if (!err.isEmpty()) {
            done(false);
            return;
        }
        const auto comments = doc["comments"].toArray();
        for (const auto &v : comments) {
            const auto c = v.toObject();
            acc->append(CommentRow{
                c["id"].toString(),
                c["author"].toObject()["displayName"].toString(),
                Database::toUtcIso(c["created"].toString()),
                Database::toUtcIso(c["updated"].toString()),
                Adf::toHtml(c["body"]),
            });
        }
        const int total = doc["total"].toInt();
        const int next = startAt + int(comments.size());
        if (!comments.isEmpty() && next < total)
            fetchCommentPage(key, next, acc, done);
        else
            done(true);
    });
}
