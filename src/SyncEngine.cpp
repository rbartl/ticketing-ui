#include "SyncEngine.h"
#include "JiraClient.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>

static constexpr int kIntervalMs = 60 * 1000;
static constexpr qint64 kReconcileEveryMs = 6LL * 3600 * 1000;
static constexpr qint64 kSprintRefreshEveryMs = 5LL * 60 * 1000;
static constexpr int kCacheDays = 30;

SyncEngine::SyncEngine(const QString &scopeJql, JiraClient *client, Database *db, QObject *parent)
    : QObject(parent), m_scope(scopeJql), m_client(client), m_db(db)
{
    m_fields.sprint = m_db->state("field_sprint");
    m_fields.rank = m_db->state("field_rank");
    m_timer.setInterval(kIntervalMs);
    connect(&m_timer, &QTimer::timeout, this, &SyncEngine::syncNow);
}

QStringList SyncEngine::fields() const
{
    QStringList f{"summary", "status", "issuetype", "priority", "assignee", "reporter",
                  "labels", "created", "updated", "description", "project"};
    if (!m_fields.sprint.isEmpty()) f << m_fields.sprint;
    if (!m_fields.rank.isEmpty()) f << m_fields.rank;
    return f;
}

void SyncEngine::setStatus(const QString &s)
{
    m_status = s;
    emit statusChanged(s);
}

void SyncEngine::start()
{
    // Scope changed since last run -> full resync.
    if (m_db->state("scope") != m_scope)
        resetForScope();
    m_db->evictCache(kCacheDays);
    m_timer.start();
    syncNow();
}

void SyncEngine::setScope(const QString &scopeJql)
{
    if (scopeJql == m_scope)
        return;
    m_scope = scopeJql;
    resetForScope();
    syncNow();  // if a sync is running, it notices the new scope when done and starts over
}

// Full download of the new scope; reconcile right after it moves issues that left the scope into the cache.
void SyncEngine::resetForScope()
{
    m_db->setState("scope", m_scope);
    m_db->setState("full_done", "0");
    m_db->setState("last_reconcile", "0");
}

// Discover site-specific custom fields (Sprint, Rank) and our own accountId.
void SyncEngine::bootstrap(std::function<void()> next)
{
    setStatus("Verbinde …");
    m_client->get("/rest/api/3/field", {}, [this, next](const QJsonDocument &doc, const QString &err) {
        m_lastError = err;
        if (err.isEmpty()) {
            m_bootstrapped = true;
            m_fields = {};
            for (const auto &v : doc.array()) {
                const auto o = v.toObject();
                const auto custom = o["schema"].toObject()["custom"].toString();
                if (custom == "com.pyxis.greenhopper.jira:gh-sprint")
                    m_fields.sprint = o["id"].toString();
                else if (custom == "com.pyxis.greenhopper.jira:gh-lexo-rank")
                    m_fields.rank = o["id"].toString();
            }
            m_db->setState("field_sprint", m_fields.sprint);
            m_db->setState("field_rank", m_fields.rank);
        }
        m_client->get("/rest/api/3/myself", {}, [this, next](const QJsonDocument &me, const QString &err2) {
            if (err2.isEmpty())
                m_db->setState("me", me["accountId"].toString());
            next();
        });
    });
}

