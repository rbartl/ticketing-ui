#include "Auth.h"
#include "Account.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>

#ifndef JIRA_OAUTH_BROKER_URL
#define JIRA_OAUTH_BROKER_URL ""
#endif
#ifndef JIRA_OAUTH_CLIENT_ID
#define JIRA_OAUTH_CLIENT_ID ""
#endif
#ifndef JIRA_OAUTH_CLIENT_SECRET
#define JIRA_OAUTH_CLIENT_SECRET ""
#endif

static const char *kTokenUrl = "https://auth.atlassian.com/oauth/token";
static const char *kAuthorizeUrl = "https://auth.atlassian.com/authorize";
static const char *kResourcesUrl = "https://api.atlassian.com/oauth/token/accessible-resources";
static const char *kMeUrl = "https://api.atlassian.com/me";

namespace {

using JsonResult = std::function<void(const QJsonDocument &doc, const QString &error, int httpCode)>;

void request(QNetworkAccessManager &nam, const QByteArray &verb, const QUrl &url, const QByteArray &auth,
             const QJsonObject &body, JsonResult cb)
{
    QNetworkRequest req(url);
    req.setRawHeader("Accept", "application/json");
    req.setTransferTimeout(30000);
    if (!auth.isEmpty())
        req.setRawHeader("Authorization", auth);
    QByteArray payload;
    if (!body.isEmpty()) {
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    }
    QNetworkReply *reply = nam.sendCustomRequest(req, verb, payload);
    QObject::connect(reply, &QNetworkReply::finished, reply, [reply, cb] {
        reply->deleteLater();
        const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto doc = QJsonDocument::fromJson(reply->readAll());
        if (reply->error() != QNetworkReply::NoError) {
            QString msg = code ? QString("HTTP %1").arg(code) : reply->errorString();
            const QString detail = doc["error_description"].toString(doc["error"].toString());
            if (!detail.isEmpty())
                msg += ": " + detail;
            cb(doc, msg, code);
            return;
        }
        cb(doc, {}, code);
    });
}

// Token endpoint, either via the broker (secret stays there) or directly.
//   kind "code":    params {code, state, verifier}     (state = broker's signed state)
//   kind "refresh": params {refresh_token}
void tokenRequest(QNetworkAccessManager &nam, const OAuthConfig &cfg, const QString &kind,
                  const QJsonObject &params, JsonResult cb)
{
    if (cfg.useBroker()) {
        QString base = cfg.brokerUrl;
        while (base.endsWith('/'))
            base.chop(1);
        request(nam, "POST", QUrl(base + (kind == "code" ? "/token" : "/refresh")), {}, params, cb);
        return;
    }
    QJsonObject body{{"client_id", cfg.clientId}, {"client_secret", cfg.clientSecret}};
    if (kind == "code") {
        body["grant_type"] = "authorization_code";
        body["code"] = params["code"];
        body["redirect_uri"] = cfg.localCallback();
    } else {
        body["grant_type"] = "refresh_token";
        body["refresh_token"] = params["refresh_token"];
    }
    request(nam, "POST", QUrl(kTokenUrl), {}, body, cb);
}

QString randomUrlSafe(int bytes)
{
    QByteArray b(bytes, Qt::Uninitialized);
    for (char &c : b)
        c = char(QRandomGenerator::system()->bounded(256));
    return QString::fromLatin1(b.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QString pkceChallenge(const QString &verifier)
{
    return QString::fromLatin1(QCryptographicHash::hash(verifier.toLatin1(), QCryptographicHash::Sha256)
                                   .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

} // namespace

// BasicAuth ---------------------------------------------------------------------

BasicAuth::BasicAuth(const QString &email, const QString &token, QObject *parent)
    : AuthProvider(parent), m_header("Basic " + (email + ':' + token).toUtf8().toBase64())
{
}

void BasicAuth::authorize(HeaderCb cb) { cb(m_header, {}); }

void BasicAuth::setCredentials(const QString &email, const QString &token)
{
    m_header = "Basic " + (email + ':' + token).toUtf8().toBase64();
}

// OAuthConfig -------------------------------------------------------------------

OAuthConfig OAuthConfig::load()
{
    OAuthConfig c;
    c.brokerUrl = QStringLiteral(JIRA_OAUTH_BROKER_URL);
    c.clientId = QStringLiteral(JIRA_OAUTH_CLIENT_ID);
    c.clientSecret = QStringLiteral(JIRA_OAUTH_CLIENT_SECRET);
    QFile f(AccountStore::dataDir() + "/oauth.json");
    if (f.open(QIODevice::ReadOnly)) {
        const auto o = QJsonDocument::fromJson(f.readAll()).object();
        c.brokerUrl = o["broker_url"].toString(c.brokerUrl);
        c.clientId = o["client_id"].toString(c.clientId);
        c.clientSecret = o["client_secret"].toString(c.clientSecret);
        c.port = o["port"].toInt(c.port);
    }
    if (!c.useBroker() && c.port == 0)
        c.port = kDirectPort;
    return c;
}

QString OAuthConfig::scopes()
{
    // Must match the scopes enabled for the app in the developer console (and broker/worker.js).
    return "read:jira-work write:jira-work read:jira-user read:sprint:jira-software read:me offline_access";
}

// OAuthSession ------------------------------------------------------------------

OAuthSession::OAuthSession(const QString &atlassianAccountId, QObject *parent)
    : AuthProvider(parent), m_accountId(atlassianAccountId)
{
}

void OAuthSession::load()
{
    const auto o = QJsonDocument::fromJson(AccountStore::readToken(secretKey()).toUtf8()).object();
    m_refresh = o["refresh_token"].toString();
    m_access = o["access_token"].toString();
    m_expiresAt = o["expires_at"].toString().toLongLong();
}

void OAuthSession::persist()
{
    const QJsonObject o{
        {"refresh_token", m_refresh},
        {"access_token", m_access},
        {"expires_at", QString::number(m_expiresAt)},
    };
    AccountStore::writeToken(secretKey(), QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)));
}

void OAuthSession::setTokens(const QJsonObject &t)
{
    m_access = t["access_token"].toString();
    if (t.contains("refresh_token"))
        m_refresh = t["refresh_token"].toString();  // rotating: always keep the newest
    m_expiresAt = QDateTime::currentMSecsSinceEpoch() + (t["expires_in"].toInt(3600) - 60) * 1000LL;
    persist();
}

void OAuthSession::authorize(HeaderCb cb)
{
    if (!m_access.isEmpty() && QDateTime::currentMSecsSinceEpoch() < m_expiresAt) {
        cb("Bearer " + m_access.toUtf8(), {});
        return;
    }
    if (m_refresh.isEmpty()) {
        cb({}, QString::fromUtf8("Nicht angemeldet – bitte „Mit Atlassian anmelden“"));
        return;
    }
    m_waiting << cb;
    refresh();
}

void OAuthSession::refresh()
{
    if (m_refreshing)
        return;
    m_refreshing = true;
    tokenRequest(m_nam, OAuthConfig::load(), "refresh", {{"refresh_token", m_refresh}},
                 [this](const QJsonDocument &doc, const QString &err, int code) {
        m_refreshing = false;
        const auto waiting = std::exchange(m_waiting, {});
        if (err.isEmpty()) {
            setTokens(doc.object());
            for (const auto &cb : waiting)
                cb("Bearer " + m_access.toUtf8(), {});
            return;
        }
        QString msg = "Token-Erneuerung fehlgeschlagen: " + err;
        if (code == 400 || code == 401 || code == 403) {  // refresh token expired / revoked
            m_refresh.clear();
            m_access.clear();
            persist();
            msg = QString::fromUtf8("Anmeldung abgelaufen – bitte neu „Mit Atlassian anmelden“");
            emit loginRequired();
        }
        for (const auto &cb : waiting)
            cb({}, msg);
    });
}

// OAuthLogin --------------------------------------------------------------------

OAuthLogin::OAuthLogin(QObject *parent) : QObject(parent)
{
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(10 * 60 * 1000);  // = STATE_TTL_SECONDS of the broker
    connect(&m_timeout, &QTimer::timeout, this, [this] { finish("Zeitüberschreitung bei der Anmeldung"); });
}

bool OAuthLogin::start(QString *err)
{
    m_cfg = OAuthConfig::load();
    if (!m_cfg.isValid()) {
        *err = "OAuth ist nicht konfiguriert.\n\nIn " + AccountStore::dataDir() +
               "/oauth.json entweder\n  \"broker_url\" (Cloudflare Worker, empfohlen) oder\n"
               "  \"client_id\" + \"client_secret\" (eigene App)\neintragen.";
        return false;
    }
    // "localhost" may resolve to ::1 or 127.0.0.1 -> listen on both (same port), loopback only.
    quint16 port = quint16(m_cfg.port);  // 0 = any free port
    for (const auto &addr : {QHostAddress(QHostAddress::LocalHost), QHostAddress(QHostAddress::LocalHostIPv6)}) {
        auto *server = new QTcpServer(this);
        if (!server->listen(addr, port)) {
            delete server;
            continue;
        }
        port = server->serverPort();
        connect(server, &QTcpServer::newConnection, this, [this, server] {
            while (QTcpSocket *s = server->nextPendingConnection()) {
                connect(s, &QTcpSocket::readyRead, this, [this, s] { onReadyRead(s); });
                connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            }
        });
        m_servers << server;
    }
    m_cfg.port = port;
    if (m_servers.isEmpty()) {
        *err = QString("Port %1 ist belegt – in oauth.json einen anderen \"port\" setzen.").arg(m_cfg.port);
        return false;
    }

    m_state = randomUrlSafe(24);
    QUrl &url = m_url;
    if (m_cfg.useBroker()) {
        m_verifier = randomUrlSafe(32);
        QString base = m_cfg.brokerUrl;
        while (base.endsWith('/'))
            base.chop(1);
        url = QUrl(base + "/login");
        url.setQuery(QUrlQuery{
            {"port", QString::number(m_cfg.port)},
            {"state", m_state},
            {"challenge", pkceChallenge(m_verifier)},
        });
    } else {
        url = QUrl(kAuthorizeUrl);
        url.setQuery(QUrlQuery{
            {"audience", "api.atlassian.com"},
            {"client_id", m_cfg.clientId},
            {"scope", OAuthConfig::scopes()},
            {"redirect_uri", m_cfg.localCallback()},
            {"state", m_state},
            {"response_type", "code"},
            {"prompt", "consent"},
        });
    }
    m_timeout.start();
    return true;
}

void OAuthLogin::cancel()
{
    m_finished = m_done = true;
    m_timeout.stop();
    for (auto *s : m_servers)
        s->close();
    deleteLater();
}

void OAuthLogin::onReadyRead(QTcpSocket *socket)
{
    QByteArray buf = socket->property("buf").toByteArray() + socket->readAll();
    socket->setProperty("buf", buf);
    if (!buf.contains("\r\n\r\n")) {
        if (buf.size() > 16 * 1024)
            socket->abort();
        return;
    }
    // "GET /callback?code=...&state=... HTTP/1.1"
    const QList<QByteArray> parts = buf.left(buf.indexOf("\r\n")).split(' ');
    const QUrl target(QString::fromLatin1(parts.value(1)));
    if (parts.value(0) != "GET" || target.path() != "/callback") {
        respond(socket, 404, "Nicht gefunden");
        return;
    }
    const QUrlQuery q(target);
    if (m_done) {
        respond(socket, 200, "<p>Anmeldung bereits abgeschlossen. Dieses Fenster kann geschlossen werden.</p>");
        return;
    }
    // Broker passes our state back as app_state (its own signed state is in "state").
    const QString ourState = q.queryItemValue(m_cfg.useBroker() ? "app_state" : "state");
    if (ourState != m_state) {
        respond(socket, 400, "<p>Ungültige Anfrage (state).</p>");
        return;
    }
    if (q.hasQueryItem("error")) {
        const QString e = q.queryItemValue("error_description", QUrl::FullyDecoded);
        respond(socket, 200, "<p>Anmeldung abgebrochen.</p>");
        finish("Anmeldung abgebrochen: " + (e.isEmpty() ? q.queryItemValue("error") : e));
        return;
    }
    respond(socket, 200, "<h2>Angemeldet ✔</h2><p>Du kannst dieses Fenster schließen und zum Jira Client zurückkehren.</p>");
    emit callbackReceived();
    exchange(q);
}

void OAuthLogin::respond(QTcpSocket *socket, int code, const QString &html)
{
    const QByteArray body = ("<!doctype html><meta charset=utf-8><title>Jira Client</title>"
                             "<body style='font-family:sans-serif;margin:3em'>" + html + "</body>").toUtf8();
    socket->write("HTTP/1.1 " + QByteArray::number(code) + (code == 200 ? " OK" : " Error") +
                  "\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\nContent-Length: " +
                  QByteArray::number(body.size()) + "\r\n\r\n" + body);
    socket->disconnectFromHost();
}

void OAuthLogin::exchange(const QUrlQuery &q)
{
    m_done = true;
    for (auto *s : m_servers)
        s->close();
    const QJsonObject params{
        {"code", q.queryItemValue("code", QUrl::FullyDecoded)},
        {"state", q.queryItemValue("state", QUrl::FullyDecoded)},
        {"verifier", m_verifier},
    };
    tokenRequest(m_nam, m_cfg, "code", params, [this](const QJsonDocument &doc, const QString &err, int) {
        if (!err.isEmpty())
            finish("Token-Austausch fehlgeschlagen: " + err);
        else
            loadIdentity(doc.object());
    });
}

void OAuthLogin::loadIdentity(const QJsonObject &tokens)
{
    const QByteArray bearer = "Bearer " + tokens["access_token"].toString().toUtf8();
    request(m_nam, "GET", QUrl(kMeUrl), bearer, {}, [this, tokens, bearer](const QJsonDocument &me, const QString &err, int) {
        if (!err.isEmpty()) {
            finish("Benutzer konnte nicht ermittelt werden (" + err +
                   ").\nIst der Scope read:me (User identity API) in der Developer Console aktiviert?");
            return;
        }
        const OAuthIdentity who{me["account_id"].toString(), me["email"].toString(), me["name"].toString()};
        request(m_nam, "GET", QUrl(kResourcesUrl), bearer, {},
                [this, tokens, who](const QJsonDocument &res, const QString &err2, int) {
            if (!err2.isEmpty()) {
                finish("Sites konnten nicht gelesen werden: " + err2);
                return;
            }
            QJsonArray jira;  // accessible-resources also lists Confluence etc.
            for (const auto &v : res.array()) {
                for (const auto &s : v.toObject()["scopes"].toArray()) {
                    if (s.toString().contains("jira")) {
                        jira.append(v);
                        break;
                    }
                }
            }
            finish({}, tokens, who, jira);
        });
    });
}

void OAuthLogin::finish(const QString &error, const QJsonObject &tokens, const OAuthIdentity &who,
                        const QJsonArray &sites)
{
    if (m_finished)
        return;
    m_finished = true;
    m_done = true;
    m_timeout.stop();
    for (auto *s : m_servers)
        s->close();
    if (!error.isEmpty())
        emit failed(error);
    else if (who.accountId.isEmpty())
        emit failed("Atlassian hat keine Benutzer-ID geliefert.");
    else
        emit succeeded(tokens, who, sites);
    deleteLater();
}
