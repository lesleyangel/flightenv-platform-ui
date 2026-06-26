#pragma once

#include <QObject>
#include <QProcess>
#include <memory>

#include "../../EnvPlatformController/StreamController.h"

class EnvPredictorUI;
namespace launchsupport {
template<typename NodeT>
class LaunchSession;
}

class EnvNodeControllerUITest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void exposesRuntimeControlButtons();
    void subjectTreeDrivesStackedModelPages();
    void closingWindowClearsSdkCallbacks();
    void trainButtonClickDoesNotCloseWindow();
    void startButtonClickDoesNotRequireRunningRos();
    void startButtonInitializesNodesAndStartsStreaming();

private:
    EnvPredictorUI* createWindowWithPlainNode();
    void destroyWindowAndSession();

    std::shared_ptr<StreamController> node_;
    std::shared_ptr<launchsupport::LaunchSession<StreamController>> session_;
    EnvPredictorUI* window_ = nullptr;
};
