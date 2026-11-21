#pragma once
#include "Database.h"

#include <QObject>
#include <QTimer>
#include <functional>

class JiraClient;

// Keeps the local clone of the sync scope up to date:
//  * first run: full download of the scope (paged)
//  * then every 60 s: only issues with "updated >= -Nm"
//  * every 6 h: reconcile ids (issues deleted / moved out of scope)
//  * every 5 min: refresh state of open sprints
// Comments are NOT synced here; they are fetched on demand (see AccountContext).
class SyncEngine : public QObject {
    Q_OBJECT
public:
    SyncEngine(const QString &scopeJql, JiraClient *client, Database *db, QObject *parent = nullptr);

    void start();
    void syncNow();

    QStringList fields() const;
    FieldIds fieldIds() const { return m_fields; }
    QString scopeJql() const { return m_scope; }
    QString status() const { return m_status; }

signals:
    void statusChanged(const QString &status);
    void dataChanged();

private:
    void setStatus(const QString &s);
    void bootstrap(std::function<void()> next);
    void fetchPages(const QString &jql, const QString &token, int total, bool full,
                    std::function<void(bool ok, int count, QString err)> done);
    void afterSync();
    void reconcile();
    void fetchIds(const QString &token, std::shared_ptr<QSet<qint64>> acc, std::function<void(bool)> done);
    void refreshSprints();

    QString m_scope;
    JiraClient *m_client;
    Database *m_db;
    FieldIds m_fields;
    QTimer m_timer;
    bool m_running = false;
    bool m_bootstrapped = false;  // custom field ids + accountId known
    QString m_lastError;
    QString m_status;
};
