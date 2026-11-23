#pragma once
#include <QList>
#include <QSet>
#include <QSqlDatabase>
#include <QString>
#include <optional>

class QSqlQuery;

// Backend neutral: statusCat is "new" | "indeterminate" | "done", times are UTC ISO.
struct IssueRow {
    qint64 id = 0;
    QString key, summary, status, statusCat, assignee, priority, issueType, updated, rank;
    QString project, reporter, labels, created;  // labels space separated
    QString assigneeId;
    bool watching = false;  // the own user watches it
    bool unread = false;    // changed since it was last opened
    bool inScope = true;
    bool cloud = false;   // result of the cloud fallback search, not stored locally
    QString accountId;    // set by the UI when lists of several accounts are merged
};

struct IssueDetail : IssueRow {
    QString descriptionHtml;
    QString sprint;  // names of its sprints, comma separated
};

struct SprintInfo {
    qint64 id = 0;
    QString name, state, start, end, goal;  // state: "active" | "future" | "closed"
    qint64 boardId = 0;
};

// Everything stored about one issue, as delivered by the backend.
struct IssueRecord : IssueDetail {
    QString descriptionText;                   // for the full text index
    std::optional<QList<SprintInfo>> sprints;  // nullopt = unknown, keep the stored ones
};

struct CommentRow {
    QString id, author, created, updated, html;
};

// Local clone of one account (SQLite + FTS5). One file per account.
class Database {
public:
    Database() = default;
    ~Database();
    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;

    bool open(const QString &path, const QString &connectionName, QString *err);

    QString state(const QString &key) const;
    void setState(const QString &key, const QString &value);

    // Issues ------------------------------------------------------------
    // markRead: the first download of a scope must not flag everything as unread.
    int upsertIssues(const QList<IssueRecord> &issues, bool inScope, bool markRead = false);
    // Filters apply before the limit: hideDone drops finished issues, projects (keys, empty = all) limits the projects.
    QList<IssueRow> search(const QString &text, int limit, bool hideDone = false) const;
    std::optional<IssueDetail> issue(const QString &key) const;
    void touchViewed(const QString &key);
    void markRead(const QString &key);

    // Everything in the sync scope, newest change first.
    QList<IssueRow> localIssues(int limit, bool hideDone = false, const QStringList &projects = {}) const;
    int unreadCount() const;                         // in the sync scope

    QSet<qint64> inScopeIds() const;
    void markOutOfScope(const QSet<qint64> &ids);
    void evictCache(int days);

    // Sprints -----------------------------------------------------------
    QList<SprintInfo> openSprints() const;
    void updateSprint(const SprintInfo &sprint);

    // Comment cache -----------------------------------------------------
    QList<CommentRow> comments(const QString &issueKey) const;
    qint64 commentsFetchedAt(const QString &issueKey) const;  // 0 = never
    bool replaceComments(const QString &issueKey, const QList<CommentRow> &rows);  // true if changed

private:
    QSqlDatabase db() const;
    void upsertSprint(QSqlQuery &q, const SprintInfo &s);
    QString m_conn;
};
