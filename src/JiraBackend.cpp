#include "JiraBackend.h"
#include "Adf.h"
#include "JiraClient.h"

#include <QDateTime>
#include <algorithm>
#include <QJsonArray>
#include <QRegularExpression>

// Backend types ---------------------------------------------------------------------

const BackendType &BackendType::jira()
{
    static const BackendType t{
        "jira",
        "Jira Cloud",
        "JQL",
        "In Jira öffnen",
        "https://firma.atlassian.net",
        "API-Token",
        "Token erstellen unter <a href=\"https://id.atlassian.com/manage-profile/security/api-tokens\">"
        "id.atlassian.com → Sicherheit → API-Token</a>. Wird nur lokal auf diesem Rechner gespeichert.",
        &JiraBackend::scopeJql,
    };
    return t;
}

const BackendType &BackendType::byId(const QString &)
{
    return jira();  // the only one so far
}

Backend *Backend::create(const Account &account, AuthProvider *auth, Database *db, QObject *parent)
{
    return new JiraBackend(account, auth, db, parent);
}

// Helpers -------------------------------------------------------------------------

// Jira: "2026-11-19T09:40:12.345+0100" -> UTC ISO
static QString toUtcIso(const QString &jiraTime)
{
    if (jiraTime.isEmpty())
        return {};
    static const QRegularExpression offset("([+-]\\d\\d)(\\d\\d)$");  // +0100 -> +01:00
    QString s = jiraTime;
    s.replace(offset, "\\1:\\2");
    const QDateTime dt = QDateTime::fromString(s, Qt::ISODateWithMs);
    return dt.isValid() ? dt.toUTC().toString(Qt::ISODateWithMs) : jiraTime;
}

static SprintInfo sprintFromJson(const QJsonObject &s)
{
    SprintInfo r;
    r.id = s["id"].toVariant().toLongLong();
    r.name = s["name"].toString();
    r.state = s["state"].toString().toLower();
    r.boardId = s["boardId"].toVariant().toLongLong();
    r.start = toUtcIso(s["startDate"].toString());
    r.end = toUtcIso(s["endDate"].toString());
    r.goal = s["goal"].toString();
    return r;
}

// Plain text -> ADF, one paragraph per line.
static QJsonObject adfFromText(const QString &text)
{
    QJsonArray paragraphs;
    for (const QString &line : text.split('\n')) {
        QJsonObject p{{"type", "paragraph"}};
        if (!line.isEmpty())
            p["content"] = QJsonArray{QJsonObject{{"type", "text"}, {"text", line}}};
        paragraphs.append(p);
    }
    return {{"type", "doc"}, {"version", 1}, {"content", paragraphs}};
}

// JiraBackend ---------------------------------------------------------------------

JiraBackend::JiraBackend(const Account &account, AuthProvider *auth, Database *db, QObject *parent)
    : Backend(parent), m_baseUrl(account.baseUrl), m_db(db)
{
    m_client = new JiraClient(account.apiBase(), auth, this);
    if (m_db) {
        m_sprintField = m_db->state("field_sprint");
        m_rankField = m_db->state("field_rank");
        m_me = m_db->state("me");
    }
}

QString JiraBackend::scopeJql(const Account &account)
{
    const QString own = account.query.trimmed();
    if (!own.isEmpty())
        return own;
    QStringList parts;
    if (!account.projects.isEmpty()) {
        QStringList quoted;
        for (const QString &p : account.projects)
            quoted << '"' + p + '"';
        parts << "project in (" + quoted.join(", ") + ")";
    }
    if (account.maxAgeDays > 0)
        parts << QString("updated >= -%1d").arg(account.maxAgeDays);
    // All tickets: /search/jql rejects JQL without a restriction.
    return parts.isEmpty() ? "project IS NOT EMPTY" : parts.join(" AND ");
}

QStringList JiraBackend::fields() const
{
    QStringList f{"summary", "status", "issuetype", "priority", "assignee", "reporter",
                  "labels", "created", "updated", "description", "project", "watches"};
    if (!m_sprintField.isEmpty()) f << m_sprintField;
    if (!m_rankField.isEmpty()) f << m_rankField;
    return f;
}

