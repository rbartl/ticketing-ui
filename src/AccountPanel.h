#pragma once
#include "Account.h"
#include "Auth.h"

#include <QJsonArray>
#include <QPointer>
#include <QWidget>
#include <functional>

class Backend;
struct ProjectInfo;
class ProjectChecklist;
class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QTabWidget;

// "Konten verwalten" inside the main window – no dialogs, extra windows confuse the macOS app switcher.
//   manage page: account list (add / remove) + editor with tabs Zugang / Lokal speichern
//   add -> choice -> API-token: new entry in the editor
//   add -> choice -> browser login (stays until it succeeds, fails or is cancelled) -> one new entry per new site
class AccountPanel : public QWidget {
    Q_OBJECT
public:
    explicit AccountPanel(QWidget *parent = nullptr);
    void setCancelable(bool on);  // false while there is no account yet
    void showChoice();
    void showTokenForm();
    void startOAuth();
    void showManage(const QString &accountId = {});  // selects that account (default: the first one)
    // Backend of an existing account (loads the project list); nullptr if there is none.
    void setBackendLookup(std::function<Backend *(const QString &accountId)> lookup) { m_backendFor = std::move(lookup); }

signals:
    void oauthLoggedIn(const QJsonObject &tokens, const OAuthIdentity &who);
    void accountReady(const Account &account, const QString &token);    // token: API-token accounts only
    void accountUpdated(const Account &account, const QString &token);  // token empty = unchanged
    void accountRemoved(const QString &accountId);
    void closed();

private:
    enum class Mode { NewToken, NewOAuth, Edit };
    enum Tab { AccessTab, ScopeTab, ViewTab };

    QWidget *buildChoice();
    QWidget *buildLogin();
    QWidget *buildManage();
    QWidget *buildAccessTab();
    QWidget *buildScopeTab();
    QWidget *buildViewTab();
    void setLoginText(const QString &text, bool error);
    void stopLogin();
    void nextSite();
    void fillList(const QString &selectId);
    void edit(Mode mode, const Account &account);
    void loadProjects(Backend *backend);
    void updateScope();  // enabled state, project count, resulting query
    void readScope(Account &a) const;
    void checkLogin();
    void setLoginCheck(const QString &text, bool error = false);
    void removeCurrent();
    void submitForm();
    void cancelCurrent();
    void leave();

    bool m_cancelable = true;
    QStackedWidget *m_stack = nullptr;
    QWidget *m_choicePage = nullptr, *m_loginPage = nullptr, *m_managePage = nullptr;
    QPushButton *m_choiceCancel = nullptr;

    // Browser login
    QPointer<OAuthLogin> m_login;
    QLabel *m_loginText = nullptr;
    QWidget *m_linkBox = nullptr;
    QLineEdit *m_link = nullptr;
    QPushButton *m_restart = nullptr;

    // Account list
    QListWidget *m_list = nullptr;
    QPushButton *m_add = nullptr, *m_remove = nullptr;
    bool m_confirmRemove = false;

    // Editor
    Mode m_mode = Mode::Edit;
    Account m_account;  // edited account, or the new one being set up
    QTabWidget *m_tabs = nullptr;
    QFormLayout *m_form = nullptr;
    QComboBox *m_type = nullptr;
    QLineEdit *m_name = nullptr, *m_url = nullptr, *m_email = nullptr, *m_token = nullptr;
    QLabel *m_tokenHint = nullptr, *m_oauthInfo = nullptr, *m_loginCheck = nullptr;
    QPushButton *m_test = nullptr;
    QWidget *m_simpleScope = nullptr;  // projects + age, disabled while an own query is used
    ProjectChecklist *m_scopeProjects = nullptr;
    QComboBox *m_age = nullptr;  // UserRole = days, 0 = any age
    QCheckBox *m_ownQuery = nullptr;
    QPlainTextEdit *m_query = nullptr;
    QLabel *m_scopePreview = nullptr;
    ProjectChecklist *m_interestProjects = nullptr;  // Account::interest
    int m_projectsRequest = 0;  // drops answers of older requests
    std::function<Backend *(const QString &)> m_backendFor;
    QLabel *m_formError = nullptr;
    QPushButton *m_submit = nullptr, *m_formCancel = nullptr;

    // Login check with the credentials of the form (API-token accounts)
    QString m_checkedFor;  // url + credentials last checked
    Backend *m_probe = nullptr;
    BasicAuth *m_probeAuth = nullptr;

    // OAuth: identity of the finished login and its sites still to set up
    OAuthIdentity m_who;
    QJsonArray m_pendingSites;
};
