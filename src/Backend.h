#pragma once
#include "Account.h"
#include "Database.h"

#include <QList>
#include <QObject>
#include <QString>
#include <functional>
#include <optional>

// A ticket system behind one account (Jira Cloud today; e.g. Request Tracker later).
// UI, models, sync and the local db only talk to this interface, never to a system's own API or JSON.

// Static description of a backend type: what the account form and the UI need before an account exists.
struct BackendType {
    struct Preset {
        QString label;  // "Mir zugewiesen"
        QString query;  // in the backend's query language; empty = all tickets
    };
    QString id;           // stored in accounts.json ("jira")
    QString name;         // "Jira Cloud"
    QString queryLabel;   // name of the query language: "JQL"
    QString openLabel;    // link to the ticket in the system's web UI: "In Jira öffnen"
    QList<Preset> presets;  // templates for "Lokal speichern", the first one is the default

    static const BackendType &jira();
    static const BackendType &byId(const QString &id);  // unknown id -> jira
};

// One property in the detail view.
struct DetailField {
    enum Kind { Text, Time, Tags };  // Time: UTC ISO, formatted by the UI; Tags: space separated
    QString label, value;
    Kind kind = Text;
};

struct Transition {
    QString id;    // backend specific, passed back to Backend::transition
    QString name;  // shown to the user: target status or the workflow step's name
};

struct IssuePage {
    QList<IssueRecord> issues;
    QString next;  // cursor of the next page, empty = last page
};

class Backend : public QObject {
    Q_OBJECT
public:
    using Done = std::function<void(QString err)>;  // empty err = ok

    using QObject::QObject;
    virtual const BackendType &type() const = 0;

    // Scope ---------------------------------------------------------------
    // Query describing everything that is kept locally, derived from the account settings.
    virtual QString scopeQuery(const Account &account) const = 0;

    // Checks the credentials; returns the display name of the logged in user.
    virtual void checkLogin(std::function<void(QString displayName, QString err)> cb) = 0;

    // Sync ----------------------------------------------------------------
    // Once per session before syncing (discover fields, own user id, …). Returns the own user id.
    virtual void prepare(std::function<void(QString myId, QString err)> cb) = 0;
    // Issues of the scope, oldest change first. updatedWithinMinutes < 0: all of them.
    virtual void fetchIssues(const QString &scope, int updatedWithinMinutes, const QString &cursor,
                             std::function<void(IssuePage page, QString err)> cb) = 0;
    // Ids of all issues currently in the scope (finds deleted / moved issues).
    virtual void fetchIds(const QString &scope, const QString &cursor,
                          std::function<void(QList<qint64> ids, QString next, QString err)> cb) = 0;
    // Sprints: boards with iterations. Backends without them return false and never call back.
    virtual bool hasSprints() const { return false; }
    virtual void fetchSprint(qint64 id, std::function<void(std::optional<SprintInfo>)> cb) { Q_UNUSED(id) Q_UNUSED(cb) }

    // Single issues and the cloud fallback --------------------------------
    virtual void fetchIssue(const QString &key, std::function<void(std::optional<IssueRecord>, QString err)> cb) = 0;
    // Full text search OUTSIDE the scope (a key is looked up directly).
    virtual void searchText(const QString &text, const QString &scope,
                            std::function<void(QList<IssueRow>, QString err)> cb) = 0;
    virtual void fetchComments(const QString &key, std::function<void(QList<CommentRow>, QString err)> cb) = 0;

    // Detail view -----------------------------------------------------------
    // Properties in display order (status is shown separately by the UI).
    virtual QList<DetailField> detailFields(const IssueDetail &issue) const = 0;
    virtual QString browseUrl(const QString &key) const = 0;

    // Actions ---------------------------------------------------------------
    virtual void transitions(const QString &key, std::function<void(QList<Transition>, QString err)> cb) = 0;
    virtual void transition(const QString &key, const QString &transitionId, Done cb) = 0;
    virtual void addComment(const QString &key, const QString &text, Done cb) = 0;
    virtual QString assignToMeLabel() const { return "Mir zuweisen"; }
    virtual void assignToMe(const QString &key, Done cb) = 0;

    // Creates the backend of an account. auth: credentials of the account (owned by the caller).
    // db: the account's local db for backend state; nullptr for a login check before the account exists.
    static Backend *create(const Account &account, class AuthProvider *auth, Database *db, QObject *parent);
};