IssueRecord JiraBackend::record(const QJsonObject &issue) const
{
    const auto f = issue["fields"].toObject();
    const auto st = f["status"].toObject();
    IssueRecord r;
    r.id = issue["id"].toString().toLongLong();
    r.key = issue["key"].toString();
    r.summary = f["summary"].toString();
    r.status = st["name"].toString();
    r.statusCat = st["statusCategory"].toObject()["key"].toString();  // new | indeterminate | done
    r.assignee = f["assignee"].toObject()["displayName"].toString();
    r.assigneeId = f["assignee"].toObject()["accountId"].toString();
    r.watching = f["watches"].toObject()["isWatching"].toBool();
    r.priority = f["priority"].toObject()["name"].toString();
    r.issueType = f["issuetype"].toObject()["name"].toString();
    r.updated = toUtcIso(f["updated"].toString());
    r.created = toUtcIso(f["created"].toString());
    r.project = f["project"].toObject()["key"].toString();
    r.reporter = f["reporter"].toObject()["displayName"].toString();
    QStringList labels;
    for (const auto &l : f["labels"].toArray())
        labels << l.toString();
    r.labels = labels.join(' ');
    if (!m_rankField.isEmpty())
        r.rank = f[m_rankField].toString();
    r.descriptionHtml = Adf::toHtml(f["description"]);
    r.descriptionText = Adf::toText(f["description"]);
    if (!m_sprintField.isEmpty() && f.contains(m_sprintField)) {
        QList<SprintInfo> sprints;
        for (const auto &sv : f[m_sprintField].toArray())
            if (!sv.toObject().isEmpty())
                sprints << sprintFromJson(sv.toObject());
        r.sprints = sprints;
    }
    return r;
}

void JiraBackend::checkLogin(std::function<void(QString, QString)> cb)
{
    m_client->get("/rest/api/3/myself", {}, [cb](const QJsonDocument &doc, const QString &err) {
        cb(doc["displayName"].toString(), err);
    });
}

void JiraBackend::fetchProjects(std::function<void(QList<ProjectInfo>, QString)> cb)
{
    fetchProjectPage(0, {}, std::move(cb));
}

void JiraBackend::fetchProjectPage(int startAt, QList<ProjectInfo> found,
                                   std::function<void(QList<ProjectInfo>, QString)> cb)
{
    const QUrlQuery query{{"startAt", QString::number(startAt)}, {"maxResults", "100"}, {"orderBy", "name"}};
    m_client->get("/rest/api/3/project/search", query,
                  [this, startAt, found, cb](const QJsonDocument &doc, const QString &err) mutable {
        if (!err.isEmpty()) {
            cb({}, err);
            return;
        }
        const QJsonArray values = doc["values"].toArray();
        for (const auto &v : values)
            found << ProjectInfo{v.toObject()["key"].toString(), v.toObject()["name"].toString()};
        if (doc["isLast"].toBool(true) || values.isEmpty())
            cb(found, {});
        else
            fetchProjectPage(startAt + int(values.size()), found, cb);
    });
}

// Sync ----------------------------------------------------------------------------

// Discover site-specific custom fields (Sprint, Rank) and our own accountId.
void JiraBackend::prepare(std::function<void(QString, QString)> cb)
{
    m_client->get("/rest/api/3/field", {}, [this, cb](const QJsonDocument &doc, const QString &err) {
        if (!err.isEmpty()) {
            cb({}, err);
            return;
        }
        m_sprintField.clear();
        m_rankField.clear();
        for (const auto &v : doc.array()) {
            const auto o = v.toObject();
            const auto custom = o["schema"].toObject()["custom"].toString();
            if (custom == "com.pyxis.greenhopper.jira:gh-sprint")
                m_sprintField = o["id"].toString();
            else if (custom == "com.pyxis.greenhopper.jira:gh-lexo-rank")
                m_rankField = o["id"].toString();
        }
        if (m_db) {
            m_db->setState("field_sprint", m_sprintField);
            m_db->setState("field_rank", m_rankField);
        }
        m_client->get("/rest/api/3/myself", {}, [this, cb](const QJsonDocument &me, const QString &err2) {
            if (err2.isEmpty()) {
                m_me = me["accountId"].toString();
                if (m_db)
                    m_db->setState("me", m_me);
            }
            cb(m_me, err2);
        });
    });
}

void JiraBackend::fetchIssues(const QString &scope, int updatedWithinMinutes, const QString &cursor,
                              std::function<void(IssuePage, QString)> cb)
{
    QString jql = "(" + scope + ")";
    if (updatedWithinMinutes >= 0)  // relative => no timezone issues
        jql += QString(" AND updated >= -%1m").arg(updatedWithinMinutes);
    jql += " ORDER BY updated ASC";
    m_client->searchJql(jql, fields(), 100, cursor, [this, cb](const QJsonDocument &doc, const QString &err) {
        if (!err.isEmpty()) {
            cb({}, err);
            return;
        }
        const auto o = doc.object();
        IssuePage page;
        for (const auto &v : o["issues"].toArray())
            page.issues << record(v.toObject());
        const QString next = o["nextPageToken"].toString();
        if (!o["isLast"].toBool(next.isEmpty()))
            page.next = next;
        cb(page, {});
    });
}

