#pragma once
#include "Backend.h"

class JiraClient;

// Jira Cloud: REST API v3 + Agile API, JQL as query language, ADF rich text.
class JiraBackend : public Backend {
    Q_OBJECT
public:
    // db: keeps the discovered custom field ids across restarts (may be nullptr, see Backend::create).
    JiraBackend(const Account &account, AuthProvider *auth, Database *db, QObject *parent = nullptr);

    const BackendType &type() const override { return BackendType::jira(); }
    static QString scopeJql(const Account &account);  // BackendType::scopeQuery

    void checkLogin(std::function<void(QString, QString)> cb) override;
    void fetchProjects(std::function<void(QList<ProjectInfo>, QString)> cb) override;
    void prepare(std::function<void(QString, QString)> cb) override;
    void fetchIssues(const QString &scope, int updatedWithinMinutes, const QString &cursor,
                     std::function<void(IssuePage, QString)> cb) override;
    void fetchIds(const QString &scope, const QString &cursor,
                  std::function<void(QList<qint64>, QString, QString)> cb) override;
    bool hasSprints() const override { return !m_sprintField.isEmpty(); }
    void fetchSprint(qint64 id, std::function<void(std::optional<SprintInfo>)> cb) override;

    void fetchIssue(const QString &key, std::function<void(std::optional<IssueRecord>, QString)> cb) override;
    void searchText(const QString &text, const QString &scope,
                    std::function<void(QList<IssueRow>, QString)> cb) override;
    void fetchComments(const QString &key, std::function<void(QList<CommentRow>, QString)> cb) override;
    void fetchHistory(const QString &key, std::function<void(QList<HistoryEntry>, QString)> cb) override;

    QList<DetailField> detailFields(const IssueDetail &issue) const override;
    QString browseUrl(const QString &key) const override;
    QString myId() const override { return m_me; }

    void transitions(const QString &key, std::function<void(QList<Transition>, QString)> cb) override;
    void transition(const QString &key, const QString &transitionId, Done cb) override;
    void addComment(const QString &key, const QString &text, Done cb) override;
    void assignToMe(const QString &key, Done cb) override;

private:
    void fetchProjectPage(int startAt, QList<ProjectInfo> found, std::function<void(QList<ProjectInfo>, QString)> cb);
    QStringList fields() const;
    IssueRecord record(const QJsonObject &issue) const;
    void fetchHistoryPage(const QString &key, int startAt, std::shared_ptr<QList<HistoryEntry>> acc,
                          std::function<void(QList<HistoryEntry>, QString)> cb);
    void fetchCommentPage(const QString &key, int startAt, std::shared_ptr<QList<CommentRow>> acc,
                          std::function<void(QList<CommentRow>, QString)> cb);

    QString m_baseUrl;  // site URL for browser links
    JiraClient *m_client;
    Database *m_db;
    QString m_sprintField, m_rankField;  // site specific custom field ids
    QString m_me;                        // own accountId
};
