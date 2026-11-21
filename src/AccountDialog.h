#pragma once
#include "Account.h"

#include <QDialog>

class QLineEdit;

class AccountDialog : public QDialog {
    Q_OBJECT
public:
    // API-token mode (default) or OAuth mode: site already known, only name + sync scope are asked.
    explicit AccountDialog(QWidget *parent = nullptr);
    AccountDialog(const QString &siteName, const QString &siteUrl, const QString &identity, QWidget *parent = nullptr);
    Account account() const;
    QString token() const;

private:
    void build(bool oauth);

    bool m_oauth = false;
    QLineEdit *m_name, *m_url, *m_email, *m_token, *m_projects, *m_extra;
};
