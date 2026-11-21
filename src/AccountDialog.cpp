#include "AccountDialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QRegularExpression>
#include <QUrl>

AccountDialog::AccountDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle("Jira-Konto mit API-Token hinzufügen");
    build(false);
}

AccountDialog::AccountDialog(const QString &siteName, const QString &siteUrl, const QString &identity,
                             QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("Jira-Site einrichten: " + siteName);
    build(true);
    m_name->setText(siteName);
    m_url->setText(siteUrl);
    m_email->setText(identity);
    m_email->setReadOnly(true);
}

void AccountDialog::build(bool oauth)
{
    m_oauth = oauth;
    auto *form = new QFormLayout(this);
    m_name = new QLineEdit;
    m_url = new QLineEdit;
    m_url->setPlaceholderText("https://firma.atlassian.net");
    m_email = new QLineEdit;
    m_token = new QLineEdit;
    m_token->setEchoMode(QLineEdit::Password);
    m_projects = new QLineEdit;
    m_projects->setPlaceholderText("ABC, XYZ");
    m_extra = new QLineEdit("assignee = currentUser() OR watcher = currentUser()");

    form->addRow("Name", m_name);
    form->addRow("URL", m_url);
    form->addRow("E-Mail", m_email);
    form->addRow("API-Token", m_token);
    auto *hint = new QLabel("<small>Token erstellen: id.atlassian.com → Sicherheit → API-Token. "
                            "Wird im Schlüsselbund des Systems gespeichert.</small>");
    hint->setWordWrap(true);
    form->addRow(QString(), hint);
    if (oauth) {
        for (QWidget *w : {static_cast<QWidget *>(m_token), static_cast<QWidget *>(hint)})
            form->setRowVisible(w, false);
        m_url->setReadOnly(true);
    }
    form->addRow("Projekte (lokal)", m_projects);
    form->addRow("Zusätzlich (JQL)", m_extra);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (!m_oauth && (m_url->text().trimmed().isEmpty() || m_email->text().trimmed().isEmpty() ||
                         m_token->text().isEmpty())) {
            QMessageBox::warning(this, windowTitle(), "URL, E-Mail und API-Token sind Pflicht.");
            return;
        }
        accept();
    });
    resize(520, sizeHint().height());
}

Account AccountDialog::account() const
{
    Account a;
    a.baseUrl = m_url->text().trimmed();
    if (!a.baseUrl.startsWith("http"))
        a.baseUrl.prepend("https://");
    a.name = m_name->text().trimmed();
    if (a.name.isEmpty())
        a.name = QUrl(a.baseUrl).host().section('.', 0, 0);
    a.email = m_email->text().trimmed();
    for (const auto &p : m_projects->text().split(QRegularExpression("[,;\\s]+"), Qt::SkipEmptyParts))
        a.projects << p.toUpper();
    a.extraJql = m_extra->text().trimmed();
    a.authType = m_oauth ? "oauth" : "token";
    return a;
}

QString AccountDialog::token() const { return m_token->text(); }
