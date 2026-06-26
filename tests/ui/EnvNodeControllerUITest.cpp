#include "EnvNodeControllerUITest.h"

#include "EnvNodeSupport/LaunchSessionFacade.h"
#include "EnvNodeSupport/LaunchSessionHost.h"
#include "../../EnvPlatformController/ui/EnvPredictorUI.h"

#include <QApplication>
#include <QByteArray>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QStackedWidget>
#include <QTest>
#include <QTreeWidget>
#include <exception>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

bool includeNodeIntegration()
{
    const auto value = qEnvironmentVariable("FLIGHTENV_RUN_NODE_UI_INTEGRATION");
    return value == QStringLiteral("1") || value.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0;
}

std::filesystem::path findRepoRoot()
{
    std::filesystem::path dir = QCoreApplication::applicationDirPath().toStdWString();
    while (!dir.empty()) {
        if (std::filesystem::exists(dir / "EnvPlatformController") ||
            std::filesystem::exists(dir / "EnvNodeController")) {
            return dir;
        }
        const auto parent = dir.parent_path();
        if (parent == dir) {
            break;
        }
        dir = parent;
    }
    throw std::runtime_error("repository root not found from test executable path");
}

std::filesystem::path depsWorkspaceRoot(const std::filesystem::path& repoRoot)
{
    const auto depsEnv = qEnvironmentVariable("FLIGHTENV_DEPS_WORKSPACE_ROOT");
    if (!depsEnv.isEmpty()) {
        return std::filesystem::absolute(std::filesystem::path(depsEnv.toStdWString()));
    }

    const auto legacyEnv = qEnvironmentVariable("FLIGHTENV_WORKSPACE_ROOT");
    if (!legacyEnv.isEmpty()) {
        return std::filesystem::absolute(std::filesystem::path(legacyEnv.toStdWString()));
    }

    return repoRoot.parent_path() / "_deps" / "workspace";
}

std::filesystem::path localArtifactsRoot(const std::filesystem::path& repoRoot)
{
    const auto artifactsEnv = qEnvironmentVariable("FLIGHTENV_LOCAL_ARTIFACTS_ROOT");
    if (!artifactsEnv.isEmpty()) {
        return std::filesystem::absolute(std::filesystem::path(artifactsEnv.toStdWString()));
    }

    return repoRoot.parent_path() / "_local_artifacts" / "flightenv-controller-ui";
}

QString executablePath(const std::filesystem::path& repoRoot, const QString& projectName)
{
    const auto workspaceRoot = depsWorkspaceRoot(repoRoot);
    const QString platform =
#if defined(_WIN64)
        QStringLiteral("x64");
#else
        QStringLiteral("Win32");
#endif

#if defined(_DEBUG)
    const QString configuration = QStringLiteral("Debug");
#else
    const QString configuration = QStringLiteral("Release");
#endif

    const QString fileName = projectName + QStringLiteral(".exe");
    const QStringList candidates = {
        QDir(QString::fromStdWString((repoRoot / projectName.toStdWString() / platform.toStdWString() / configuration.toStdWString()).wstring())).filePath(fileName),
        QDir(QString::fromStdWString((workspaceRoot / platform.toStdWString() / configuration.toStdWString()).wstring())).filePath(fileName)
    };

    for (const auto& candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return QDir::toNativeSeparators(candidate);
        }
    }

    throw std::runtime_error(QStringLiteral("executable not found: %1").arg(fileName).toStdString());
}

std::filesystem::path makeIntegrationConfig(const std::filesystem::path& repoRoot)
{
    const auto exampleDir = depsWorkspaceRoot(repoRoot) / "example";
    const auto sourceCfg = exampleDir / "launcher_local_test_cfg.case_t03_online_sensor.json";
    const auto modelPath = exampleDir / "launcher_local_test_model.default.json";

    std::ifstream in(sourceCfg);
    if (!in) {
        throw std::runtime_error("failed to open source integration cfg: " + sourceCfg.string());
    }
    nlohmann::json cfg;
    in >> cfg;

    cfg["model_ref"]["path"] = std::filesystem::absolute(modelPath).string();
    cfg["test_eval"]["enabled"] = false;
    cfg["test_eval"]["write_online_sample_reports"] = false;

    const auto outDir = localArtifactsRoot(repoRoot) / "controller-ui-qtest";
    std::filesystem::create_directories(outDir);
    const auto outPath = outDir / "controller_ui_node_integration_cfg.json";
    std::ofstream out(outPath);
    if (!out) {
        throw std::runtime_error("failed to write integration cfg: " + outPath.string());
    }
    out << cfg.dump(2);
    return outPath;
}

