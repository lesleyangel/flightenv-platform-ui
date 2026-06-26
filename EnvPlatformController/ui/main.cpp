#define NOMINMAX
#include <Windows.h>
#undef byte  // 取消 Windows 定义的 byte，保留 std::byte

#include <QtWidgets/QApplication>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPushButton>
#include <QTimer>
#include <QtDataVisualization/Q3DScatter>
#include <QtDataVisualization/QScatter3DSeries>
#include <chrono>
#include <iostream>
#include <fstream>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <emmintrin.h>
#include <thread>
#include <glog/logging.h>
#include "direct.h"
#include "../../include/EnvNodeTools/Utf8Console.h"
#include "EnvNodeSupport/LaunchSessionFacade.h"
#include "EnvNodeSupport/LaunchSessionHost.h"

#include "EnvPredictorUI.h"

#include <rclcpp/rclcpp.hpp>
#include <rmw/rmw.h>


#include <iomanip>
#include <mutex>
#include <sstream>


namespace {

    std::mutex g_trace_mutex;//日志锁


/**
* @brief 记录程序启动阶段的日志，日志格式包含时间戳、线程ID、启动耗时和当前阶段信息。日志输出到ui_startup_trace.log文件中
* @param stage 当前启动阶段的描述信息
* 
*/
void trace_startup(const char* stage)
{
#if defined(_MSC_VER)
    char* raw = nullptr;
    size_t raw_size = 0;
    const bool enabled = _dupenv_s(&raw, &raw_size, "FLIGHTENV_UI_STARTUP_TRACE") == 0 && raw && *raw;//读取环境变量FLIGHTENV_UI_STARTUP_TRACE，环境变量可能不存在
    std::free(raw);
#else
    // 其他操作系统读取
    const char* raw = std::getenv("FLIGHTENV_UI_STARTUP_TRACE");
    const bool enabled = raw && *raw;
#endif
    //日志记录
    if (!enabled) {
        return;
    }
    static const auto start_time = std::chrono::steady_clock::now();//程序气动时间
    const auto now_system = std::chrono::system_clock::now();//当前时间
    const auto now_steady = std::chrono::steady_clock::now();//统计耗时
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now_steady - start_time).count();//统计启动耗时
    auto time_t_now = std::chrono::system_clock::to_time_t(now_system);
    std::tm tm{};
#if defined(_WIN32)

    localtime_s(&tm, &time_t_now);

#else

    localtime_r(&time_t_now, &tm);

#endif
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now_system.time_since_epoch())% 1000;
    std::ostringstream tid_stream;
    tid_stream << std::this_thread::get_id();
    try 
    {
		std::lock_guard<std::mutex> lock(g_trace_mutex);
        std::ofstream out("ui_startup_trace.log", std::ios::app);
        out<< "["
            // 日期时间
            << std::put_time(&tm,"%Y-%m-%d %H:%M:%S")<< "."
            // 毫秒
            << std::setw(3)<< std::setfill('0')<< ms.count()<< "]"
            // 线程ID
            << "[TID:"<< tid_stream.str()<< "]"
            // 启动耗时
            << "[+"<< elapsed<< "ms] "
            // 当前阶段
            << stage<< std::endl;
        out.flush();
    }
    catch (...) {
    }
}

void shutdown_ros_if_needed()
{
    if (rclcpp::ok()) {
        rclcpp::shutdown();
    }
}

void verify_ros_ready()
{
    if (!rclcpp::ok()) {
        throw std::runtime_error("ROS2 context is not initialized. Check Visual Studio debugger environment.");
    }

    const char* rmw_id = rmw_get_implementation_identifier();
    if (!rmw_id || !*rmw_id) {
        throw std::runtime_error(
            "RMW implementation is not available. Check RMW_IMPLEMENTATION, PATH, and ROS2 DLL package.");
    }

    RCLCPP_INFO(rclcpp::get_logger("main"), "RMW implementation: %s", rmw_id);
}

bool env_flag_enabled(const char* name)
{
#if defined(_MSC_VER)
    char* raw = nullptr;
    size_t raw_size = 0;
    const bool enabled = _dupenv_s(&raw, &raw_size, name) == 0 && raw && *raw && std::string(raw) != "0";
    std::free(raw);
    return enabled;
#else
    const char* raw = std::getenv(name);
    return raw && *raw && std::string(raw) != "0";
#endif
}

