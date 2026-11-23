#include "MainWindow.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

// The app used to be called "jira-client": take over its data directory and settings once.
static void migrateFromJiraClient()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir parent = QFileInfo(dir).dir();
    const QString old = parent.filePath("jira-client");
    if (!QFileInfo::exists(dir) && QFileInfo::exists(old)) {
        parent.mkpath(".");
        QDir().rename(old, dir);
    }

    QSettings now;
    if (now.allKeys().isEmpty()) {
        QSettings before(QApplication::organizationName(), "jira-client");
        for (const QString &key : before.allKeys())
            now.setValue(key, before.value(key));
    }
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName("edvbartl");
    QApplication::setApplicationName("ticketing-ui");
    migrateFromJiraClient();

    MainWindow w;
    w.show();
    return app.exec();
}