class ScopedNodeProcess {
public:
    ScopedNodeProcess(QString name, QString program, QString workingDirectory)
        : name_(std::move(name))
    {
        process_.setProgram(std::move(program));
        process_.setWorkingDirectory(std::move(workingDirectory));
        process_.setProcessChannelMode(QProcess::MergedChannels);
        process_.setProcessEnvironment(QProcessEnvironment::systemEnvironment());
        process_.start();
        if (!process_.waitForStarted(10000)) {
            throw std::runtime_error(
                QStringLiteral("%1 did not start: %2")
                    .arg(name_, process_.errorString())
                    .toStdString());
        }
    }

    ~ScopedNodeProcess()
    {
        process_.terminate();
        if (!process_.waitForFinished(5000)) {
            process_.kill();
            process_.waitForFinished(5000);
        }
    }

    QProcess& process() { return process_; }

private:
    QString name_;
    QProcess process_;
};

void setNodeIntegrationEnvironment(const std::filesystem::path& repoRoot)
{
    qputenv("FLIGHTENV_DEPS_WORKSPACE_ROOT",
        QDir::toNativeSeparators(QString::fromStdWString(depsWorkspaceRoot(repoRoot).wstring())).toUtf8());
    const auto cyclonePath = repoRoot / "tools" / "launch" / "cyclonedds_localhost.xml";
    const auto cycloneUri =
        QStringLiteral("file://") +
        QDir::fromNativeSeparators(QString::fromStdWString(cyclonePath.wstring()));

    qputenv("RMW_IMPLEMENTATION", "rmw_cyclonedds_cpp");
    qputenv("ROS_LOCALHOST_ONLY", "0");
    if (qEnvironmentVariableIsEmpty("ROS_DOMAIN_ID")) {
        qputenv("ROS_DOMAIN_ID", "137");
    }
    qputenv("CYCLONEDDS_URI", cycloneUri.toUtf8());
    qputenv("FLIGHTENV_SENSOR_INIT_TIMEOUT_SECONDS", "120");
    qputenv("FLIGHTENV_FILTER_INIT_TIMEOUT_SECONDS", "240");
}

QTreeWidgetItem* childAt(QTreeWidget* tree, int rootIndex, int childIndex)
{
    auto* root = tree->topLevelItem(rootIndex);
    return root ? root->child(childIndex) : nullptr;
}

void clickTreeItem(QTreeWidget* tree, QTreeWidgetItem* item)
{
    QVERIFY(tree != nullptr);
    QVERIFY(item != nullptr);

    tree->scrollToItem(item);
    const QRect rect = tree->visualItemRect(item);
    QVERIFY2(rect.isValid(), "tree item is not visible");

    QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, rect.center());
    QCoreApplication::processEvents();
}

} // namespace

void EnvNodeControllerUITest::initTestCase()
{
    try {
        window_ = createWindowWithPlainNode();
    } catch (const std::exception& e) {
        QFAIL(qPrintable(QString("EnvPredictorUI construction threw: %1").arg(e.what())));
    } catch (...) {
        QFAIL("EnvPredictorUI construction threw an unknown exception");
    }

    window_->show();
    QVERIFY(QTest::qWaitForWindowExposed(window_));
}

void EnvNodeControllerUITest::cleanupTestCase()
{
    destroyWindowAndSession();
}

EnvPredictorUI* EnvNodeControllerUITest::createWindowWithPlainNode()
{
    StreamControllerConfig cfg;
    cfg.node_name = "env_node_controller_ui_test";
    node_ = std::make_shared<StreamController>(cfg);
    return new EnvPredictorUI(nullptr, node_, nullptr);
}

