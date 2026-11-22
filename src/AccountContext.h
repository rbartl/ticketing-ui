#pragma once
#include "Account.h"
#include "Database.h"

#include <QObject>
#include <functional>

class AuthProvider;
class JiraClient;
class SyncEngine;

// Everything belonging to one Jira site: API client, local db, sync engine.
// All sites sync in parallel; the UI only shows the active one.
class AccountContext : public QObject {
    Q_OBJECT
public:
    AccountContext(const Account &account, AuthProvider *auth, QObject *parent = nullptr);

    bool open(QString *err);
    void update(const Account &account);  // edited name / sync scope (same site, same db)

    const Account &account() const { return m_account; }
    AuthProvider *auth() const { return m_auth; }
    Database &db() { return m_db; }
    SyncEngine &sync() { return *m_sync; }
    QString me() const { return m_db.state("me"); }
    QString browseUrl(const QString &key) const;

    // Cloud fallback: everything OUTSIDE the local scope.
    void searchCloud(const QString &text, std::function<void(QList<IssueRow>, QString err)> cb);
    // Fetch one issue (not in scope or refresh cache) and store it in the local cache.
    void fetchIssue(const QString &key, std::function<void(bool ok, QString err)> cb);
    // Comments: shown from cache, refreshed here in the background.
    void refreshComments(const QString &key, std::function<void(bool changed)> cb);

private:
    void fetchCommentPage(const QString &key, int startAt, std::shared_ptr<QList<CommentRow>> acc,
                          std::function<void(bool ok)> done);

    Account m_account;
    AuthProvider *m_auth;
    JiraClient *m_client = nullptr;
    Database m_db;
    SyncEngine *m_sync = nullptr;
    QSet<QString> m_commentsInFlight;
};
