#include "ProjectChecklist.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QSignalBlocker>
#include <QVBoxLayout>

ProjectChecklist::ProjectChecklist(const QString &title, const QString &noneText, QWidget *parent)
    : QWidget(parent), m_noneText(noneText)
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(6);
    auto *head = new QHBoxLayout;
    head->addWidget(new QLabel("<b>" + title.toHtmlEscaped() + "</b>"));
    head->addStretch();
    m_filter = new QLineEdit;
    m_filter->setPlaceholderText("Filtern …");
    m_filter->setClearButtonEnabled(true);
    m_filter->setFixedWidth(200);
    head->addWidget(m_filter);
    v->addLayout(head);
    m_list = new QListWidget;
    m_list->setMinimumHeight(160);
    v->addWidget(m_list, 1);
    m_info = new QLabel;
    m_info->setForegroundRole(QPalette::PlaceholderText);
    v->addWidget(m_info);

    connect(m_filter, &QLineEdit::textChanged, this, [this] { filter(); });
    connect(m_list, &QListWidget::itemChanged, this, [this] {
        updateInfo();
        if (changed)
            changed();
    });
    updateInfo();
}

void ProjectChecklist::setProjects(const QList<ProjectInfo> &all)
{
    m_all = all;
    fill(checked());
}

void ProjectChecklist::setChecked(const QStringList &keys)
{
    m_filter->clear();
    fill(keys);
}

void ProjectChecklist::fill(const QStringList &checked)
{
    {
        QSignalBlocker b(m_list);
        m_list->clear();
        auto add = [this, &checked](const QString &key, const QString &name) {
            auto *item = new QListWidgetItem(name.isEmpty() ? key : name + "  ·  " + key, m_list);
            item->setData(Qt::UserRole, key);
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
            item->setCheckState(checked.contains(key) ? Qt::Checked : Qt::Unchecked);
        };
        for (const QString &key : checked) {  // ticked ones first
            QString name;
            for (const auto &p : std::as_const(m_all))
                if (p.key == key)
                    name = p.name;
            add(key, name);
        }
        for (const auto &p : std::as_const(m_all))
            if (!checked.contains(p.key))
                add(p.key, p.name);
    }
    filter();
    updateInfo();
    if (changed)
        changed();
}

QStringList ProjectChecklist::checked() const
{
    QStringList keys;
    for (int i = 0; i < m_list->count(); ++i)
        if (m_list->item(i)->checkState() == Qt::Checked)
            keys << m_list->item(i)->data(Qt::UserRole).toString();
    return keys;
}

void ProjectChecklist::setStatus(const QString &status)
{
    m_status = status;
    updateInfo();
}

void ProjectChecklist::filter()
{
    const QString f = m_filter->text().trimmed();
    for (int i = 0; i < m_list->count(); ++i)
        m_list->item(i)->setHidden(!f.isEmpty() && !m_list->item(i)->text().contains(f, Qt::CaseInsensitive));
}

void ProjectChecklist::updateInfo()
{
    const int n = int(checked().size());
    const QString info = !m_status.isEmpty() ? m_status
                         : n == 0            ? m_noneText
                                             : QString("%1 von %2 gewählt").arg(n).arg(m_list->count());
    m_info->setText("<small>" + info.toHtmlEscaped() + "</small>");
}
