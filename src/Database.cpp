#include "Database.h"
#include "Adf.h"

#include <QDateTime>
#include <QHash>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace {

const QString kCols = "i.id,i.key,i.summary,i.status,i.status_cat,i.assignee,i.priority,"
                      "i.issue_type,i.updated,i.rank,i.in_scope";

IssueRow readRow(const QSqlQuery &q)
{
    IssueRow r;
    r.id = q.value(0).toLongLong();
    r.key = q.value(1).toString();
    r.summary = q.value(2).toString();
    r.status = q.value(3).toString();
    r.statusCat = q.value(4).toString();
    r.assignee = q.value(5).toString();
    r.priority = q.value(6).toString();
    r.issueType = q.value(7).toString();
    r.updated = q.value(8).toString();
    r.rank = q.value(9).toString();
    r.inScope = q.value(10).toInt() != 0;
    return r;
}

QList<IssueRow> readRows(QSqlQuery &q)
{
    QList<IssueRow> out;
    while (q.next())
        out << readRow(q);
    return out;
}

// "login time" -> "login"* "time"*   (all terms, prefix match)
QString ftsQuery(const QString &input)
{
    static const QRegularExpression ws("\\s+");
    QStringList parts;
    for (QString t : input.split(ws, Qt::SkipEmptyParts)) {
        t.replace('"', "\"\"");
        parts << '"' + t + "\"*";
    }
    return parts.join(' ');
}

const QRegularExpression &issueKeyRe()
{
    static const QRegularExpression re("^[A-Za-z][A-Za-z0-9_]*-\\d+$");
    return re;
}

QVariant nullable(const QString &s) { return s.isEmpty() ? QVariant() : QVariant(s); }

} // namespace

Database::~Database()
{
    if (m_conn.isEmpty())
        return;
    {
        QSqlDatabase d = QSqlDatabase::database(m_conn, false);
        d.close();
    }
    QSqlDatabase::removeDatabase(m_conn);
}

QSqlDatabase Database::db() const { return QSqlDatabase::database(m_conn, false); }

bool Database::open(const QString &path, const QString &connectionName, QString *err)
{
    m_conn = connectionName;
    QSqlDatabase d = QSqlDatabase::addDatabase("QSQLITE", m_conn);
    d.setDatabaseName(path);
    if (!d.open()) {
        *err = d.lastError().text();
        return false;
    }
    const char *schema[] = {
        "PRAGMA journal_mode=WAL",
        "PRAGMA synchronous=NORMAL",
        "PRAGMA temp_store=MEMORY",
        "PRAGMA mmap_size=268435456",
        "CREATE TABLE IF NOT EXISTS kv(k TEXT PRIMARY KEY, v TEXT)",
        "CREATE TABLE IF NOT EXISTS issues("
        " id INTEGER PRIMARY KEY, key TEXT UNIQUE NOT NULL, project TEXT, summary TEXT,"
        " status TEXT, status_cat TEXT, issue_type TEXT, priority TEXT,"
        " assignee TEXT, assignee_id TEXT, reporter TEXT, labels TEXT,"
        " created TEXT, updated TEXT, rank TEXT, description_html TEXT,"
        " in_scope INTEGER NOT NULL DEFAULT 1, last_viewed INTEGER)",
        "CREATE INDEX IF NOT EXISTS ix_issues_assignee ON issues(assignee_id, status_cat)",
        "CREATE INDEX IF NOT EXISTS ix_issues_updated ON issues(updated)",
        "CREATE TABLE IF NOT EXISTS sprints("
        " id INTEGER PRIMARY KEY, name TEXT, state TEXT, board_id INTEGER,"
        " start_date TEXT, end_date TEXT, goal TEXT)",
        "CREATE TABLE IF NOT EXISTS issue_sprints("
        " issue_id INTEGER NOT NULL, sprint_id INTEGER NOT NULL, PRIMARY KEY(issue_id, sprint_id))",
        "CREATE INDEX IF NOT EXISTS ix_issue_sprints_sprint ON issue_sprints(sprint_id)",
        "CREATE VIRTUAL TABLE IF NOT EXISTS issues_fts USING fts5("
        " key, summary, description, labels,"
        " tokenize='unicode61 remove_diacritics 2', prefix='2 3')",
        "CREATE TABLE IF NOT EXISTS comments("
        " issue_key TEXT NOT NULL, id TEXT NOT NULL, author TEXT, created TEXT, updated TEXT,"
        " body_html TEXT, PRIMARY KEY(issue_key, id))",
        "CREATE TABLE IF NOT EXISTS comment_fetch(issue_key TEXT PRIMARY KEY, fetched_at INTEGER)",
    };
    for (const char *stmt : schema) {
        QSqlQuery q(d);
        if (!q.exec(QString::fromLatin1(stmt))) {
            *err = QString("%1\n(%2)").arg(q.lastError().text(), QString::fromLatin1(stmt));
            return false;
        }
    }
    return true;
}

