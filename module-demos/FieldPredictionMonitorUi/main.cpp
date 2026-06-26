#include "FieldPredictionMonitorWidget.h"

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
    QApplication::setApplicationName(QStringLiteral("FlightEnv 场预测模块示意 UI"));
    QApplication::setOrganizationName(QStringLiteral("FlightEnv"));
    QLocale::setDefault(QLocale(QLocale::Chinese, QLocale::China));
    rclcpp::init(argc, argv);

    FieldPredictionMonitorWidget widget;
    widget.resize(1160, 740);
    widget.show();

    const int ret = app.exec();
    if (rclcpp::ok()) {
        rclcpp::shutdown();
    }
    return ret;
}
