#include "TwinWorkbenchWindow.h"

#include "ContextBar.h"
#include "NavRail.h"
#include "RunStatusPill.h"

#include "../datahub/LegacyRunCatalogSource.h"
#include "../datahub/LiveDataHub.h"
#include "../datahub/PdkUiReaders.h"
#include "../datahub/PlatformRunController.h"
#include "../pages/ConfigPage.h"
#include "../pages/DataPlanePage.h"
#include "../pages/DiagnosticsPage.h"
#include "../pages/GraphPage.h"
#include "../pages/HealthPage.h"
#include "../pages/ModelsPage.h"
#include "../pages/ObjectPage.h"
#include "../pages/OnlinePage.h"
#include "../pages/OperatorLibraryPage.h"
#include "../pages/OverviewPage.h"
#include "../pages/ReplayPage.h"
#include "../pages/RuntimeHostPage.h"

#include <QAction>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <utility>

namespace twin {

namespace {

QString objectDisplayName(const PdkObjectPackageView& objectPackage) {
    const QString objectType = objectPackage.twinObjectJson.value(QStringLiteral("object_type")).toString();
    if (!objectType.isEmpty()) {
        return objectType;
    }
    return objectPackage.objectId.isEmpty() ? QStringLiteral("object") : objectPackage.objectId;
}

bool isValidObjectPackageRoot(const QString& root) {
    if (root.isEmpty()) {
        return false;
    }
    const QDir dir(root);
    return QFileInfo::exists(dir.filePath(QStringLiteral("object/twin_object.json"))) &&
           QFileInfo::exists(dir.filePath(QStringLiteral("assets/resources.json")));
}

QString inferObjectPackageRootFromFile(const QString& filePath) {
    const QFileInfo info(filePath);
    QDir dir = info.dir();
    if (info.fileName() == QStringLiteral("twin_object.json") &&
        dir.dirName() == QStringLiteral("object")) {
        dir.cdUp();
        return dir.absolutePath();
    }
    if (info.fileName() == QStringLiteral("resources.json") &&
        dir.dirName() == QStringLiteral("assets")) {
        dir.cdUp();
        return dir.absolutePath();
    }
    if (isValidObjectPackageRoot(dir.absolutePath())) {
        return dir.absolutePath();
    }
    return {};
}

QString modeLabel(const QString& mode) {
    if (mode == QStringLiteral("platform_online_run")) {
        return QStringLiteral("在线运行链路");
    }
    if (mode == QStringLiteral("platform_mainline")) {
        return QStringLiteral("主链路证据");
    }
    if (mode == QStringLiteral("platform_run")) {
        return QStringLiteral("运行证据");
    }
    if (mode == QStringLiteral("workflow/evidence")) {
        return QStringLiteral("编排证据");
    }
    return mode;
}

} // namespace

TwinWorkbenchWindow::TwinWorkbenchWindow(
    LiveDataHub* hub,
    LegacyRunCatalogSource* legacyRunCatalog,
    QString workspaceRoot,
    QString objectPackageRoot,
    QWidget* parent)
    : QMainWindow(parent),
      hub_(hub),
      legacyRunCatalog_(legacyRunCatalog),
      workspaceRoot_(std::move(workspaceRoot)),
      objectPackageRoot_(std::move(objectPackageRoot)) {
    setWindowTitle(QStringLiteral("FlightEnv · Twin Workbench"));
    resize(1320, 840);
    // 给布局一个尺寸下限：窗口再小，页面也走滚动而不是裁切重叠。
    setMinimumSize(1040, 680);

    auto* central = new QWidget(this);
    central->setObjectName(QStringLiteral("TwinRoot"));
    auto* rootRow = new QHBoxLayout(central);
    rootRow->setContentsMargins(0, 0, 0, 0);
    rootRow->setSpacing(0);

    nav_ = new NavRail(central);
    rootRow->addWidget(nav_);

    auto* mainCol = new QWidget(central);
    auto* colLayout = new QVBoxLayout(mainCol);
    colLayout->setContentsMargins(0, 0, 0, 0);
    colLayout->setSpacing(0);

    ctx_ = new ContextBar(mainCol);
    auto* loadObjectButton = ctx_->addActionButton(QStringLiteral("载入对象包"));
    connect(loadObjectButton, &QPushButton::clicked,
            this, &TwinWorkbenchWindow::loadObjectPackageDirectory);
    auto* loadObjectFileButton = ctx_->addActionButton(QStringLiteral("载入对象文件"));
    connect(loadObjectFileButton, &QPushButton::clicked,
            this, &TwinWorkbenchWindow::loadObjectPackageFile);
    colLayout->addWidget(ctx_);

    stack_ = new QStackedWidget(mainCol);
    stack_->setObjectName(QStringLiteral("PageArea"));
    colLayout->addWidget(stack_, 1);

    rootRow->addWidget(mainCol, 1);
    setCentralWidget(central);

    runController_ = new PlatformRunController(workspaceRoot_, objectPackageRoot_, this);
    setupMenus();

    if (hub_) {
        connect(hub_, &LiveDataHub::timelineUpdated,
                this, &TwinWorkbenchWindow::onTimeline);
        connect(runController_, &PlatformRunController::evidenceRootChanged,
                hub_, &LiveDataHub::setEvidenceRoot);
    }
    connect(nav_, &NavRail::pageChanged, this, &TwinWorkbenchWindow::showPage);
    rebuildPages();
}

void TwinWorkbenchWindow::addPage(const QString& key, QWidget* page) {
    // 每页统一套一层可伸缩滚动区：窗口变小时内容竖向滚动，不裁切。
    // 横向不滚动（内容应自适应换行/拉伸）；页面自带的内层滚动/Splitter 仍照常工作。
    auto* scroll = new QScrollArea(stack_);
    scroll->setObjectName(QStringLiteral("PageScroll"));
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(page);
    pageIndex_.insert(key, stack_->addWidget(scroll));
}

void TwinWorkbenchWindow::setupMenus() {
    auto* objectMenu = menuBar()->addMenu(QStringLiteral("对象"));

    auto* loadDir = new QAction(QStringLiteral("载入对象包目录..."), this);
    connect(loadDir, &QAction::triggered, this, &TwinWorkbenchWindow::loadObjectPackageDirectory);
    objectMenu->addAction(loadDir);

    auto* loadFile = new QAction(QStringLiteral("载入对象文件..."), this);
    connect(loadFile, &QAction::triggered, this, &TwinWorkbenchWindow::loadObjectPackageFile);
    objectMenu->addAction(loadFile);
}

void TwinWorkbenchWindow::rebuildPages() {
    const QString previousPage = currentPageKey_.isEmpty() ? QStringLiteral("overview") : currentPageKey_;
    while (stack_->count() > 0) {
        QWidget* page = stack_->widget(0);
        stack_->removeWidget(page);
        page->deleteLater();
    }
    pageIndex_.clear();
    onlinePage_ = nullptr;
    graphPage_ = nullptr;
    replayPage_ = nullptr;

    const QString evidenceRoot = hub_ ? hub_->evidenceRoot() : QString();
    auto* overviewPage = new OverviewPage(legacyRunCatalog_, evidenceRoot, objectPackageRoot_, stack_);
    onlinePage_ = new OnlinePage(objectPackageRoot_, stack_);
    auto* objectPage = new ObjectPage(objectPackageRoot_, stack_);
    objectPage->setEvidenceRoot(evidenceRoot);
    auto* modelsPage = new ModelsPage(objectPackageRoot_, stack_);
    auto* operatorsPage = new OperatorLibraryPage(objectPackageRoot_, stack_);
    operatorsPage->setEvidenceRoot(evidenceRoot);
    graphPage_ = new GraphPage(objectPackageRoot_, stack_);
    auto* runtimePage = new RuntimeHostPage(evidenceRoot, objectPackageRoot_, stack_);
    auto* dataPlanePage = new DataPlanePage(evidenceRoot, objectPackageRoot_, stack_);
    replayPage_ = new ReplayPage(legacyRunCatalog_, evidenceRoot, objectPackageRoot_, stack_);
    auto* healthPage = new HealthPage(objectPackageRoot_, evidenceRoot, stack_);
    auto* configPage = new ConfigPage(legacyRunCatalog_, evidenceRoot, objectPackageRoot_, stack_);
    auto* diagnosticsPage = new DiagnosticsPage(legacyRunCatalog_, evidenceRoot, objectPackageRoot_, stack_);

    addPage(QStringLiteral("overview"), overviewPage);
    addPage(QStringLiteral("online"), onlinePage_);
    addPage(QStringLiteral("object"), objectPage);
    addPage(QStringLiteral("models"), modelsPage);
    addPage(QStringLiteral("operators"), operatorsPage);
    addPage(QStringLiteral("graph"), graphPage_);
    addPage(QStringLiteral("runtime"), runtimePage);
    addPage(QStringLiteral("dataplane"), dataPlanePage);
    addPage(QStringLiteral("replay"), replayPage_);
    addPage(QStringLiteral("health"), healthPage);
    addPage(QStringLiteral("config"), configPage);
    addPage(QStringLiteral("diagnostics"), diagnosticsPage);

    connect(overviewPage, &OverviewPage::navigateTo, this, &TwinWorkbenchWindow::showPage);
    connect(objectPage, &ObjectPage::navigateTo, this, &TwinWorkbenchWindow::showPage);
    connect(modelsPage, &ModelsPage::navigateTo, this, &TwinWorkbenchWindow::showPage);
    connect(replayPage_, &ReplayPage::navigateTo, this, &TwinWorkbenchWindow::showPage);
    connect(healthPage, &HealthPage::navigateTo, this, &TwinWorkbenchWindow::showPage);

    if (hub_) {
        connect(hub_, &LiveDataHub::timelineUpdated,
                onlinePage_, &OnlinePage::onTimeline);
        connect(hub_, &LiveDataHub::timelineUpdated,
                graphPage_, &GraphPage::refresh);
        graphPage_->setEvidenceRoot(evidenceRoot);
        // 实时运行的 evidence 根经 timeline 下发，让对象画像三维网格也能在线点亮。
        connect(hub_, &LiveDataHub::timelineUpdated, objectPage,
                [objectPage](const QJsonObject& timeline) {
                    const QString runDir = timeline.value(QStringLiteral("mainline_run_dir")).toString(
                        timeline.value(QStringLiteral("run_dir")).toString());
                    if (!runDir.isEmpty()) {
                        objectPage->setEvidenceRoot(runDir);
                    }
                });
    }
    connect(onlinePage_, &OnlinePage::startMainlineRequested,
            runController_, &PlatformRunController::startWorkflow);
    connect(onlinePage_, &OnlinePage::prepareMainlineRequested,
            runController_, &PlatformRunController::prepareWorkflow);
    connect(runController_, &PlatformRunController::progressUpdated,
            onlinePage_, &OnlinePage::onRunProgress);
    connect(runController_, &PlatformRunController::statusChanged,
            onlinePage_, &OnlinePage::onRunStatus);
    connect(runController_, &PlatformRunController::logLine,
            onlinePage_, &OnlinePage::onRunLog);
    connect(runController_, &PlatformRunController::evidenceRootChanged,
            replayPage_, &ReplayPage::setCurrentEvidenceRoot);
    connect(runController_, &PlatformRunController::evidenceRootChanged,
            dataPlanePage, &DataPlanePage::setEvidenceRoot);
    connect(runController_, &PlatformRunController::evidenceRootChanged,
            runtimePage, &RuntimeHostPage::setEvidenceRoot);
    connect(runController_, &PlatformRunController::evidenceRootChanged,
            operatorsPage, [operatorsPage](const QString& root) { operatorsPage->setEvidenceRoot(root); });
    connect(runController_, &PlatformRunController::evidenceRootChanged,
            objectPage, [objectPage](const QString& root) { objectPage->setEvidenceRoot(root); });

    updateObjectContext();
    showPage(pageIndex_.contains(previousPage) ? previousPage : QStringLiteral("overview"));
}

void TwinWorkbenchWindow::setObjectPackageRoot(const QString& objectPackageRoot) {
    const QString cleanRoot = QDir::fromNativeSeparators(objectPackageRoot);
    if (!isValidObjectPackageRoot(cleanRoot)) {
        QMessageBox::warning(
            this,
            QStringLiteral("对象包无效"),
            QStringLiteral("请选择包含 object/twin_object.json 与 assets/resources.json 的对象包目录。"));
        return;
    }
    objectPackageRoot_ = cleanRoot;
    runController_->setObjectPackageRoot(objectPackageRoot_);
    rebuildPages();
}

void TwinWorkbenchWindow::updateObjectContext() {
    if (!ctx_) {
        return;
    }
    if (objectPackageRoot_.isEmpty()) {
        ctx_->setObjectField(QStringLiteral("未载入对象包"), QString());
        ctx_->setPhase(QStringLiteral("未选择"));
        ctx_->setMode(QStringLiteral("等待对象"));
        ctx_->setRun(QStringLiteral("-"));
        ctx_->setGraph(QStringLiteral("-"));
        ctx_->setWorkflow(QStringLiteral("-"));
        ctx_->setRunProfile(QStringLiteral("-"));
        ctx_->setClock(QStringLiteral("-"));
        ctx_->setRuntimeStatus(QStringLiteral("idle"));
        ctx_->setEvidenceRoot(QString());
        ctx_->statusPill()->setState(RunStatusPill::State::Idle, QStringLiteral("待载入"));
        return;
    }

    const PdkObjectPackageView objectPackage = PdkObjectPackageReader().read(objectPackageRoot_);
    if (objectPackage.ok()) {
        ctx_->setObjectField(objectDisplayName(objectPackage), objectPackage.objectId);
        ctx_->setPhase(QStringLiteral("对象包"));
        ctx_->setMode(QStringLiteral("已载入"));
        ctx_->statusPill()->setState(RunStatusPill::State::Idle, QStringLiteral("空闲"));
    } else {
        ctx_->setObjectField(QStringLiteral("对象包读取失败"), QFileInfo(objectPackageRoot_).fileName());
        ctx_->setPhase(QStringLiteral("错误"));
        ctx_->setMode(QStringLiteral("请重新载入"));
        ctx_->statusPill()->setState(RunStatusPill::State::Failed, QStringLiteral("对象错误"));
    }
    ctx_->setRun(QStringLiteral("-"));
    ctx_->setGraph(QStringLiteral("-"));
    ctx_->setWorkflow(QStringLiteral("-"));
    ctx_->setRunProfile(QStringLiteral("-"));
    ctx_->setClock(QStringLiteral("-"));
    ctx_->setRuntimeStatus(QStringLiteral("idle"));
    ctx_->setEvidenceRoot(hub_ ? hub_->evidenceRoot() : QString());
}

void TwinWorkbenchWindow::showPage(const QString& key) {
    const auto it = pageIndex_.find(key);
    if (it == pageIndex_.end()) {
        return;
    }
    currentPageKey_ = key;
    stack_->setCurrentIndex(it.value());
    nav_->setActive(key);
}

void TwinWorkbenchWindow::loadObjectPackageDirectory() {
    const QString dir = QFileDialog::getExistingDirectory(
        this,
        QStringLiteral("载入对象包目录"),
        objectPackageRoot_.isEmpty() ? workspaceRoot_ : objectPackageRoot_,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (dir.isEmpty()) {
        return;
    }
    setObjectPackageRoot(dir);
}

void TwinWorkbenchWindow::loadObjectPackageFile() {
    const QString file = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("载入对象文件"),
        objectPackageRoot_.isEmpty() ? workspaceRoot_ : objectPackageRoot_,
        QStringLiteral("对象文件 (twin_object.json resources.json *.json);;JSON 文件 (*.json);;所有文件 (*.*)"));
    if (file.isEmpty()) {
        return;
    }
    const QString root = inferObjectPackageRootFromFile(file);
    if (root.isEmpty()) {
        QMessageBox::warning(
            this,
            QStringLiteral("无法识别对象包"),
            QStringLiteral("请选择 object/twin_object.json、assets/resources.json，或对象包根目录下的 JSON 文件。"));
        return;
    }
    setObjectPackageRoot(root);
}

void TwinWorkbenchWindow::onTimeline(const QJsonObject& timeline) {
    const QString mode = timeline.value(QStringLiteral("mode")).toString();
    ctx_->setGraph(mode.isEmpty() ? QStringLiteral("编排证据") : modeLabel(mode));
    ctx_->setWorkflow(timeline.value(QStringLiteral("workflow_id")).toString());
    ctx_->setRunProfile(timeline.value(QStringLiteral("run_profile_id")).toString());
    ctx_->setEvidenceRoot(timeline.value(QStringLiteral("mainline_run_dir")).toString(
        timeline.value(QStringLiteral("run_dir")).toString(hub_ ? hub_->evidenceRoot() : QString())));
    const QString runStatus = timeline.value(QStringLiteral("status")).toString();
    ctx_->setRuntimeStatus(runStatus);
    auto* pill = ctx_->statusPill();
    if (runStatus == QStringLiteral("running")) {
        pill->setState(RunStatusPill::State::Running, QStringLiteral("运行中"));
    } else if (runStatus == QStringLiteral("ok")) {
        pill->setState(RunStatusPill::State::Idle, QStringLiteral("完成"));
    } else if (runStatus == QStringLiteral("failed")) {
        pill->setState(RunStatusPill::State::Failed, QStringLiteral("失败"));
    } else {
        pill->setState(RunStatusPill::State::Idle, QStringLiteral("空闲"));
    }

    const int frames = timeline.value(QStringLiteral("observed_frame_count")).toInt();
    const int runs = timeline.value(QStringLiteral("prediction_run_count")).toInt();
    const QString runId = timeline.value(QStringLiteral("run_id")).toString();
    ctx_->setRun(runId.isEmpty() ? QStringLiteral("frames %1 / pred %2").arg(frames).arg(runs) : runId);
    const QJsonObject clock = timeline.value(QStringLiteral("clock")).toObject();
    if (!clock.isEmpty()) {
        ctx_->setClock(QStringLiteral("t=%1 tick=%2")
            .arg(clock.value(QStringLiteral("run_time_s")).toDouble(0.0), 0, 'f', 2)
            .arg(clock.value(QStringLiteral("tick_index")).toInt(0)));
    }
}

} // namespace twin