QString Database::state(const QString &key) const
{
    QSqlQuery q(db());
    q.prepare("SELECT v FROM kv WHERE k=?");
    q.addBindValue(key);
    q.exec();
    return q.next() ? q.value(0).toString() : QString();
}

void Database::setState(const QString &key, const QString &value)
{
    QSqlQuery q(db());
    q.prepare("INSERT INTO kv(k,v) VALUES(?,?) ON CONFLICT(k) DO UPDATE SET v=excluded.v");
    q.addBindValue(key);
    q.addBindValue(value);
    q.exec();
}

QString Database::toUtcIso(const QString &jiraTime)
{
    if (jiraTime.isEmpty())
        return {};
    static const QRegularExpression offset("([+-]\\d\\d)(\\d\\d)$");  // +0100 -> +01:00
    QString s = jiraTime;
    s.replace(offset, "\\1:\\2");
    const QDateTime dt = QDateTime::fromString(s, Qt::ISODateWithMs);
    return dt.isValid() ? dt.toUTC().toString(Qt::ISODateWithMs) : jiraTime;
}

IssueRow Database::rowFromJson(const QJsonObject &issue)
{
    const auto f = issue["fields"].toObject();
    const auto st = f["status"].toObject();
    IssueRow r;
    r.id = issue["id"].toString().toLongLong();
    r.key = issue["key"].toString();
    r.summary = f["summary"].toString();
    r.status = st["name"].toString();
    r.statusCat = st["statusCategory"].toObject()["key"].toString();
    r.assignee = f["assignee"].toObject()["displayName"].toString();
    r.priority = f["priority"].toObject()["name"].toString();
    r.issueType = f["issuetype"].toObject()["name"].toString();
    r.updated = toUtcIso(f["updated"].toString());
    return r;
}

void Database::upsertSprint(QSqlQuery &q, const QJsonObject &s)
{
    q.bindValue(":id", s["id"].toVariant().toLongLong());
    q.bindValue(":name", s["name"].toString());
    q.bindValue(":state", s["state"].toString().toLower());
    q.bindValue(":board", s["boardId"].toVariant().toLongLong());
    q.bindValue(":start", nullable(toUtcIso(s["startDate"].toString())));
    q.bindValue(":end", nullable(toUtcIso(s["endDate"].toString())));
    q.bindValue(":goal", s["goal"].toString());
    q.exec();
}

static const char *kUpsertSprint =
    "INSERT INTO sprints(id,name,state,board_id,start_date,end_date,goal)"
    " VALUES(:id,:name,:state,:board,:start,:end,:goal)"
    " ON CONFLICT(id) DO UPDATE SET name=excluded.name, state=excluded.state,"
    " board_id=excluded.board_id, start_date=excluded.start_date,"
    " end_date=excluded.end_date, goal=excluded.goal";

