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
    bool inScope = true;
    bool cloud = false;  // result of the cloud fallback search, not stored locally
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
    QString assigneeId;
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
    int upsertIssues(const QList<IssueRecord> &issues, bool inScope);
    QList<IssueRow> search(const QString &text, int limit) const;
    QList<IssueRow> myOpen(const QString &accountId, int limit) const;
    std::optional<IssueDetail> issue(const QString &key) const;
    void touchViewed(const QString &key);

    QSet<qint64> inScopeIds() const;
    void markOutOfScope(const QSet<qint64> &ids);
    void evictCache(int days);

    // Sprints -----------------------------------------------------------
    QList<SprintInfo> openSprints() const;
    QList<IssueRow> sprintIssues(qint64 sprintId) const;
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
