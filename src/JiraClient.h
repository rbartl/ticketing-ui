#pragma once
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QUrl>
#include <QUrlQuery>
#include <functional>

// Thin async wrapper around the Jira Cloud REST API (HTTP/2, gzip, retry on 429/503,
// transparent OAuth token refresh on 401).
class AuthProvider;

class JiraClient : public QObject {
    Q_OBJECT
public:
    using JsonCb = std::function<void(const QJsonDocument &doc, const QString &error)>;

    JiraClient(const QString &apiBase, AuthProvider *auth, QObject *parent = nullptr);

    void get(const QString &path, const QUrlQuery &query, JsonCb cb);
    void post(const QString &path, const QJsonObject &body, JsonCb cb);
    void put(const QString &path, const QJsonObject &body, JsonCb cb);

    // POST /rest/api/3/search/jql (token based paging)
    void searchJql(const QString &jql, const QStringList &fields, int maxResults,
                   const QString &nextPageToken, JsonCb cb);

private:
    void send(const QByteArray &verb, const QUrl &url, const QByteArray &body, JsonCb cb, int attempt,
              bool authRetried);
    void dispatch(const QByteArray &verb, const QUrl &url, const QByteArray &body, JsonCb cb, int attempt,
                  bool authRetried, const QByteArray &authHeader);

    QNetworkAccessManager m_nam;
    QUrl m_base;
    AuthProvider *m_auth;
};