int Database::upsertIssues(const QJsonArray &issues, const FieldIds &fieldIds, bool inScope)
{
    QSqlDatabase d = db();
    d.transaction();

    QSqlQuery up(d);
    up.prepare(
        "INSERT INTO issues(id,key,project,summary,status,status_cat,issue_type,priority,assignee,"
        " assignee_id,reporter,labels,created,updated,rank,description_html,in_scope)"
        " VALUES(:id,:key,:project,:summary,:status,:cat,:type,:prio,:assignee,"
        " :assignee_id,:reporter,:labels,:created,:updated,:rank,:desc,:scope)"
        " ON CONFLICT(id) DO UPDATE SET key=excluded.key, project=excluded.project,"
        " summary=excluded.summary, status=excluded.status, status_cat=excluded.status_cat,"
        " issue_type=excluded.issue_type, priority=excluded.priority, assignee=excluded.assignee,"
        " assignee_id=excluded.assignee_id, reporter=excluded.reporter, labels=excluded.labels,"
        " created=excluded.created, updated=excluded.updated,"
        " rank=COALESCE(excluded.rank, issues.rank), description_html=excluded.description_html,"
        " in_scope=MAX(issues.in_scope, excluded.in_scope)");
    QSqlQuery delFts(d), insFts(d), delSprints(d), insIssueSprint(d), upSprint(d);
    delFts.prepare("DELETE FROM issues_fts WHERE rowid=?");
    insFts.prepare("INSERT INTO issues_fts(rowid,key,summary,description,labels) VALUES(?,?,?,?,?)");
    delSprints.prepare("DELETE FROM issue_sprints WHERE issue_id=?");
    insIssueSprint.prepare("INSERT OR IGNORE INTO issue_sprints(issue_id,sprint_id) VALUES(?,?)");
    upSprint.prepare(kUpsertSprint);

    int n = 0;
    for (const auto &v : issues) {
        const auto issue = v.toObject();
        const auto f = issue["fields"].toObject();
        const IssueRow r = rowFromJson(issue);
        if (r.id == 0)
            continue;

        QStringList labels;
        for (const auto &l : f["labels"].toArray())
            labels << l.toString();
        const QString rank = fieldIds.rank.isEmpty() ? QString() : f[fieldIds.rank].toString();

        up.bindValue(":id", r.id);
        up.bindValue(":key", r.key);
        up.bindValue(":project", f["project"].toObject()["key"].toString());
        up.bindValue(":summary", r.summary);
        up.bindValue(":status", r.status);
        up.bindValue(":cat", r.statusCat);
        up.bindValue(":type", r.issueType);
        up.bindValue(":prio", r.priority);
        up.bindValue(":assignee", r.assignee);
        up.bindValue(":assignee_id", f["assignee"].toObject()["accountId"].toString());
        up.bindValue(":reporter", f["reporter"].toObject()["displayName"].toString());
        up.bindValue(":labels", labels.join(' '));
        up.bindValue(":created", toUtcIso(f["created"].toString()));
        up.bindValue(":updated", r.updated);
        up.bindValue(":rank", nullable(rank));
        up.bindValue(":desc", Adf::toHtml(f["description"]));
        up.bindValue(":scope", inScope ? 1 : 0);
        up.exec();

        delFts.addBindValue(r.id);
        delFts.exec();
        insFts.addBindValue(r.id);
        insFts.addBindValue(r.key);
        insFts.addBindValue(r.summary);
        insFts.addBindValue(Adf::toText(f["description"]));
        insFts.addBindValue(labels.join(' '));
        insFts.exec();

        if (!fieldIds.sprint.isEmpty() && f.contains(fieldIds.sprint)) {
            delSprints.addBindValue(r.id);
            delSprints.exec();
            for (const auto &sv : f[fieldIds.sprint].toArray()) {
                const auto s = sv.toObject();
                if (s.isEmpty())
                    continue;
                upsertSprint(upSprint, s);
                insIssueSprint.addBindValue(r.id);
                insIssueSprint.addBindValue(s["id"].toVariant().toLongLong());
                insIssueSprint.exec();
            }
        }
        ++n;
    }
    d.commit();
    return n;
}