void EnvNodeControllerUITest::destroyWindowAndSession()
{
    delete window_;
    window_ = nullptr;
    session_.reset();
    node_.reset();
}

void EnvNodeControllerUITest::exposesRuntimeControlButtons()
{
    auto* start = window_->findChild<QPushButton*>("startBtn");
    auto* pause = window_->findChild<QPushButton*>("pauseBtn");
    auto* reset = window_->findChild<QPushButton*>("resetBtn");

    QVERIFY(start != nullptr);
    QVERIFY(pause != nullptr);
    QVERIFY(reset != nullptr);
    QVERIFY(!start->text().trimmed().isEmpty());
    QVERIFY(!pause->text().trimmed().isEmpty());
    QVERIFY(!reset->text().trimmed().isEmpty());
}

void EnvNodeControllerUITest::subjectTreeDrivesStackedModelPages()
{
    auto* tree = window_->findChild<QTreeWidget*>("treeWidget_subjects");
    auto* pages = window_->findChild<QStackedWidget*>("stackedWidget_2");

    QVERIFY(tree != nullptr);
    QVERIFY(pages != nullptr);
    QCOMPARE(tree->topLevelItemCount(), 3);

    clickTreeItem(tree, childAt(tree, 0, 0));
    QCOMPARE(pages->currentIndex(), 0);

    clickTreeItem(tree, childAt(tree, 0, 1));
    QCOMPARE(pages->currentIndex(), 5);

    clickTreeItem(tree, childAt(tree, 1, 0));
    QCOMPARE(pages->currentIndex(), 1);

    clickTreeItem(tree, childAt(tree, 1, 1));
    QCOMPARE(pages->currentIndex(), 4);

    clickTreeItem(tree, childAt(tree, 1, 2));
    QCOMPARE(pages->currentIndex(), 3);

    clickTreeItem(tree, tree->topLevelItem(2));
    QCOMPARE(pages->currentIndex(), 2);
}

void EnvNodeControllerUITest::closingWindowClearsSdkCallbacks()
{
    QVERIFY(node_ != nullptr);
    QVERIFY(window_ != nullptr);

    node_->onLog = [](const std::string&) {};
    node_->onPredictionResult = [](const contracts::PredictionResultDTO&) {};
    node_->onSensorFrame = [](const contracts::SensorFrame&) {};
    node_->onStateFrame = [](const contracts::StateFrame&) {};
    node_->onRuntimeSnapshot = [](const contracts::RuntimeSnapshotDTO&) {};
    node_->onRuntimeMeta = [](const contracts::RuntimeMetaDTO&) {};

    QVERIFY(static_cast<bool>(node_->onLog));
    QVERIFY(static_cast<bool>(node_->onPredictionResult));
    QVERIFY(static_cast<bool>(node_->onSensorFrame));
    QVERIFY(static_cast<bool>(node_->onStateFrame));
    QVERIFY(static_cast<bool>(node_->onRuntimeSnapshot));
    QVERIFY(static_cast<bool>(node_->onRuntimeMeta));

    QVERIFY(window_->close());
    QCoreApplication::processEvents();
    QTRY_VERIFY(!window_->isVisible());

    QVERIFY(!node_->onLog);
    QVERIFY(!node_->onPredictionResult);
    QVERIFY(!node_->onSensorFrame);
    QVERIFY(!node_->onStateFrame);
    QVERIFY(!node_->onRuntimeSnapshot);
    QVERIFY(!node_->onRuntimeMeta);

    destroyWindowAndSession();
    window_ = createWindowWithPlainNode();
    window_->show();
    QVERIFY(QTest::qWaitForWindowExposed(window_));
}

void EnvNodeControllerUITest::trainButtonClickDoesNotCloseWindow()
{
    if (includeNodeIntegration()) {
        QSKIP("Skipped when the process-level node/UI integration test needs the active ROS context.");
    }

    auto* train = window_->findChild<QPushButton*>("trainBtn");
    QVERIFY(train != nullptr);

    QTest::mouseClick(train, Qt::LeftButton);
    QCoreApplication::processEvents();

    QVERIFY(window_->isVisible());
}