void JiraBackend::fetchIds(const QString &scope, const QString &cursor,
                           std::function<void(QList<qint64>, QString, QString)> cb)
{
    m_client->searchJql("(" + scope + ")", {"id"}, 5000, cursor, [cb](const QJsonDocument &doc, const QString &err) {
        if (!err.isEmpty()) {
            cb({}, {}, err);
            return;
        }
        const auto o = doc.object();
        QList<qint64> ids;
        for (const auto &v : o["issues"].toArray())
            ids << v.toObject()["id"].toString().toLongLong();
        const QString next = o["nextPageToken"].toString();
        cb(ids, o["isLast"].toBool(next.isEmpty()) ? QString() : next, {});
    });
}

void JiraBackend::fetchSprint(qint64 id, std::function<void(std::optional<SprintInfo>)> cb)
{
    m_client->get(QString("/rest/agile/1.0/sprint/%1").arg(id), {}, [cb](const QJsonDocument &doc, const QString &err) {
        if (err.isEmpty())
            cb(sprintFromJson(doc.object()));
        else
            cb(std::nullopt);
    });
}

// Single issues and the cloud fallback ----------------------------------------------

void JiraBackend::fetchIssue(const QString &key, std::function<void(std::optional<IssueRecord>, QString)> cb)
{
    m_client->get("/rest/api/3/issue/" + key, {{"fields", fields().join(',')}},
                  [this, cb](const QJsonDocument &doc, const QString &err) {
        if (!err.isEmpty())
            cb(std::nullopt, err);
        else
            cb(record(doc.object()), {});
    });
}

void JiraBackend::searchText(const QString &text, const QString &scope,
                             std::function<void(QList<IssueRow>, QString)> cb)
{
    static const QRegularExpression keyRe("^[A-Za-z][A-Za-z0-9_]*-\\d+$");
    const QString t = text.trimmed();

    if (keyRe.match(t).hasMatch()) {
        fetchIssue(t.toUpper(), [cb](std::optional<IssueRecord> r, const QString &err) {
            if (!r)
                cb({}, err.startsWith("HTTP 404") ? QString() : err);
            else
                cb({*r}, {});
        });
        return;
    }

    QString escaped = t;
    escaped.replace('\\', "\\\\").replace('"', "\\\"");
    const QString jql = QString("text ~ \"%1\" AND NOT (%2) ORDER BY updated DESC").arg(escaped, scope);
    m_client->searchJql(jql, {"summary", "status", "assignee", "priority", "issuetype", "updated", "created",
                              "reporter", "labels", "project"}, 50, {},
                        [this, cb](const QJsonDocument &doc, const QString &err) {
        if (!err.isEmpty()) {
            cb({}, err);
            return;
        }
        QList<IssueRow> rows;
        for (const auto &v : doc["issues"].toArray())
            rows << record(v.toObject());
        cb(rows, {});
    });
}

void JiraBackend::fetchComments(const QString &key, std::function<void(QList<CommentRow>, QString)> cb)
{
    fetchCommentPage(key, 0, std::make_shared<QList<CommentRow>>(), cb);
}

void JiraBackend::fetchCommentPage(const QString &key, int startAt, std::shared_ptr<QList<CommentRow>> acc,
                                   std::function<void(QList<CommentRow>, QString)> cb)
{
    QUrlQuery q{{"startAt", QString::number(startAt)}, {"maxResults", "100"}, {"orderBy", "created"}};
    m_client->get("/rest/api/3/issue/" + key + "/comment", q,
                  [=, this](const QJsonDocument &doc, const QString &err) {
        if (!err.isEmpty()) {
            cb({}, err);
            return;
        }
        const auto comments = doc["comments"].toArray();
        for (const auto &v : comments) {
            const auto c = v.toObject();
            acc->append(CommentRow{
                c["id"].toString(),
                c["author"].toObject()["displayName"].toString(),
                toUtcIso(c["created"].toString()),
                toUtcIso(c["updated"].toString()),
                Adf::toHtml(c["body"]),
            });
        }
        const int next = startAt + int(comments.size());
        if (!comments.isEmpty() && next < doc["total"].toInt())
            fetchCommentPage(key, next, acc, cb);
        else
            cb(*acc, {});
    });
}

