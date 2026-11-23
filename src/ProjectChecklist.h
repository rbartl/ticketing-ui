#pragma once
#include "Backend.h"

#include <QWidget>
#include <functional>

class QLabel;
class QLineEdit;
class QListWidget;

// Projects to tick, with a filter field (sites have hundreds of projects). Ticked ones are listed first;
// ticked keys the site does not (or not yet) report stay in the list.
class ProjectChecklist : public QWidget {
public:
    ProjectChecklist(const QString &title, const QString &noneText, QWidget *parent = nullptr);
    void setProjects(const QList<ProjectInfo> &all);  // keeps the ticks
    void setChecked(const QStringList &keys);
    QStringList checked() const;
    void setStatus(const QString &status);  // loading / error; empty = show the count
    std::function<void()> changed;

private:
    void fill(const QStringList &checked);
    void filter();
    void updateInfo();

    QString m_noneText;
    QString m_status;
    QList<ProjectInfo> m_all;
    QLineEdit *m_filter = nullptr;
    QListWidget *m_list = nullptr;  // UserRole = key
    QLabel *m_info = nullptr;
};
