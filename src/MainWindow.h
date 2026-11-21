#pragma once
#include "Database.h"

#include <QHash>
#include <QMainWindow>
#include <QTimer>

class AccountContext;
class AuthProvider;
class OAuthSession;
class IssueListModel;
class QComboBox;
class QLabel;
class QLineEdit;
class QTabWidget;
class QTableView;
class QTextBrowser;
struct Account;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

private:
    QTableView *makeTable(IssueListModel *model);
    void loadAccounts();
    void addContext(const Account &account, AuthProvider *auth);
    void switchAccount(int index);
    void addAccountDialog();
    void oauthLogin();
    void firstRun();

    void runLocalSearch();
    void runCloudSearch();
    void applyResults();
    void updateSearchInfo();

    void reloadSprints();
    void showSprint();

    void openIssue(const QString &key);
    void backgroundRefresh();
    void renderDetail();
    void reselect(QTableView *view, IssueListModel *model);

    // One session per Atlassian identity (e.g. own address + customer addresses),
    // shared by all sites of that identity. Key: Atlassian account id.
    OAuthSession *oauthSession(const QString &atlassianAccountId);
    QHash<QString, OAuthSession *> m_sessions;
    QList<AccountContext *> m_contexts;
    AccountContext *m_ctx = nullptr;

    QComboBox *m_accountBox = nullptr;
    QLineEdit *m_search = nullptr;
    QTabWidget *m_tabs = nullptr;
    QTableView *m_resultView = nullptr;
    IssueListModel *m_resultModel = nullptr;
    QComboBox *m_sprintBox = nullptr;
    QTableView *m_sprintView = nullptr;
    IssueListModel *m_sprintModel = nullptr;
    QTextBrowser *m_detail = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_searchInfo = nullptr;

    QTimer m_cloudTimer;    // debounce for the cloud fallback
    QTimer m_refreshTimer;  // debounce for background refresh of the opened issue

    QList<IssueRow> m_local, m_cloud;
    double m_localMs = 0;
    QString m_cloudInfo;
    quint64 m_searchGen = 0;
    QString m_currentKey;
    QString m_renderedKey;
};