int env_int_value(const char* name, int fallback)
{
#if defined(_MSC_VER)
    char* raw = nullptr;
    size_t raw_size = 0;
    const bool ok = _dupenv_s(&raw, &raw_size, name) == 0 && raw && *raw;
    if (!ok) {
        std::free(raw);
        return fallback;
    }
    try {
        const int value = std::stoi(raw);
        std::free(raw);
        return value > 0 ? value : fallback;
    } catch (...) {
        std::free(raw);
        return fallback;
    }
#else
    const char* raw = std::getenv(name);
    if (!raw || !*raw) {
        return fallback;
    }
    try {
        const int value = std::stoi(raw);
        return value > 0 ? value : fallback;
    } catch (...) {
        return fallback;
    }
#endif
}

void clear_env_var(const char* name)
{
#if defined(_MSC_VER)
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

} // namespace

int main(int argc, char* argv[])
{
    trace_startup("main: enter");
    configure_utf8_console();
    trace_startup("main: utf8 console configured");
    QApplication a(argc, argv);
    trace_startup("main: QApplication constructed");
    try {
#ifdef FLIGHTENV_USE_PLATFORM_BACKEND
        trace_startup("main: platform controller mode");
        auto window = std::make_unique<EnvPredictorUI>(
            nullptr,
            std::shared_ptr<StreamController>{},
            std::shared_ptr<launchsupport::LaunchSession<StreamController>>{});
        trace_startup("main: after EnvPredictorUI");
        window->show();
        trace_startup("main: window shown");
        const bool autoTrainTest = env_flag_enabled("FLIGHTENV_PLATFORM_UI_AUTOTRAIN_TEST");
        const bool autoStartTest = env_flag_enabled("FLIGHTENV_PLATFORM_UI_AUTOSTART_TEST");
        if (autoTrainTest || autoStartTest) {
            const int autoQuitMs = env_int_value("FLIGHTENV_PLATFORM_UI_AUTOQUIT_MS", 8000);
            clear_env_var("FLIGHTENV_PLATFORM_UI_AUTOTRAIN_TEST");
            clear_env_var("FLIGHTENV_PLATFORM_UI_AUTOSTART_TEST");
            clear_env_var("FLIGHTENV_PLATFORM_UI_AUTOQUIT_MS");
            QTimer::singleShot(1000, window.get(), [w = window.get(), autoStartTest]() {
                if (autoStartTest) {
                    if (auto* start = w->findChild<QPushButton*>("startBtn")) {
                        start->click();
                    }
                    return;
                }
                if (auto* train = w->findChild<QPushButton*>("trainBtn")) {
                    train->click();
                }
            });
            QTimer::singleShot(autoQuitMs, window.get(), [&a]() {
                a.quit();
            });
        }

        const int ret = a.exec();
        trace_startup("main: QApplication exited");

        if (window) {
            window->prepareForShutdown();
        }
        window.reset();

        trace_startup("main: exit success");
        return ret;
#else
        trace_startup("main: before rclcpp::init");
        rclcpp::init(argc, argv);
        trace_startup("main: after rclcpp::init");
        verify_ros_ready();
        trace_startup("main: ROS verified");

        StreamControllerConfig sc_cfg;
        trace_startup("main: before load_and_validate");
        auto preflight = launchsupport::load_and_validate({ argc, argv, true });
        trace_startup("main: after load_and_validate");
        trace_startup("main: before StreamController");
        auto stream_node = std::make_shared<StreamController>(sc_cfg);
        trace_startup("main: after StreamController");
        trace_startup("main: before LaunchSession");
        auto session = std::make_shared<launchsupport::LaunchSession<StreamController>>(
            std::move(preflight),
            std::move(stream_node));
        trace_startup("main: after LaunchSession");
        auto node = session->node();

        trace_startup("main: before LaunchSessionHost");
        auto host = std::make_unique<launchsupport::LaunchSessionHost<StreamController>>(session);
        trace_startup("main: after LaunchSessionHost");

        trace_startup("main: before EnvPredictorUI");
        auto window = std::make_unique<EnvPredictorUI>(nullptr, node, session);
        trace_startup("main: after EnvPredictorUI");
        window->show();
        trace_startup("main: window shown");

        int ret = a.exec();
        trace_startup("main: QApplication exited");

        if (window) {
            window->prepareForShutdown();
        }
        if (host) {
            host->shutdown();
        }
        window.reset();
        host.reset();
        node.reset();
        session.reset();
        stream_node.reset();
        RCLCPP_INFO(rclcpp::get_logger("main"), "Application exited.");
        shutdown_ros_if_needed();

        trace_startup("main: exit success");
        return ret;
#endif
    }
    catch (const std::exception& e) {
        trace_startup(e.what());
        QMessageBox::critical(
            nullptr,
            QStringLiteral("FlightEnv UI startup failed"),
            QString::fromLocal8Bit(e.what()));
        shutdown_ros_if_needed();
        return 1;
    }
}