QList<IssueRow> Database::search(const QString &text, int limit) const
{
    QList<IssueRow> out;
    const QString t = text.trimmed();
    if (t.isEmpty())
        return out;

    if (issueKeyRe().match(t).hasMatch()) {
        QSqlQuery q(db());
        q.prepare("SELECT " + kCols + " FROM issues i WHERE i.key=?");
        q.addBindValue(t.toUpper());
        q.exec();
        out = readRows(q);
    }

    QSqlQuery q(db());
    q.prepare("SELECT " + kCols + " FROM issues_fts JOIN issues i ON i.id = issues_fts.rowid"
              " WHERE issues_fts MATCH ? ORDER BY bm25(issues_fts, 10.0, 5.0, 1.0, 2.0) LIMIT ?");
    q.addBindValue(ftsQuery(t));
    q.addBindValue(limit);
    q.exec();
    while (q.next()) {
        IssueRow r = readRow(q);
        if (out.isEmpty() || out.first().id != r.id)
            out << r;
    }
    return out;
}

QList<IssueRow> Database::myOpen(const QString &accountId, int limit) const
{
    QSqlQuery q(db());
    if (accountId.isEmpty()) {
        q.prepare("SELECT " + kCols + " FROM issues i WHERE i.in_scope=1 ORDER BY i.updated DESC LIMIT ?");
    } else {
        q.prepare("SELECT " + kCols + " FROM issues i WHERE i.assignee_id=? AND i.status_cat<>'done'"
                  " ORDER BY i.updated DESC LIMIT ?");
        q.addBindValue(accountId);
    }
    q.addBindValue(limit);
    q.exec();
    return readRows(q);
}

std::optional<IssueDetail> Database::issue(const QString &key) const
{
    QSqlQuery q(db());
    q.prepare("SELECT " + kCols + ",i.project,i.reporter,i.labels,i.created,i.description_html"
              " FROM issues i WHERE i.key=?");
    q.addBindValue(key);
    q.exec();
    if (!q.next())
        return std::nullopt;
    IssueDetail d;
    static_cast<IssueRow &>(d) = readRow(q);
    d.project = q.value(11).toString();
    d.reporter = q.value(12).toString();
    d.labels = q.value(13).toString();
    d.created = q.value(14).toString();
    d.descriptionHtml = q.value(15).toString();
    return d;
}

void Database::touchViewed(const QString &key)
{
    QSqlQuery q(db());
    q.prepare("UPDATE issues SET last_viewed=? WHERE key=?");
    q.addBindValue(QDateTime::currentSecsSinceEpoch());
    q.addBindValue(key);
    q.exec();
}

QSet<qint64> Database::inScopeIds() const
{
    QSet<qint64> ids;
    QSqlQuery q(db());
    q.exec("SELECT id FROM issues WHERE in_scope=1");
    while (q.next())
        ids.insert(q.value(0).toLongLong());
    return ids;
}

void Database::markOutOfScope(const QSet<qint64> &ids)
{
    QSqlDatabase d = db();
    d.transaction();
    QSqlQuery up(d), del(d);
    up.prepare("UPDATE issues SET in_scope=0 WHERE id=?");
    del.prepare("DELETE FROM issue_sprints WHERE issue_id=?");
    for (qint64 id : ids) {
        up.addBindValue(id);
        up.exec();
        del.addBindValue(id);
        del.exec();
    }
    d.commit();
}

void Database::evictCache(int days)
{
    const qint64 cutoff = QDateTime::currentSecsSinceEpoch() - qint64(days) * 86400;
    const QString cond = QString("in_scope=0 AND COALESCE(last_viewed,0) < %1").arg(cutoff);
    QSqlDatabase d = db();
    d.transaction();
    QSqlQuery q(d);
    q.exec("DELETE FROM issues_fts WHERE rowid IN (SELECT id FROM issues WHERE " + cond + ")");
    q.exec("DELETE FROM issue_sprints WHERE issue_id IN (SELECT id FROM issues WHERE " + cond + ")");
    q.exec("DELETE FROM issues WHERE " + cond);
    q.exec("DELETE FROM comments WHERE issue_key NOT IN (SELECT key FROM issues)");
    q.exec("DELETE FROM comment_fetch WHERE issue_key NOT IN (SELECT key FROM issues)");
    d.commit();
}

