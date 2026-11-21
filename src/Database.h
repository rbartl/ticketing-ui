#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QSet>
#include <QSqlDatabase>
#include <QString>
#include <optional>

class QSqlQuery;

// Site-specific custom field ids, discovered via /rest/api/3/field.
struct FieldIds {
    QString sprint;
    QString rank;
};

struct IssueRow {
    qint64 id = 0;
    QString key, summary, status, statusCat, assignee, priority, issueType, updated, rank;
    bool inScope = true;
    bool cloud = false;  // result of the cloud fallback search, not stored locally
};

struct IssueDetail : IssueRow {
    QString project, reporter, labels, created, descriptionHtml;
};

struct SprintInfo {
    qint64 id = 0;
    QString name, state, start, end;
    qint64 boardId = 0;
};

struct CommentRow {
    QString id, author, created, updated, html;
};

// Local clone of one Jira site (SQLite + FTS5). One file per account.
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
    int upsertIssues(const QJsonArray &issues, const FieldIds &fields, bool inScope);
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
    void updateSprint(const QJsonObject &sprint);

    // Comment cache -----------------------------------------------------
    QList<CommentRow> comments(const QString &issueKey) const;
    qint64 commentsFetchedAt(const QString &issueKey) const;  // 0 = never
    bool replaceComments(const QString &issueKey, const QList<CommentRow> &rows);  // true if changed

    static IssueRow rowFromJson(const QJsonObject &issue);
    static QString toUtcIso(const QString &jiraTime);

private:
    QSqlDatabase db() const;
    void upsertSprint(QSqlQuery &q, const QJsonObject &s);
    QString m_conn;
};
