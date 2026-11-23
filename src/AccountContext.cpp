#include "AccountContext.h"
#include "Backend.h"
#include "SyncEngine.h"

#include <QDateTime>

static constexpr qint64 kCommentMinAgeSecs = 30;  // don't refetch faster than that

AccountContext::AccountContext(const Account &account, AuthProvider *auth, QObject *parent)
    : QObject(parent), m_account(account), m_auth(auth)
{
}

bool AccountContext::open(QString *err)
{
    if (!m_db.open(AccountStore::dataDir() + "/" + m_account.id + ".db", m_account.id, err))
        return false;
    m_backend = Backend::create(m_account, m_auth, &m_db, this);
    m_sync = new SyncEngine(m_backend->scopeQuery(m_account), m_backend, &m_db, this);
    return true;
}

void AccountContext::update(const Account &account)
{
    m_account = account;
    m_sync->setScope(m_backend->scopeQuery(account));
}

QString AccountContext::browseUrl(const QString &key) const
{
    return m_backend->browseUrl(key);
}

void AccountContext::searchCloud(const QString &text, std::function<void(QList<IssueRow>, QString)> cb)
{
    m_backend->searchText(text, m_sync->scope(), cb);
}

void AccountContext::fetchIssue(const QString &key, std::function<void(bool, QString)> cb)
{
    m_backend->fetchIssue(key, [this, cb](const std::optional<IssueRecord> &r, const QString &err) {
        if (!r) {
            cb(false, err);
            return;
        }
        m_db.upsertIssues({*r}, false);
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
    m_backend->fetchComments(key, [this, key, cb](const QList<CommentRow> &rows, const QString &err) {
        m_commentsInFlight.remove(key);
        cb(err.isEmpty() && m_db.replaceComments(key, rows));
    });
}
