#include "SyncEngine.h"
#include "Backend.h"

#include <QDateTime>

static constexpr int kIntervalMs = 60 * 1000;
static constexpr qint64 kReconcileEveryMs = 6LL * 3600 * 1000;
static constexpr qint64 kSprintRefreshEveryMs = 5LL * 60 * 1000;
static constexpr int kCacheDays = 30;

SyncEngine::SyncEngine(const QString &scope, Backend *backend, Database *db, QObject *parent)
    : QObject(parent), m_scope(scope), m_backend(backend), m_db(db)
{
    m_timer.setInterval(kIntervalMs);
    connect(&m_timer, &QTimer::timeout, this, &SyncEngine::syncNow);
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

void SyncEngine::setScope(const QString &scope)
{
    if (scope == m_scope)
        return;
    m_scope = scope;
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

// Backend specifics (Jira: custom field ids, own accountId) before the first sync.
void SyncEngine::bootstrap(std::function<void()> next)
{
    setStatus("Verbinde …");
    m_backend->prepare([this, next](const QString &myId, const QString &err) {
        m_lastError = err;
        m_bootstrapped = err.isEmpty();
        if (!myId.isEmpty())
            m_db->setState("me", myId);
        next();
    });
}

void SyncEngine::syncNow()
{
    if (m_running)
        return;
    m_running = true;

    // Never sync before the backend is prepared (e.g. started while logged out / offline).
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
    int minutes = -1;
    if (!full) {
        const qint64 last = m_db->state("last_sync").toLongLong();
        minutes = int((started - last) / 60000 + 2);  // 2 min overlap
    }
    if (full)
        setStatus("Erstsync …");

    fetchPages(minutes, {}, 0, full, [this, full, started, scope = m_scope](bool ok, int count, const QString &err) {
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

void SyncEngine::fetchPages(int updatedWithinMinutes, const QString &cursor, int total, bool full,
                            std::function<void(bool, int, QString)> done)
{
    m_backend->fetchIssues(m_scope, updatedWithinMinutes, cursor,
                           [=, this](const IssuePage &page, const QString &err) {
        if (!err.isEmpty()) {
            done(false, total, err);
            return;
        }
        const int n = total + m_db->upsertIssues(page.issues, true, full);
        if (full) {
            setStatus(QString("Erstsync: %1 Issues …").arg(n));
            if (!page.issues.isEmpty())
                emit dataChanged();  // fill the UI progressively
        }
        if (!page.next.isEmpty())
            fetchPages(updatedWithinMinutes, page.next, n, full, done);
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
    if (!m_backend->hasSprints())
        return;
    for (const auto &s : m_db->openSprints()) {
        m_backend->fetchSprint(s.id, [this](const std::optional<SprintInfo> &sprint) {
            if (!sprint)
                return;
            m_db->updateSprint(*sprint);
            if (sprint->state != "active" && sprint->state != "future")
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

void SyncEngine::fetchIds(const QString &cursor, std::shared_ptr<QSet<qint64>> acc, std::function<void(bool)> done)
{
    m_backend->fetchIds(m_scope, cursor, [=, this](const QList<qint64> &ids, const QString &next, const QString &err) {
        if (!err.isEmpty()) {
            done(false);
            return;
        }
        for (qint64 id : ids)
            acc->insert(id);
        if (!next.isEmpty())
            fetchIds(next, acc, done);
        else
            done(true);
    });
}