void SyncEngine::syncNow()
{
    if (m_running)
        return;
    m_running = true;

    // Never sync without knowing the Sprint/Rank fields (e.g. started while logged out / offline).
    if (!m_bootstrapped) {
        bootstrap([this] {
            m_running = false;
            if (m_bootstrapped)
                syncNow();
            else
                setStatus("Offline/Fehler: " + m_lastError + " – nächster Versuch in 60 s");
        });
        return;
    }

    const bool full = m_db->state("full_done") != "1";
    const qint64 started = QDateTime::currentMSecsSinceEpoch();
    QString jql = "(" + m_scope + ")";
    if (!full) {
        const qint64 last = m_db->state("last_sync").toLongLong();
        const qint64 minutes = (started - last) / 60000 + 2;  // 2 min overlap, relative => no timezone issues
        jql += QString(" AND updated >= -%1m").arg(minutes);
    }
    jql += " ORDER BY updated ASC";
    if (full)
        setStatus("Erstsync …");

    fetchPages(jql, {}, 0, full, [this, full, started, scope = m_scope](bool ok, int count, const QString &err) {
        m_running = false;
        if (scope != m_scope) {  // scope edited meanwhile
            syncNow();
            return;
        }
        if (!ok) {
            setStatus("Offline/Fehler: " + err + " – nächster Versuch in 60 s");
            return;
        }
        m_db->setState("last_sync", QString::number(started));
        if (full)
            m_db->setState("full_done", "1");
        if (count > 0 && !full)
            emit dataChanged();
        setStatus(QString("Aktuell · %1").arg(QTime::currentTime().toString("HH:mm:ss")));
        afterSync();
    });
}

void SyncEngine::fetchPages(const QString &jql, const QString &token, int total, bool full,
                            std::function<void(bool, int, QString)> done)
{
    m_client->searchJql(jql, fields(), 100, token,
                        [=, this](const QJsonDocument &doc, const QString &err) {
        if (!err.isEmpty()) {
            done(false, total, err);
            return;
        }
        const auto o = doc.object();
        const auto issues = o["issues"].toArray();
        const int n = total + m_db->upsertIssues(issues, m_fields, true);
        if (full) {
            setStatus(QString("Erstsync: %1 Issues …").arg(n));
            if (!issues.isEmpty())
                emit dataChanged();  // fill the UI progressively
        }
        const QString next = o["nextPageToken"].toString();
        if (!o["isLast"].toBool(next.isEmpty()) && !next.isEmpty())
            fetchPages(jql, next, n, full, done);
        else
            done(true, n, {});
    });
}

void SyncEngine::afterSync()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_db->state("last_sprint_refresh").toLongLong() > kSprintRefreshEveryMs) {
        m_db->setState("last_sprint_refresh", QString::number(now));
        refreshSprints();
    }
    if (now - m_db->state("last_reconcile").toLongLong() > kReconcileEveryMs) {
        m_db->setState("last_reconcile", QString::number(now));
        reconcile();
    }
}

// Closing a sprint does not touch the done issues, so sprint state is refreshed explicitly.
void SyncEngine::refreshSprints()
{
    for (const auto &s : m_db->openSprints()) {
        m_client->get(QString("/rest/agile/1.0/sprint/%1").arg(s.id), {},
                      [this](const QJsonDocument &doc, const QString &err) {
            if (!err.isEmpty())
                return;
            const auto o = doc.object();
            m_db->updateSprint(o);
            const QString state = o["state"].toString().toLower();
            if (state != "active" && state != "future")
                emit dataChanged();  // sprint was closed -> drop it from the sprint list
        });
    }
}

// Issues that were deleted or left the scope are not reported by "updated >= ...".
void SyncEngine::reconcile()
{
    auto ids = std::make_shared<QSet<qint64>>();
    fetchIds({}, ids, [this, ids](bool ok) {
        if (!ok)
            return;
        QSet<qint64> gone = m_db->inScopeIds();
        gone.subtract(*ids);
        if (!gone.isEmpty()) {
            m_db->markOutOfScope(gone);  // become normal cache entries, evicted after 30 days
            emit dataChanged();
        }
        m_db->evictCache(kCacheDays);
    });
}

void SyncEngine::fetchIds(const QString &token, std::shared_ptr<QSet<qint64>> acc, std::function<void(bool)> done)
{
    m_client->searchJql("(" + m_scope + ")", {"id"}, 5000, token,
                        [=, this](const QJsonDocument &doc, const QString &err) {
        if (!err.isEmpty()) {
            done(false);
            return;
        }
        const auto o = doc.object();
        for (const auto &v : o["issues"].toArray())
            acc->insert(v.toObject()["id"].toString().toLongLong());
        const QString next = o["nextPageToken"].toString();
        if (!o["isLast"].toBool(next.isEmpty()) && !next.isEmpty())
            fetchIds(next, acc, done);
        else
            done(true);
    });
}