QList<SprintInfo> Database::openSprints() const
{
    QList<SprintInfo> out;
    QSqlQuery q(db());
    q.exec("SELECT id,name,state,start_date,end_date,board_id FROM sprints s"
           " WHERE state IN ('active','future')"
           "   AND EXISTS (SELECT 1 FROM issue_sprints x WHERE x.sprint_id = s.id)"
           " ORDER BY CASE state WHEN 'active' THEN 0 ELSE 1 END, start_date, name");
    while (q.next()) {
        SprintInfo s;
        s.id = q.value(0).toLongLong();
        s.name = q.value(1).toString();
        s.state = q.value(2).toString();
        s.start = q.value(3).toString();
        s.end = q.value(4).toString();
        s.boardId = q.value(5).toLongLong();
        out << s;
    }
    return out;
}

QList<IssueRow> Database::sprintIssues(qint64 sprintId) const
{
    QSqlQuery q(db());
    q.prepare("SELECT " + kCols + " FROM issue_sprints s JOIN issues i ON i.id = s.issue_id"
              " WHERE s.sprint_id=?"
              " ORDER BY CASE i.status_cat WHEN 'new' THEN 0 WHEN 'indeterminate' THEN 1 ELSE 2 END,"
              " i.rank");
    q.addBindValue(sprintId);
    q.exec();
    return readRows(q);
}

void Database::updateSprint(const QJsonObject &sprint)
{
    QSqlQuery q(db());
    q.prepare(kUpsertSprint);
    upsertSprint(q, sprint);
}

QList<CommentRow> Database::comments(const QString &issueKey) const
{
    QList<CommentRow> out;
    QSqlQuery q(db());
    q.prepare("SELECT id,author,created,updated,body_html FROM comments WHERE issue_key=? ORDER BY created");
    q.addBindValue(issueKey);
    q.exec();
    while (q.next())
        out << CommentRow{q.value(0).toString(), q.value(1).toString(), q.value(2).toString(),
                          q.value(3).toString(), q.value(4).toString()};
    return out;
}

qint64 Database::commentsFetchedAt(const QString &issueKey) const
{
    QSqlQuery q(db());
    q.prepare("SELECT fetched_at FROM comment_fetch WHERE issue_key=?");
    q.addBindValue(issueKey);
    q.exec();
    return q.next() ? q.value(0).toLongLong() : 0;
}

bool Database::replaceComments(const QString &issueKey, const QList<CommentRow> &rows)
{
    const auto old = comments(issueKey);
    bool changed = old.size() != rows.size();
    for (int i = 0; !changed && i < rows.size(); ++i)
        changed = old[i].id != rows[i].id || old[i].updated != rows[i].updated;

    QSqlDatabase d = db();
    d.transaction();
    QSqlQuery q(d);
    if (changed) {
        q.prepare("DELETE FROM comments WHERE issue_key=?");
        q.addBindValue(issueKey);
        q.exec();
        q.prepare("INSERT INTO comments(issue_key,id,author,created,updated,body_html) VALUES(?,?,?,?,?,?)");
        for (const auto &c : rows) {
            q.addBindValue(issueKey);
            q.addBindValue(c.id);
            q.addBindValue(c.author);
            q.addBindValue(c.created);
            q.addBindValue(c.updated);
            q.addBindValue(c.html);
            q.exec();
        }
    }
    q.prepare("INSERT INTO comment_fetch(issue_key,fetched_at) VALUES(?,?)"
              " ON CONFLICT(issue_key) DO UPDATE SET fetched_at=excluded.fetched_at");
    q.addBindValue(issueKey);
    q.addBindValue(QDateTime::currentSecsSinceEpoch());
    q.exec();
    d.commit();
    // First fetch of an issue without comments still counts as a change (removes "Lade …").
    return changed || old.isEmpty();
}
