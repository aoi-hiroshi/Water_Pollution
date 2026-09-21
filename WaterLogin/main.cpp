#include "loginwindow.h"
#include "applogger.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFont>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("WaterQuality"));
    QCoreApplication::setApplicationName(QStringLiteral("WaterPollutionClient"));
    a.setFont(QFont("Microsoft YaHei", 10));

    AppLogger::instance().log(
        AppLogType::System, AppLogLevel::Info,
        QStringLiteral("客户端"), QStringLiteral("Qt客户端启动"));

    LoginWindow w;
    w.show();
    const int result = a.exec();
    AppLogger::instance().log(
        AppLogType::System, AppLogLevel::Info,
        QStringLiteral("客户端"), QStringLiteral("Qt客户端退出"));
    return result;
}