void EnvNodeControllerUITest::startButtonClickDoesNotRequireRunningRos()
{
    if (includeNodeIntegration()) {
        QSKIP("Skipped when the process-level node/UI integration test needs the active ROS context.");
    }

    auto* start = window_->findChild<QPushButton*>("startBtn");
    QVERIFY(start != nullptr);

    if (rclcpp::ok()) {
        rclcpp::shutdown();
    }
    QTest::mouseClick(start, Qt::LeftButton);
    QCoreApplication::processEvents();

    QVERIFY(window_->isVisible());
}

void EnvNodeControllerUITest::startButtonInitializesNodesAndStartsStreaming()
{
    if (!includeNodeIntegration()) {
        QSKIP("Set FLIGHTENV_RUN_NODE_UI_INTEGRATION=1 to run the process-level node/UI integration test.");
    }

    if (!rclcpp::ok()) {
        QSKIP("ROS context was shut down by a previous lightweight start-button test.");
    }

    destroyWindowAndSession();

    const auto repoRoot = findRepoRoot();
    setNodeIntegrationEnvironment(repoRoot);

    const QString exampleDir = QDir::toNativeSeparators(
        QString::fromStdWString((depsWorkspaceRoot(repoRoot) / "example").wstring()));
    const QString senserExe = executablePath(repoRoot, QStringLiteral("EnvNodeSenser"));
    const QString filterExe = executablePath(repoRoot, QStringLiteral("EnvNodeFilter"));
    ScopedNodeProcess senser(QStringLiteral("EnvNodeSenser"), senserExe, exampleDir);
    QTest::qWait(2000);
    ScopedNodeProcess filter(QStringLiteral("EnvNodeFilter"), filterExe, exampleDir);
    QTest::qWait(2000);

    StreamControllerConfig cfg;
    cfg.node_name = "env_node_controller_ui_integration_test";
    node_ = std::make_shared<StreamController>(cfg);

    const QByteArray cfgPath =
        QDir::toNativeSeparators(QString::fromStdWString(makeIntegrationConfig(repoRoot).wstring()))
            .toLocal8Bit();
    char arg0[] = "EnvNodeControllerUITests";
    char arg1[] = "";
    char arg2[] = "";
    char arg3[] = "";
    char arg4[] = "";
    std::vector<char> cfgPathBuffer(cfgPath.begin(), cfgPath.end());
    cfgPathBuffer.push_back('\0');
    char* argv[] = { arg0, arg1, arg2, arg3, arg4, cfgPathBuffer.data() };
    const int argc = static_cast<int>(sizeof(argv) / sizeof(argv[0]));

    session_ = std::make_shared<launchsupport::LaunchSession<StreamController>>(
        launchsupport::load_and_validate({ argc, argv, true }),
        node_);
    launchsupport::LaunchSessionHost<StreamController> host(session_);

    window_ = new EnvPredictorUI(nullptr, node_, session_);
    window_->show();
    QVERIFY(QTest::qWaitForWindowExposed(window_));

    auto* start = window_->findChild<QPushButton*>("startBtn");
    QVERIFY(start != nullptr);
    QTest::mouseClick(start, Qt::LeftButton);
    QCoreApplication::processEvents();

    QTRY_VERIFY_WITH_TIMEOUT(node_->runtime_snapshot_copy().has_value(), 260000);
    QVERIFY(session_->runtime_snapshot().has_value());
    QVERIFY(session_->runtime_meta().has_value());
    auto* sensorCombo = window_->findChild<QComboBox*>("comboBox_4");
    QVERIFY(sensorCombo != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(sensorCombo->count() > 0, 260000);
    auto controller = std::make_shared<launchsupport::ControllerViewAdapter>(node_, session_);
    QVERIFY(controller->runtime_snapshot().has_value());
    QVERIFY(controller->runtime_meta().has_value());
    QVERIFY(window_->isVisible());

    session_->stop_streaming();
    host.shutdown();
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    if (includeNodeIntegration()) {
        setNodeIntegrationEnvironment(findRepoRoot());
    }
    rclcpp::init(argc, argv);

    EnvNodeControllerUITest test;
    const int result = QTest::qExec(&test, argc, argv);
    if (rclcpp::ok()) {
        rclcpp::shutdown();
    }
    return result;
}
