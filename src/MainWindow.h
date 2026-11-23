#pragma once
#include "Database.h"

#include <QHash>
#include <QMainWindow>
#include <QTimer>

class AccountContext;
class AccountPanel;
class AuthProvider;
class IssueListModel;
class IssueProxyModel;
class IssueView;
class OAuthSession;
class QAction;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QStackedWidget;
class QTableView;
struct Account;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    void saveSnapshots(const QString &dir);  // development: the main views as PNG

private:
    void buildMenus();
    QWidget *buildList();
    void setupTable();

    // Accounts
    void loadAccounts();
    void addContext(const Account &account, AuthProvider *auth);
    void addAccount(const Account &account, const QString &token);
    void updateAccount(const Account &account, const QString &token);
    void removeAccount(const QString &accountId);
    void rebuildAccountBox();
    void switchScope(int index);  // 0 = all accounts
    void showAccountPanel();
    QList<AccountContext *> scope() const;  // contexts the list shows
    AccountContext *context(const QString &accountId) const;
    AccountContext *currentContext() const;  // selected account; with "Alle Konten" the one of the open issue

    // List: all local tickets of the selected account(s), or the search results
    void reloadList();
    void runCloudSearch();
    void applyRows();
    void updateListHeader();
    bool interestOnly();  // list limited to "my projects" (Account::interest); shows / hides the checkbox
    void updateStatus();
    void selectionChanged();
    void reselect();

    OAuthSession *oauthSession(const QString &atlassianAccountId);
    QHash<QString, OAuthSession *> m_sessions;
    QList<AccountContext *> m_contexts;
    QString m_scopeId;  // empty = all accounts

    QStackedWidget *m_pages = nullptr;  // main view | account panel
    AccountPanel *m_accountPanel = nullptr;
    QComboBox *m_accountBox = nullptr;
    QLineEdit *m_search = nullptr;
    QAction *m_syncAct = nullptr, *m_hideDoneAct = nullptr;

    QLabel *m_listTitle = nullptr, *m_listSub = nullptr;
    QCheckBox *m_hideDone = nullptr;
    QCheckBox *m_interestOnly = nullptr;  // "Nur meine Projekte" (Account::interest)
    QStackedWidget *m_listStack = nullptr;  // table | empty state
    QTableView *m_table = nullptr;
    IssueListModel *m_model = nullptr;
    IssueProxyModel *m_proxy = nullptr;
    QWidget *m_cloudBar = nullptr;
    QLabel *m_cloudInfoLabel = nullptr;
    IssueView *m_issue = nullptr;

    QLabel *m_connection = nullptr, *m_syncTimes = nullptr, *m_counts = nullptr;

    QTimer m_reloadTimer;  // coalesces sync notifications of several accounts
    QList<IssueRow> m_rows, m_cloud;
    double m_localMs = 0;
    QString m_cloudInfo;
    quint64 m_searchGen = 0;
    QString m_selAccount, m_selKey;  // selected row, kept across reloads
};
