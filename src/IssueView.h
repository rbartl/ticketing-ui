#pragma once
#include <QPointer>
#include <QTimer>
#include <QWidget>

class AccountContext;
class QComboBox;
class QFormLayout;
class QGridLayout;
class QLabel;
class QVBoxLayout;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QTabWidget;

// Detail of one issue: header, actions (status, assign, comment) and tabs Details / Kommentare / Verlauf.
// Shows the local copy at once and refreshes it from the backend in the background.
class IssueView : public QWidget {
    Q_OBJECT
public:
    explicit IssueView(QWidget *parent = nullptr);

    void showIssue(AccountContext *ctx, const QString &key);  // nullptr / empty key: nothing selected
    void refresh();  // local data changed (sync)
    AccountContext *context() const { return m_ctx; }
    QString key() const { return m_key; }

    void openInBrowser();
    void assignToMe();
    void startComment();  // tab Kommentare, focus on the input

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

signals:
    void read(AccountContext *ctx, const QString &key);     // opened -> no longer unread
    void changed(AccountContext *ctx, const QString &key);  // changed by an action here
    void message(const QString &text);                      // for the status bar

private:
    void render();
    void renderComments();
    void setHistoryMessage(const QString &text);
    void backgroundRefresh();
    void loadTransitions();
    void loadHistory();
    void applyTransition(int index);
    void sendComment();
    void reloadAfterAction(const QString &done);
    void updateActions();

    QPointer<AccountContext> m_ctx;
    QString m_key;
    QTimer m_refreshTimer;  // debounce while scrolling through the list

    QStackedWidget *m_stack = nullptr;
    QLabel *m_empty = nullptr;
    QLabel *m_typeIcon = nullptr, *m_keyLabel = nullptr, *m_account = nullptr, *m_link = nullptr, *m_title = nullptr;
    QComboBox *m_status = nullptr;
    QPushButton *m_assign = nullptr, *m_comment = nullptr, *m_send = nullptr;
    QTabWidget *m_tabs = nullptr;
    QFormLayout *m_fields = nullptr;      // tab Details: property card
    QLabel *m_description = nullptr;
    QVBoxLayout *m_commentList = nullptr;  // tab Kommentare
    QGridLayout *m_historyGrid = nullptr;  // tab Verlauf
    QWidget *m_detailsPage = nullptr, *m_commentsScroll = nullptr, *m_historyPage = nullptr;
    QPlainTextEdit *m_draft = nullptr;

    QString m_historyKey;  // history currently shown / loading for
    bool m_busy = false;   // an action is running
};
