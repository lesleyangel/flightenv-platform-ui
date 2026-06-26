#include "TrajectoryMonitorWidget.h"

#include <QApplication>
#include <QLocale>

#include <rclcpp/rclcpp.hpp>

#ifdef _WIN32
#include <windows.h>
#endif

int main(int argc, char* argv[])
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("FlightEnv 弹道模块示意 UI"));
    QApplication::setOrganizationName(QStringLiteral("FlightEnv"));
    QLocale::setDefault(QLocale(QLocale::Chinese, QLocale::China));
    rclcpp::init(argc, argv);

    TrajectoryMonitorWidget widget;
    widget.resize(1180, 760);
    widget.show();

    const int ret = app.exec();
    if (rclcpp::ok()) {
        rclcpp::shutdown();
    }
    return ret;
}
