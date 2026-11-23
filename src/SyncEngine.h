#pragma once
#include "Database.h"

#include <QObject>
#include <QTimer>
#include <functional>

class Backend;

// Keeps the local clone of the sync scope up to date (backend neutral, see Backend):
//  * first run: full download of the scope (paged)
//  * then every 60 s: only issues with "updated >= -Nm"
//  * every 6 h: reconcile ids (issues deleted / moved out of scope)
//  * every 5 min: refresh state of open sprints
// Comments are NOT synced here; they are fetched on demand (see AccountContext).
// The scope is a query in the backend's language (Jira: JQL).
class SyncEngine : public QObject {
    Q_OBJECT
public:
    SyncEngine(const QString &scope, Backend *backend, Database *db, QObject *parent = nullptr);

    void start();
    void syncNow();
    void setScope(const QString &scope);  // account edited -> full resync + reconcile

    QString scope() const { return m_scope; }
    QString status() const { return m_status; }

signals:
    void statusChanged(const QString &status);
    void dataChanged();

private:
    void setStatus(const QString &s);
    void resetForScope();
    void bootstrap(std::function<void()> next);
    void fetchPages(int updatedWithinMinutes, const QString &cursor, int total, bool full,
                    std::function<void(bool ok, int count, QString err)> done);
    void afterSync();
    void reconcile();
    void fetchIds(const QString &cursor, std::shared_ptr<QSet<qint64>> acc, std::function<void(bool)> done);
    void refreshSprints();

    QString m_scope;
    Backend *m_backend;
    Database *m_db;
    QTimer m_timer;
    bool m_running = false;
    bool m_bootstrapped = false;  // Backend::prepare done
    QString m_lastError;
    QString m_status;
};