void JiraBackend::fetchHistory(const QString &key, std::function<void(QList<HistoryEntry>, QString)> cb)
{
    fetchHistoryPage(key, 0, std::make_shared<QList<HistoryEntry>>(), cb);
}

void JiraBackend::fetchHistoryPage(const QString &key, int startAt, std::shared_ptr<QList<HistoryEntry>> acc,
                                   std::function<void(QList<HistoryEntry>, QString)> cb)
{
    QUrlQuery q{{"startAt", QString::number(startAt)}, {"maxResults", "100"}};
    m_client->get("/rest/api/3/issue/" + key + "/changelog", q,
                  [=, this](const QJsonDocument &doc, const QString &err) {
        if (!err.isEmpty()) {
            cb({}, err);
            return;
        }
        const auto values = doc["values"].toArray();
        for (const auto &v : values) {
            const auto h = v.toObject();
            QStringList lines;
            for (const auto &iv : h["items"].toArray()) {
                const auto item = iv.toObject();
                const QString from = item["fromString"].toString(), to = item["toString"].toString();
                const QString field = item["field"].toString();
                if (field == "description")
                    lines << "Beschreibung geändert";
                else
                    lines << QString("%1: %2 → %3").arg(field, from.isEmpty() ? "–" : from, to.isEmpty() ? "–" : to);
            }
            acc->append(HistoryEntry{toUtcIso(h["created"].toString()), h["author"].toObject()["displayName"].toString(),
                                     lines.join('\n')});
        }
        const int next = startAt + int(values.size());
        if (!values.isEmpty() && !doc["isLast"].toBool(next >= doc["total"].toInt()))
            fetchHistoryPage(key, next, acc, cb);
        else {
            std::reverse(acc->begin(), acc->end());
            cb(*acc, {});
        }
    });
}

// Detail view -----------------------------------------------------------------------

QList<DetailField> JiraBackend::detailFields(const IssueDetail &d) const
{
    QList<DetailField> f{
        {"Typ", d.issueType},
        {"Priorität", d.priority},
        {"Zugewiesen", d.assignee},
        {"Melder", d.reporter},
    };
    if (!d.sprint.isEmpty())
        f << DetailField{"Sprint", d.sprint};
    f << DetailField{"Erstellt", d.created, DetailField::Time} << DetailField{"Geändert", d.updated, DetailField::Time};
    if (!d.labels.isEmpty())
        f << DetailField{"Labels", d.labels, DetailField::Tags};
    return f;
}

QString JiraBackend::browseUrl(const QString &key) const
{
    QString base = m_baseUrl;
    while (base.endsWith('/'))
        base.chop(1);
    return base + "/browse/" + key;
}

// Actions ---------------------------------------------------------------------------

void JiraBackend::transitions(const QString &key, std::function<void(QList<Transition>, QString)> cb)
{
    m_client->get("/rest/api/3/issue/" + key + "/transitions", {}, [cb](const QJsonDocument &doc, const QString &err) {
        QList<Transition> out;
        for (const auto &v : doc["transitions"].toArray()) {
            const auto t = v.toObject();
            const QString to = t["to"].toObject()["name"].toString();
            out << Transition{t["id"].toString(), to.isEmpty() ? t["name"].toString() : to};
        }
        cb(out, err);
    });
}

void JiraBackend::transition(const QString &key, const QString &transitionId, Done cb)
{
    m_client->post("/rest/api/3/issue/" + key + "/transitions", {{"transition", QJsonObject{{"id", transitionId}}}},
                   [cb](const QJsonDocument &, const QString &err) { cb(err); });
}

void JiraBackend::addComment(const QString &key, const QString &text, Done cb)
{
    m_client->post("/rest/api/3/issue/" + key + "/comment", {{"body", adfFromText(text)}},
                   [cb](const QJsonDocument &, const QString &err) { cb(err); });
}

void JiraBackend::assignToMe(const QString &key, Done cb)
{
    if (m_me.isEmpty()) {
        cb("Eigene Benutzer-ID noch unbekannt – bitte nach dem ersten Sync erneut versuchen.");
        return;
    }
    m_client->put("/rest/api/3/issue/" + key + "/assignee", {{"accountId", m_me}},
                  [cb](const QJsonDocument &, const QString &err) { cb(err); });
}
