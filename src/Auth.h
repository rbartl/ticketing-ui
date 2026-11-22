#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <functional>

class QTcpServer;
class QTcpSocket;

// Supplies the Authorization header for JiraClient.
class AuthProvider : public QObject {
    Q_OBJECT
public:
    using HeaderCb = std::function<void(const QByteArray &header, const QString &error)>;
    using QObject::QObject;
    virtual void authorize(HeaderCb cb) = 0;
    virtual bool canRefresh() const { return false; }
    virtual void invalidate() {}
};

// E-Mail + API-Token (Basic auth).
class BasicAuth : public AuthProvider {
    Q_OBJECT
public:
    BasicAuth(const QString &email, const QString &token, QObject *parent = nullptr);
    void authorize(HeaderCb cb) override;
    void setCredentials(const QString &email, const QString &token);

private:
    QByteArray m_header;
};

// How the app talks OAuth to Atlassian:
//  * broker mode (recommended): a Cloudflare Worker (see broker/) holds the client secret
//  * direct mode: own OAuth app, client id + secret on this machine
// Defaults come from CMake (-DJIRA_OAUTH_BROKER_URL / _CLIENT_ID / _CLIENT_SECRET),
// <AppData>/oauth.json {"broker_url","client_id","client_secret","port"} overrides them.
struct OAuthConfig {
    QString brokerUrl, clientId, clientSecret;
    // Broker: 0 = any free port, so a login can be restarted while the old one still listens.
    // Direct mode: must match the callback URL registered in the developer console.
    int port = 0;
    static constexpr int kDirectPort = 47823;

    bool useBroker() const { return !brokerUrl.isEmpty(); }
    bool isValid() const { return useBroker() || (!clientId.isEmpty() && !clientSecret.isEmpty()); }
    QString localCallback() const { return QString("http://localhost:%1/callback").arg(port); }
    static OAuthConfig load();
    static QString scopes();  // direct mode; broker mode: SCOPES in broker/worker.js
};

// The Atlassian user behind a login (one person can have several, e.g. per customer).
struct OAuthIdentity {
    QString accountId, email, name;
};

// One Atlassian login (identity). Shared by all sites of that identity: one refresh
// token (rotating), transparent refresh, requests queue while refreshing.
class OAuthSession : public AuthProvider {
    Q_OBJECT
public:
    OAuthSession(const QString &atlassianAccountId, QObject *parent = nullptr);

    QString accountId() const { return m_accountId; }
    void load();  // from secrets.json
    bool isLoggedIn() const { return !m_refresh.isEmpty(); }
    void setTokens(const QJsonObject &tokenResponse);

    void authorize(HeaderCb cb) override;
    bool canRefresh() const override { return !m_refresh.isEmpty(); }
    void invalidate() override { m_expiresAt = 0; }

signals:
    void loginRequired();

private:
    void refresh();
    void persist();
    QString secretKey() const { return "oauth:" + m_accountId; }

    QString m_accountId;
    QNetworkAccessManager m_nam;
    QString m_access, m_refresh;
    qint64 m_expiresAt = 0;  // msecs since epoch
    bool m_refreshing = false;
    QList<HeaderCb> m_waiting;
};

// Browser login: url() is the Atlassian consent page (via broker or directly) for the browser,
// the result arrives on http://localhost:<port>/callback, then tokens, identity and Jira sites are loaded.
class OAuthLogin : public QObject {
    Q_OBJECT
public:
    explicit OAuthLogin(QObject *parent = nullptr);
    bool start(QString *err);  // listens on localhost, does not open the browser
    QUrl url() const { return m_url; }
    void cancel();  // stops listening, no further signals, deletes itself

signals:
    void callbackReceived();  // browser came back, tokens are being fetched
    void succeeded(const QJsonObject &tokens, const OAuthIdentity &identity, const QJsonArray &jiraSites);
    void failed(const QString &error);

private:
    void onReadyRead(QTcpSocket *socket);
    void respond(QTcpSocket *socket, int code, const QString &html);
    void exchange(const QUrlQuery &callbackQuery);
    void loadIdentity(const QJsonObject &tokens);
    void finish(const QString &error, const QJsonObject &tokens = {}, const OAuthIdentity &who = {},
                const QJsonArray &sites = {});

    OAuthConfig m_cfg;
    QUrl m_url;
    QString m_state;     // our CSRF state, comes back on the localhost callback
    QString m_verifier;  // PKCE verifier (broker mode)
    QList<QTcpServer *> m_servers;
    QNetworkAccessManager m_nam;
    QTimer m_timeout;
    bool m_done = false;
    bool m_finished = false;
};
