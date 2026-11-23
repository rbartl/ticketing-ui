#include "IssueListModel.h"
#include "MainWindow.h"
#include "Theme.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QPalette>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

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
    QApplication::setApplicationVersion(APP_VERSION);

    // Development: render the main views to PNG files with test data, without touching the real data or settings.
    // The test accounts live in QStandardPaths' test location (~/.qttest/...).
    const QString snapshotDir = qEnvironmentVariable("TICKETING_UI_SNAPSHOT");
    if (!snapshotDir.isEmpty()) {
        QStandardPaths::setTestModeEnabled(true);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, snapshotDir);
        qInfo("Snapshot: data in %s", qPrintable(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)));
    } else {
        migrateFromJiraClient();
    }

    // Fusion everywhere, system colors; selection in the accent color.
    QApplication::setStyle("Fusion");
    QPalette pal = QApplication::palette();
    pal.setColor(QPalette::Highlight, IssueStyle::accent());
    pal.setColor(QPalette::HighlightedText, Qt::white);
    QApplication::setPalette(pal);
    Theme::apply(app);

    MainWindow w;
    w.show();
    if (!snapshotDir.isEmpty())
        QTimer::singleShot(1500, &w, [&w, snapshotDir] {
            w.saveSnapshots(snapshotDir);
            QApplication::quit();
        });
    return app.exec();
}
