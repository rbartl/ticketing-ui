#include "MainWindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName("edvbartl");
    QApplication::setApplicationName("jira-client");

    MainWindow w;
    w.show();
    return app.exec();
}
