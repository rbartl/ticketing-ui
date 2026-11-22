#pragma once
#include "Account.h"
#include "Auth.h"

#include <QJsonArray>
#include <QPointer>
#include <QSet>
#include <QWidget>
#include <functional>
#include <memory>

class JiraClient;
class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QStackedWidget;

// "Add / edit account" inside the main window – no dialogs, extra windows confuse the macOS app switcher.
//   choice -> API-token form
//   choice -> browser login (stays until it succeeds, fails or is cancelled) -> one form per new site
//   edit   -> same form for an existing account
class AccountPanel : public QWidget {
    Q_OBJECT
public:
    explicit AccountPanel(QWidget *parent = nullptr);
    // OAuth: the session of an Atlassian identity (owned by MainWindow), used to list projects.
    void setSessionProvider(std::function<AuthProvider *(const QString &oauthUser)> f) { m_sessionFor = std::move(f); }
    void setCancelable(bool on);  // false while there is no account yet
    void showChoice();
    void showTokenForm();
    void startOAuth();
    void showEditForm(const Account &account);

signals:
    void oauthLoggedIn(const QJsonObject &tokens, const OAuthIdentity &who);
    void accountReady(const Account &account, const QString &token);    // token: API-token accounts only
    void accountUpdated(const Account &account, const QString &token);  // token empty = unchanged
    void closed();

private:
    enum class Mode { NewToken, NewOAuth, Edit };

    QWidget *buildChoice();
    QWidget *buildLogin();
    QWidget *buildForm();
    void setLoginText(const QString &text, bool error);
    void stopLogin();
    void nextSite();
    void showForm(Mode mode, const Account &account);
    void loadProjects();
    void fetchProjectPage(int startAt, std::shared_ptr<QList<QPair<QString, QString>>> acc);
    void fillProjects(const QList<QPair<QString, QString>> &projects);
    void filterProjects();
    void updateProjectInfo(const QString &text = {});
    void submitForm();
    void cancelCurrent();
    void leave();

    bool m_cancelable = true;
    std::function<AuthProvider *(const QString &)> m_sessionFor;
    QStackedWidget *m_stack = nullptr;
    QWidget *m_choicePage = nullptr, *m_loginPage = nullptr, *m_formPage = nullptr;
    QPushButton *m_choiceCancel = nullptr;

    // Browser login
    QPointer<OAuthLogin> m_login;
    QLabel *m_loginText = nullptr;
    QWidget *m_linkBox = nullptr;
    QLineEdit *m_link = nullptr;
    QPushButton *m_restart = nullptr;

    // Account form
    Mode m_mode = Mode::NewToken;
    Account m_account;  // edited account, or the OAuth site being set up
    QFormLayout *m_form = nullptr;
    QLabel *m_formTitle = nullptr, *m_formError = nullptr, *m_tokenHint = nullptr;
    QLineEdit *m_name = nullptr, *m_url = nullptr, *m_email = nullptr, *m_token = nullptr;
    QComboBox *m_preset = nullptr, *m_age = nullptr;
    QLineEdit *m_jql = nullptr;
    QLineEdit *m_projectFilter = nullptr;
    QListWidget *m_projectList = nullptr;
    QLabel *m_projectInfo = nullptr;
    QPushButton *m_submit = nullptr, *m_formCancel = nullptr;

    // Project list, fetched with the credentials of the form
    QSet<QString> m_selectedProjects;
    QString m_projectsLoadedFor;  // url + credentials the list belongs to
    JiraClient *m_probe = nullptr;
    BasicAuth *m_probeAuth = nullptr;

    // OAuth: identity of the finished login and its sites still to set up
    OAuthIdentity m_who;
    QJsonArray m_pendingSites;
};
