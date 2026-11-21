#include "JiraClient.h"
#include "Auth.h"

#include <QJsonArray>
#include <QNetworkReply>
#include <QTimer>

JiraClient::JiraClient(const QString &apiBase, AuthProvider *auth, QObject *parent)
    : QObject(parent), m_auth(auth)
{
    QString b = apiBase.trimmed();
    while (b.endsWith('/'))
        b.chop(1);
    m_base = QUrl(b);
    m_nam.setTransferTimeout(30000);
}

void JiraClient::get(const QString &path, const QUrlQuery &query, JsonCb cb)
{
    QUrl url(m_base.toString() + path);
    url.setQuery(query);
    send("GET", url, {}, std::move(cb), 0, false);
}

void JiraClient::post(const QString &path, const QJsonObject &body, JsonCb cb)
{
    send("POST", QUrl(m_base.toString() + path), QJsonDocument(body).toJson(QJsonDocument::Compact), std::move(cb), 0, false);
}

void JiraClient::searchJql(const QString &jql, const QStringList &fields, int maxResults,
                           const QString &nextPageToken, JsonCb cb)
{
    QJsonObject body{
        {"jql", jql},
        {"fields", QJsonArray::fromStringList(fields)},
        {"maxResults", maxResults},
    };
    if (!nextPageToken.isEmpty())
        body["nextPageToken"] = nextPageToken;
    post("/rest/api/3/search/jql", body, std::move(cb));
}

void JiraClient::send(const QByteArray &verb, const QUrl &url, const QByteArray &body, JsonCb cb, int attempt,
                      bool authRetried)
{
    m_auth->authorize([=, this](const QByteArray &authHeader, const QString &authErr) {
        if (!authErr.isEmpty())
            cb({}, authErr);
        else
            dispatch(verb, url, body, cb, attempt, authRetried, authHeader);
    });
}

void JiraClient::dispatch(const QByteArray &verb, const QUrl &url, const QByteArray &body, JsonCb cb, int attempt,
                          bool authRetried, const QByteArray &authHeader)
{
    QNetworkRequest req(url);
    req.setRawHeader("Authorization", authHeader);
    req.setRawHeader("Accept", "application/json");
    if (!body.isEmpty())
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply *reply = m_nam.sendCustomRequest(req, verb, body);
    connect(reply, &QNetworkReply::finished, this, [=, this] {
        reply->deleteLater();
        const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        if (code == 401 && !authRetried && m_auth->canRefresh()) {  // access token expired early
            m_auth->invalidate();
            send(verb, url, body, cb, attempt, true);
            return;
        }
        if ((code == 429 || code == 503) && attempt < 3) {
            int wait = reply->rawHeader("Retry-After").toInt();
            if (wait <= 0)
                wait = 2 << attempt;
            QTimer::singleShot(wait * 1000, this, [=, this] { send(verb, url, body, cb, attempt + 1, authRetried); });
            return;
        }

        const QByteArray data = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            QString msg = code ? QString("HTTP %1").arg(code) : reply->errorString();
            const auto err = QJsonDocument::fromJson(data);
            const auto messages = err["errorMessages"].toArray();
            if (!messages.isEmpty())
                msg += ": " + messages.first().toString();
            cb({}, msg);
            return;
        }

        QJsonParseError pe;
        const auto doc = QJsonDocument::fromJson(data, &pe);
        if (!data.isEmpty() && pe.error != QJsonParseError::NoError) {
            cb({}, "Ungültiges JSON: " + pe.errorString());
            return;
        }
        cb(doc, {});
    });
}
