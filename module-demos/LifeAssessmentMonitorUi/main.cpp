#include "LifeAssessmentMonitorWidget.h"

#include <QtWidgets/QApplication>

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
    QApplication::setApplicationName(QStringLiteral("LifeAssessmentMonitorUi"));
    QApplication::setOrganizationName(QStringLiteral("FlightEnv"));
    rclcpp::init(argc, argv);

    LifeAssessmentMonitorWidget widget;
    widget.resize(1120, 740);
    widget.show();

    const int ret = app.exec();
    if (rclcpp::ok()) {
        rclcpp::shutdown();
    }
    return ret;
}
