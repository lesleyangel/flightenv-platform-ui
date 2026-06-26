#include "OnlinePage.h"

#include "../widgets/BranchRunExplorerWidgets.h"
#include "../widgets/GraphWorkflowDisplayWidgets.h"
#include "../widgets/KvList.h"
#include "../widgets/Panel.h"

#include <QDateTime>
#include <QAbstractItemView>
#include <QComboBox>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QSplitter>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace twin {

using flightenv::ui::display::ScalarTrendWidget;

namespace {

QProgressBar* makeProgressBar(QWidget* parent)
{
    auto* bar = new QProgressBar(parent);
    bar->setRange(0, 1000);
    bar->setValue(0);
    bar->setTextVisible(true);
    return bar;
}

QTableWidget* makeTable(const QStringList& headers, QWidget* parent)
{
    auto* table = new QTableWidget(parent);
    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->verticalHeader()->hide();
    table->horizontalHeader()->setStretchLastSection(true);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    return table;
}

QScrollArea* makeColumnScroll(QWidget* content, QWidget* parent)
{
    auto* scroll = new QScrollArea(parent);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setWidget(content);
    return scroll;
}

QString statusText(const QString& status)
{
    if (status == QStringLiteral("running")) {
        return QStringLiteral("运行中");
    }
    if (status == QStringLiteral("completed")) {
        return QStringLiteral("已完成");
    }
    if (status == QStringLiteral("failed")) {
        return QStringLiteral("失败");
    }
    if (status == QStringLiteral("ok")) {
        return QStringLiteral("正常");
    }
    return status.isEmpty() ? QStringLiteral("空闲") : status;
}

QString stageText(const QString& stage)
{
    if (stage == QStringLiteral("starting")) {
        return QStringLiteral("启动后端进程");
    }
    if (stage == QStringLiteral("initializing")) {
        return QStringLiteral("初始化对象与算子");
    }
    if (stage == QStringLiteral("compile_online")) {
        return QStringLiteral("编译在线 workflow");
    }
    if (stage == QStringLiteral("compile_future")) {
        return QStringLiteral("编译预测 workflow");
    }
    if (stage == QStringLiteral("waiting_external_input")) {
        return QStringLiteral("等待外部观测输入");
    }
    if (stage == QStringLiteral("online_running")) {
        return QStringLiteral("在线融合运行中");
    }
    if (stage == QStringLiteral("prediction_running")) {
        return QStringLiteral("预测分支运行中");
    }
    if (stage == QStringLiteral("completed")) {
        return QStringLiteral("主链路完成");
    }
    if (stage == QStringLiteral("failed")) {
        return QStringLiteral("主链路失败");
    }
    return stage.isEmpty() ? QStringLiteral("等待启动") : stage;
}

QString stopReasonText(const QString& reason)
{
    if (reason.isEmpty()) {
        return QStringLiteral("-");
    }
    if (reason == QStringLiteral("input_exhausted")) {
        return QStringLiteral("输入耗尽");
    }
    if (reason == QStringLiteral("max_iterations") || reason == QStringLiteral("max_steps")) {
        return QStringLiteral("达到最大步数");
    }
    if (reason == QStringLiteral("failure_reached")) {
        return QStringLiteral("达到失效阈值");
    }
    if (reason == QStringLiteral("ground_reached")) {
        return QStringLiteral("到达落点");
    }
    return reason;
}

QString valueText(const QJsonValue& value, int precision = 2)
{
    if (!value.isDouble()) {
        return QStringLiteral("-");
    }
    return QString::number(value.toDouble(), 'f', precision);
}

QJsonArray timelinePoints(const QJsonObject& timeline)
{
    QJsonArray points = timeline.value(QStringLiteral("timeline_points")).toArray();
    if (!points.isEmpty()) {
        return points;
    }
    points = timeline.value(QStringLiteral("online_frames")).toArray();
    for (int i = 0; i < points.size(); ++i) {
        QJsonObject point = points.at(i).toObject();
        point.insert(QStringLiteral("branch_id"), QStringLiteral("main.online"));
        point.insert(QStringLiteral("point_kind"), QStringLiteral("在线融合帧"));
        if (!point.contains(QStringLiteral("loop_iteration_index"))) {
            point.insert(QStringLiteral("loop_iteration_index"),
                         point.value(QStringLiteral("frame_index")).toInt(i));
        }
        points.replace(i, point);
    }
    return points;
}

QString primaryBranchId(const QJsonObject& timeline)
{
    const QString primary = timeline.value(QStringLiteral("primary_branch_id")).toString();
    if (!primary.isEmpty()) {
        return primary;
    }
    const QJsonArray branches = timeline.value(QStringLiteral("branch_descriptors")).toArray(
        timeline.value(QStringLiteral("branches")).toArray());
    if (!branches.isEmpty()) {
        const QString branch = branches.first().toObject().value(QStringLiteral("branch_id")).toString();
        if (!branch.isEmpty()) {
            return branch;
        }
    }
    return QStringLiteral("main.online");
}

int latestLoopForBranch(const QJsonObject& timeline, const QString& branchId)
{
    int latest = -1;
    for (const QJsonValue& value : timelinePoints(timeline)) {
        const QJsonObject point = value.toObject();
        if (point.value(QStringLiteral("branch_id")).toString(QStringLiteral("main.online")) != branchId) {
            continue;
        }
        const int loop = point.value(QStringLiteral("loop_iteration_index"))
                             .toInt(point.value(QStringLiteral("frame_index")).toInt(latest));
        latest = std::max(latest, loop);
    }
    return latest;
}

// 当前分支上的有序帧序列（loop_iteration_index 去重升序）——时间轴 scrubber 用。
QVector<int> loopIndicesForBranch(const QJsonObject& timeline, const QString& branchId)
{
    QVector<int> loops;
    for (const QJsonValue& value : timelinePoints(timeline)) {
        const QJsonObject point = value.toObject();
        if (point.value(QStringLiteral("branch_id")).toString(QStringLiteral("main.online")) != branchId) {
            continue;
        }
        const int loop = point.value(QStringLiteral("loop_iteration_index"))
                             .toInt(point.value(QStringLiteral("frame_index")).toInt(-1));
        if (loop >= 0 && !loops.contains(loop)) {
            loops.push_back(loop);
        }
    }
    std::sort(loops.begin(), loops.end());
    return loops;
}

QJsonObject latestPointForBranch(const QJsonObject& timeline, const QString& branchId)
{
    QJsonObject best;
    int bestLoop = -1;
    for (const QJsonValue& value : timelinePoints(timeline)) {
        const QJsonObject point = value.toObject();
        if (point.value(QStringLiteral("branch_id")).toString(QStringLiteral("main.online")) != branchId) {
            continue;
        }
        const int loop = point.value(QStringLiteral("loop_iteration_index"))
                             .toInt(point.value(QStringLiteral("frame_index")).toInt(-1));
        if (loop >= bestLoop) {
            bestLoop = loop;
            best = point;
        }
    }
    return best;
}

QJsonObject selectedState(const QJsonObject& point)
{
    QJsonObject state = point.value(QStringLiteral("selected_state")).toObject();
    if (!state.isEmpty()) {
        return state;
    }
    state = point.value(QStringLiteral("state")).toObject();
    return state.isEmpty() ? point.value(QStringLiteral("posterior")).toObject() : state;
}

QJsonObject ballisticState(const QJsonObject& point)
{
    const QJsonObject state = selectedState(point);
    const QJsonObject components = state.value(QStringLiteral("components")).toObject();
    const QJsonObject ballistic = components.value(QStringLiteral("ballistic")).toObject();
    return ballistic.isEmpty() ? state.value(QStringLiteral("ballistic")).toObject() : ballistic;
}

double numberFromPoint(const QJsonObject& point, const QString& key, const QString& ballisticKey = {})
{
    if (point.value(key).isDouble()) {
        return point.value(key).toDouble();
    }
    const QJsonObject ballistic = ballisticState(point);
    if (!ballisticKey.isEmpty() && ballistic.value(ballisticKey).isDouble()) {
        return ballistic.value(ballisticKey).toDouble();
    }
    return std::numeric_limits<double>::quiet_NaN();
}

} // namespace

OnlinePage::OnlinePage(QString objectPackageRoot, QWidget* parent)
    : QWidget(parent),
      objectPackage_(PdkObjectPackageReader().read(objectPackageRoot))
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    auto* mainSplitter = new QSplitter(Qt::Horizontal, this);
    mainSplitter->setChildrenCollapsible(false);
    mainSplitter->setOpaqueResize(false);
    root->addWidget(mainSplitter, 1);

    auto* leftWidget = new QWidget(this);
    leftWidget->setMinimumWidth(260);
    auto* leftCol = new QVBoxLayout(leftWidget);
    leftCol->setSpacing(12);

    auto* controlPanel = new Panel(QStringLiteral("平台运行控制"), this);
    controlPanel->setSubtitle(QStringLiteral("初始化对象 / 启动外部观测驱动 / 记录 evidence"));
    workflowCombo_ = new QComboBox(controlPanel->body());
    profileCombo_ = new QComboBox(controlPanel->body());
    for (const PdkWorkflowView& workflow : objectPackage_.workflows) {
        workflowCombo_->addItem(
            QStringLiteral("%1 / %2").arg(workflow.workflowId, workflow.phase),
            workflow.workflowId);
    }
    if (workflowCombo_->count() == 0) {
        workflowCombo_->addItem(QStringLiteral("No workflow in object package"), QString());
    }
    profileCombo_->addItem(QStringLiteral("All active operators"), QString());
    for (const QJsonValue& value : objectPackage_.twinObjectJson.value(QStringLiteral("run_profiles")).toArray()) {
        const QString profileId = value.toObject().value(QStringLiteral("profile_id")).toString();
        if (!profileId.isEmpty()) {
            profileCombo_->addItem(profileId, profileId);
        }
    }
    for (int i = 0; i < workflowCombo_->count(); ++i) {
        if (workflowCombo_->itemData(i).toString().contains(QStringLiteral("online_filtering_external_input"))) {
            workflowCombo_->setCurrentIndex(i);
            break;
        }
    }
    for (int i = 0; i < profileCombo_->count(); ++i) {
        if (profileCombo_->itemData(i).toString() == QStringLiteral("online_filtering_full")) {
            profileCombo_->setCurrentIndex(i);
            break;
        }
    }
    controlPanel->bodyLayout()->addWidget(workflowCombo_);
    controlPanel->bodyLayout()->addWidget(profileCombo_);
    prepareRunButton_ = new QPushButton(QStringLiteral("初始化算子 / 加载模型"), controlPanel->body());
    startRunButton_ = new QPushButton(QStringLiteral("启动外部数据回放驱动"), controlPanel->body());
    connect(prepareRunButton_, &QPushButton::clicked, this, [this]() {
        emit prepareMainlineRequested(
            workflowCombo_->currentData().toString(),
            profileCombo_->currentData().toString());
    });
    connect(startRunButton_, &QPushButton::clicked, this, [this]() {
        emit startMainlineRequested(
            workflowCombo_->currentData().toString(),
            profileCombo_->currentData().toString());
    });
    controlPanel->bodyLayout()->addWidget(prepareRunButton_);
    controlPanel->bodyLayout()->addWidget(startRunButton_);

    auto* runKv = new KvList(controlPanel->body());
    runStageVal_ = runKv->addRow(QStringLiteral("当前阶段"), QStringLiteral("等待启动"), false);
    runMessageVal_ = runKv->addRow(QStringLiteral("运行状态"), QStringLiteral("空闲"), false);
    runLogVal_ = runKv->addRow(QStringLiteral("最新日志"), QStringLiteral("-"), false);
    clockProgressVal_ = runKv->addRow(QStringLiteral("平台时钟"), QStringLiteral("t=0.00s / tick=0 / dt=0.00s"), false);
    initializationStatusVal_ = runKv->addRow(QStringLiteral("算子初始化"), QStringLiteral("未执行"), false);
    controlPanel->bodyLayout()->addWidget(runKv);

    totalProgressBar_ = makeProgressBar(controlPanel->body());
    onlineProgressBar_ = makeProgressBar(controlPanel->body());
    predictionProgressBar_ = makeProgressBar(controlPanel->body());
    totalProgressBar_->setFormat(QStringLiteral("总进度 %p%"));
    onlineProgressBar_->setFormat(QStringLiteral("在线帧 %p%"));
    predictionProgressBar_->setFormat(QStringLiteral("预测分支 %p%"));
    controlPanel->bodyLayout()->addWidget(totalProgressBar_);
    controlPanel->bodyLayout()->addWidget(onlineProgressBar_);
    controlPanel->bodyLayout()->addWidget(predictionProgressBar_);

    initTable_ = makeTable({
        QStringLiteral("workflow"),
        QStringLiteral("算子"),
        QStringLiteral("模型/资源"),
        QStringLiteral("状态")
    }, controlPanel->body());
    initTable_->setMinimumHeight(92);
    controlPanel->bodyLayout()->addWidget(initTable_);
    leftCol->addWidget(controlPanel);

    auto* inputPanel = new Panel(QStringLiteral("实时输入"), this);
    inputPanel->setSubtitle(QStringLiteral("外部传感器 / 数据库回放 / 文件队列"));
    auto* inputKv = new KvList(inputPanel->body());
    dataSourceVal_ = inputKv->addRow(QStringLiteral("数据源"), QStringLiteral("等待 Runtime Host"), false);
    latestStateVal_ = inputKv->addRow(QStringLiteral("最新状态"), QStringLiteral("-"));
    freshnessVal_ = inputKv->addRow(QStringLiteral("当前时刻"), QStringLiteral("-"));
    onlineFramesVal_ = inputKv->addRow(QStringLiteral("在线帧数"), QStringLiteral("0"));
    inputPanel->bodyLayout()->addWidget(inputKv);
    leftCol->addWidget(inputPanel);

    auto* filterPanel = new Panel(QStringLiteral("滤波状态"), this);
    filterPanel->setSubtitle(QStringLiteral("观测方程 + 粒子滤波摘要"));
    auto* filterKv = new KvList(filterPanel->body());
    essVal_ = filterKv->addRow(QStringLiteral("ESS"), QStringLiteral("-"));
    particleVal_ = filterKv->addRow(QStringLiteral("粒子数"), QStringLiteral("-"));
    residualVal_ = filterKv->addRow(QStringLiteral("残差范数"), QStringLiteral("-"));
    resampleVal_ = filterKv->addRow(QStringLiteral("重采样次数"), QStringLiteral("0"));
    filterPanel->bodyLayout()->addWidget(filterKv);

    essTrend_ = new ScalarTrendWidget(filterPanel->body());
    essTrend_->setTitle(QStringLiteral("ESS"));
    essTrend_->setFixedHeight(72);
    filterPanel->bodyLayout()->addWidget(essTrend_);

    residualTrend_ = new ScalarTrendWidget(filterPanel->body());
    residualTrend_->setTitle(QStringLiteral("观测残差范数"));
    residualTrend_->setFixedHeight(72);
    filterPanel->bodyLayout()->addWidget(residualTrend_);
    leftCol->addWidget(filterPanel);

    auto* runtimePanel = new Panel(QStringLiteral("运行时调度"), this);
    runtimePanel->setSubtitle(QStringLiteral("统一时钟 / 调度器 / 预测分支"));
    auto* runtimeKv = new KvList(runtimePanel->body());
    runtimeVal_ = runtimeKv->addRow(QStringLiteral("主时间轴"), QStringLiteral("-"), false);
    schedulerVal_ = runtimeKv->addRow(QStringLiteral("调度吞吐"), QStringLiteral("-"), false);
    predictionVal_ = runtimeKv->addRow(QStringLiteral("预测分支"), QStringLiteral("-"), false);
    runtimePanel->bodyLayout()->addWidget(runtimeKv);
    leftCol->addWidget(runtimePanel);
    leftCol->addStretch(1);

    auto* middleWidget = new QWidget(this);
    middleWidget->setMinimumWidth(280);
    auto* middleCol = new QVBoxLayout(middleWidget);
    middleCol->setSpacing(12);

    auto* treePanel = new Panel(QStringLiteral("分支树"), this);
    treePanel->setSubtitle(QStringLiteral("在线主分支 / 预测分支 / 父子关系"));
    treePanel->setMinimumHeight(280);
    branchTree_ = new BranchTreeWidget(treePanel->body());
    branchTree_->setMinimumHeight(250);
    treePanel->bodyLayout()->addWidget(branchTree_, 1);
    middleCol->addWidget(treePanel, 2);

    auto* timelinePanel = new Panel(QStringLiteral("分支时间线"), this);
    timelinePanel->setSubtitle(QStringLiteral("在线融合帧 / 预测步 / 可切换历史点"));

    // 时间轴 scrubber：拖动在当前分支的帧序列上回看，驱动状态/场/曲线。
    auto* scrubRow = new QHBoxLayout();
    scrubRow->setSpacing(8);
    auto* scrubTitle = new QLabel(QStringLiteral("时间轴"), timelinePanel->body());
    scrubTitle->setProperty("tiny", true);
    scrubRow->addWidget(scrubTitle);
    timeScrubber_ = new QSlider(Qt::Horizontal, timelinePanel->body());
    timeScrubber_->setEnabled(false);
    timeScrubber_->setPageStep(1);
    scrubRow->addWidget(timeScrubber_, 1);
    scrubberLabel_ = new QLabel(QStringLiteral("—"), timelinePanel->body());
    scrubberLabel_->setProperty("mono", true);
    scrubberLabel_->setMinimumWidth(190);
    scrubberLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    scrubRow->addWidget(scrubberLabel_);
    timelinePanel->bodyLayout()->addLayout(scrubRow);
    connect(timeScrubber_, &QSlider::valueChanged, this, &OnlinePage::onScrubberMoved);

    branchTimeline_ = new BranchTimelineWidget(timelinePanel->body());
    timelinePanel->bodyLayout()->addWidget(branchTimeline_, 1);
    timelinePanel->body()->setMinimumHeight(180);
    middleCol->addWidget(timelinePanel, 3);

    // 当前状态从右列移到中列：选中时间线某点即在下方看到状态/滤波诊断，右列整列让给云图。
    auto* statePanel = new Panel(QStringLiteral("当前状态"), this);
    statePanel->setSubtitle(QStringLiteral("选中分支 + 选中时间点 + 滤波诊断"));
    branchState_ = new BranchStatePanel(statePanel->body());
    statePanel->bodyLayout()->addWidget(branchState_, 1);
    middleCol->addWidget(statePanel, 2);

    auto* seriesPanel = new Panel(QStringLiteral("参数历程"), this);
    seriesPanel->setSubtitle(QStringLiteral("按当前分支自动识别标量序列"));
    branchSeries_ = new BranchSeriesPanel(seriesPanel->body());
    seriesPanel->bodyLayout()->addWidget(branchSeries_, 1);
    middleCol->addWidget(seriesPanel, 2);

    // 右列整列让给真实场云图：VTK 不放进滚动容器，直接吃满列高，配合下方 artifact/QoI 表。
    auto* fieldPanel = new Panel(QStringLiteral("真实场云图"), this);
    fieldPanel->setSubtitle(QStringLiteral("完整 field artifact + 对象包 mesh layout + VTK"));
    fieldPanel->setMinimumWidth(460);
    fieldPanel->setMinimumHeight(560);
    branchField_ = new BranchFieldPanel(fieldPanel->body());
    branchField_->setAssetRoot(objectPackage_.rootPath);
    fieldPanel->bodyLayout()->addWidget(branchField_, 1);

    mainSplitter->addWidget(makeColumnScroll(leftWidget, mainSplitter));
    mainSplitter->addWidget(makeColumnScroll(middleWidget, mainSplitter));
    mainSplitter->addWidget(fieldPanel);
    mainSplitter->setStretchFactor(0, 1);
    mainSplitter->setStretchFactor(1, 2);
    mainSplitter->setStretchFactor(2, 4);
    mainSplitter->setSizes({300, 440, 780});

    connect(branchTree_, &BranchTreeWidget::branchSelected,
            this, &OnlinePage::selectBranch);
    connect(branchTimeline_, &BranchTimelineWidget::timelinePointSelected,
            this, &OnlinePage::selectTimelinePoint);
    connect(branchField_, &BranchFieldPanel::fieldOptionSelected,
            this, [this](const QString& optionId, const QString& branchId, int loop) {
                if (runLogVal_) {
                    runLogVal_->setText(QStringLiteral("显示完整场：%1 / loop %2 / %3")
                                            .arg(branchId)
                                            .arg(loop)
                                            .arg(optionId));
                }
            });
}

void OnlinePage::onRuntimeSnapshot(const contracts::RuntimeSnapshotDTO& snapshot)
{
    Q_UNUSED(snapshot);
}

void OnlinePage::onPrediction(const contracts::PredictionResultDTO& prediction)
{
    Q_UNUSED(prediction);
    ++livePredictionCount_;
    if (predictionVal_) {
        predictionVal_->setText(QStringLiteral("实时消息 %1 次；主显示以 branch/timeline snapshot 为准")
                                    .arg(livePredictionCount_));
    }
}

void OnlinePage::onState(const contracts::StateFrame& state)
{
    const qint64 nowNs = QDateTime::currentMSecsSinceEpoch() * 1000000LL;
    if (lastStateNs_ > 0 && freshnessVal_) {
        const double freshMs = (nowNs - lastStateNs_) / 1.0e6;
        freshnessVal_->setText(QStringLiteral("实时状态延迟 %1 ms").arg(freshMs, 0, 'f', 0));
    }
    lastStateNs_ = nowNs;
    if (latestStateVal_) {
        latestStateVal_->setText(QStringLiteral("tp=%1 / h=%2m / Ma=%3")
                                     .arg(state.taskpoint_id)
                                     .arg(state.h, 0, 'f', 0)
                                     .arg(state.ma, 0, 'f', 2));
    }
}

void OnlinePage::setProgressValue(QProgressBar* bar, const double percent)
{
    if (!bar) {
        return;
    }
    bar->setValue(static_cast<int>(std::clamp(percent, 0.0, 100.0) * 10.0));
}

void OnlinePage::onRunProgress(const QJsonObject& progress)
{
    if (progress.isEmpty()) {
        return;
    }

    const QString stage = progress.value(QStringLiteral("stage")).toString();
    const QString status = progress.value(QStringLiteral("status")).toString();
    const QString message = progress.value(QStringLiteral("message")).toString();
    if (runStageVal_) {
        runStageVal_->setText(stageText(stage));
    }
    if (runMessageVal_) {
        runMessageVal_->setText(QStringLiteral("%1 / %2")
                                    .arg(statusText(status),
                                         message.isEmpty() ? QStringLiteral("-") : message));
    }

    const QJsonObject operation = progress.value(QStringLiteral("operation")).toObject();
    if (runLogVal_ && !operation.isEmpty()) {
        runLogVal_->setText(QStringLiteral("%1 / %2 / %3s")
                                .arg(operation.value(QStringLiteral("name")).toString(QStringLiteral("-")),
                                     statusText(operation.value(QStringLiteral("status")).toString()),
                                     QString::number(operation.value(QStringLiteral("elapsed_s")).toInt(0))));
    }

    const bool running = status == QStringLiteral("running");
    if (startRunButton_) {
        startRunButton_->setEnabled(!running);
        startRunButton_->setText(running
            ? QStringLiteral("外部数据驱动运行中")
            : QStringLiteral("启动外部数据回放驱动"));
    }

    setProgressValue(totalProgressBar_,
                     progress.value(QStringLiteral("total_progress_percent")).toDouble(0.0));

    const QJsonObject clock = progress.value(QStringLiteral("clock")).toObject();
    if (clockProgressVal_ && !clock.isEmpty()) {
        clockProgressVal_->setText(QStringLiteral("%1 / t=%2s / tick=%3 / dt=%4s")
            .arg(clock.value(QStringLiteral("source")).toString(QStringLiteral("replay")))
            .arg(clock.value(QStringLiteral("run_time_s")).toDouble(0.0), 0, 'f', 2)
            .arg(clock.value(QStringLiteral("tick_index")).toInt(0))
            .arg(clock.value(QStringLiteral("delta_t_s")).toDouble(0.0), 0, 'f', 2));
    }

    const QJsonObject initialization = progress.value(QStringLiteral("initialization")).toObject();
    if (initializationStatusVal_ && !initialization.isEmpty()) {
        initializationStatusVal_->setText(QStringLiteral("%1 / workflow=%2 / preflight=%3")
            .arg(initialization.value(QStringLiteral("status")).toString(QStringLiteral("pending")))
            .arg(initialization.value(QStringLiteral("workflows")).toArray().size())
            .arg(initialization.value(QStringLiteral("preflight_runs")).toArray().size()));
    }
    if (initTable_ && !initialization.isEmpty()) {
        initTable_->setRowCount(0);
        const QJsonArray preflightRuns = initialization.value(QStringLiteral("preflight_runs")).toArray();
        const QJsonArray workflows = initialization.value(QStringLiteral("workflows")).toArray();
        const QJsonArray rows = preflightRuns.isEmpty() ? workflows : preflightRuns;
        for (const QJsonValue& value : rows) {
            const QJsonObject rowObj = value.toObject();
            const QJsonObject summary = rowObj.value(QStringLiteral("summary")).toObject();
            const int row = initTable_->rowCount();
            initTable_->insertRow(row);
            initTable_->setItem(row, 0, new QTableWidgetItem(
                rowObj.value(QStringLiteral("workflow_id")).toString(QStringLiteral("-"))));
            initTable_->setItem(row, 1, new QTableWidgetItem(QString::number(
                summary.value(QStringLiteral("prepared_node_count"))
                    .toInt(rowObj.value(QStringLiteral("operator_count")).toInt()))));
            initTable_->setItem(row, 2, new QTableWidgetItem(QStringLiteral("%1/%2")
                .arg(rowObj.value(QStringLiteral("model_count")).toInt(summary.value(QStringLiteral("adapter_event_count")).toInt()))
                .arg(rowObj.value(QStringLiteral("resource_lock_count")).toInt())));
            initTable_->setItem(row, 3, new QTableWidgetItem(
                rowObj.value(QStringLiteral("status")).toString(QStringLiteral("-"))));
        }
    }

    const QJsonObject online = progress.value(QStringLiteral("online")).toObject();
    const int requestedFrames = online.value(QStringLiteral("requested_frames")).toInt();
    const int effectiveFrames = online.value(QStringLiteral("effective_frames")).toInt(requestedFrames);
    const int completedFrames = online.value(QStringLiteral("completed_frames")).toInt();
    const double onlinePercent = online.value(QStringLiteral("progress_percent")).toDouble(
        effectiveFrames > 0 ? (100.0 * completedFrames / effectiveFrames) : 0.0);
    if (onlineProgressBar_) {
        onlineProgressBar_->setFormat(QStringLiteral("在线有效帧 %1/%2 (%p%)")
            .arg(completedFrames)
            .arg(effectiveFrames));
    }
    setProgressValue(onlineProgressBar_, onlinePercent);

    const QJsonObject prediction = progress.value(QStringLiteral("prediction")).toObject();
    const int requestedRuns = prediction.value(QStringLiteral("requested_runs")).toInt();
    const int completedRuns = prediction.value(QStringLiteral("completed_runs")).toInt();
    const double predictionPercent = prediction.value(QStringLiteral("progress_percent")).toDouble(
        requestedRuns > 0 ? (100.0 * completedRuns / requestedRuns) : 0.0);
    if (predictionProgressBar_) {
        predictionProgressBar_->setFormat(QStringLiteral("预测分支 %1/%2 (%p%)")
            .arg(completedRuns)
            .arg(requestedRuns));
    }
    setProgressValue(predictionProgressBar_, predictionPercent);
}

void OnlinePage::onRunStatus(const QString& status, const QString& message)
{
    if (runMessageVal_) {
        runMessageVal_->setText(QStringLiteral("%1 / %2")
                                    .arg(statusText(status),
                                         message.isEmpty() ? QStringLiteral("-") : message));
    }
}

void OnlinePage::onRunLog(const QString& line)
{
    if (runLogVal_) {
        runLogVal_->setText(line);
    }
}

void OnlinePage::onTimeline(const QJsonObject& timeline)
{
    latestTimeline_ = timeline;
    const QJsonObject progress = timeline.value(QStringLiteral("mainline_progress")).toObject();
    if (!progress.isEmpty()) {
        onRunProgress(progress);
    }

    const bool followLive = timeline.value(QStringLiteral("live")).toBool(false);
    applyBranchSnapshot(timeline, followLive);
    updateOnlineSummary(timeline);
    updateFilterSummary(timeline);
    updateRuntimeSummary(timeline);
}

void OnlinePage::applyBranchSnapshot(const QJsonObject& timeline, const bool followLive)
{
    if (currentBranchId_.isEmpty()) {
        currentBranchId_ = primaryBranchId(timeline);
    }
    if (followLive && currentBranchId_ == primaryBranchId(timeline)) {
        currentLoopIterationIndex_ = latestLoopForBranch(timeline, currentBranchId_);
    } else if (currentLoopIterationIndex_ < 0) {
        currentLoopIterationIndex_ = latestLoopForBranch(timeline, currentBranchId_);
    }

    branchTree_->setSnapshot(timeline);
    branchTree_->setCurrentBranch(currentBranchId_);
    branchTimeline_->setSnapshot(timeline);
    branchTimeline_->setCurrentTimelinePoint(currentBranchId_, currentLoopIterationIndex_);
    branchState_->setSnapshot(timeline);
    branchState_->setCurrentTimelinePoint(currentBranchId_, currentLoopIterationIndex_);
    branchField_->setSnapshot(timeline);
    branchField_->setCurrentTimelinePoint(currentBranchId_, currentLoopIterationIndex_);
    branchSeries_->setSnapshot(timeline);
    branchSeries_->setCurrentBranch(currentBranchId_);
    syncScrubber();
}

void OnlinePage::selectBranch(const QString& branchId)
{
    if (branchId.isEmpty()) {
        return;
    }
    currentBranchId_ = branchId;
    currentLoopIterationIndex_ = latestLoopForBranch(latestTimeline_, currentBranchId_);
    branchTimeline_->setCurrentBranch(currentBranchId_);
    branchTimeline_->setCurrentTimelinePoint(currentBranchId_, currentLoopIterationIndex_);
    branchState_->setCurrentTimelinePoint(currentBranchId_, currentLoopIterationIndex_);
    branchField_->setCurrentTimelinePoint(currentBranchId_, currentLoopIterationIndex_);
    branchSeries_->setCurrentBranch(currentBranchId_);
    syncScrubber();
}

void OnlinePage::selectTimelinePoint(const QString& branchId, const int loopIterationIndex)
{
    currentBranchId_ = branchId;
    currentLoopIterationIndex_ = loopIterationIndex;
    branchTree_->setCurrentBranch(currentBranchId_);
    branchTimeline_->setCurrentTimelinePoint(currentBranchId_, currentLoopIterationIndex_);
    branchState_->setCurrentTimelinePoint(currentBranchId_, currentLoopIterationIndex_);
    branchField_->setCurrentTimelinePoint(currentBranchId_, currentLoopIterationIndex_);
    branchSeries_->setCurrentBranch(currentBranchId_);
    syncScrubber();
}

void OnlinePage::onScrubberMoved(const int sliderValue)
{
    if (sliderValue < 0 || sliderValue >= scrubberLoops_.size()) {
        return;
    }
    const int loop = scrubberLoops_.at(sliderValue);
    if (loop == currentLoopIterationIndex_) {
        return;
    }
    // 直接更新各面板（不经 selectTimelinePoint，避免 syncScrubber 重入回写滑块）。
    currentLoopIterationIndex_ = loop;
    branchTimeline_->setCurrentTimelinePoint(currentBranchId_, loop);
    branchState_->setCurrentTimelinePoint(currentBranchId_, loop);
    branchField_->setCurrentTimelinePoint(currentBranchId_, loop);
    updateScrubberLabel(loop);
}

void OnlinePage::syncScrubber()
{
    if (!timeScrubber_) {
        return;
    }
    scrubberLoops_ = loopIndicesForBranch(latestTimeline_, currentBranchId_);
    const QSignalBlocker blocker(timeScrubber_);
    if (scrubberLoops_.isEmpty()) {
        timeScrubber_->setEnabled(false);
        timeScrubber_->setRange(0, 0);
        scrubberLabel_->setText(QStringLiteral("—"));
        return;
    }
    timeScrubber_->setEnabled(true);
    timeScrubber_->setRange(0, scrubberLoops_.size() - 1);
    int idx = static_cast<int>(scrubberLoops_.indexOf(currentLoopIterationIndex_));
    if (idx < 0) {
        idx = scrubberLoops_.size() - 1;
    }
    timeScrubber_->setValue(idx);
    updateScrubberLabel(scrubberLoops_.at(idx));
}

void OnlinePage::updateScrubberLabel(const int loopIterationIndex)
{
    if (!scrubberLabel_) {
        return;
    }
    const int idx = static_cast<int>(scrubberLoops_.indexOf(loopIterationIndex));
    const int total = scrubberLoops_.size();
    // 从该帧取 t/h 展示
    double tSeconds = 0.0;
    double altitude = 0.0;
    bool haveT = false;
    bool haveH = false;
    for (const QJsonValue& value : timelinePoints(latestTimeline_)) {
        const QJsonObject point = value.toObject();
        if (point.value(QStringLiteral("branch_id")).toString(QStringLiteral("main.online")) != currentBranchId_) {
            continue;
        }
        const int loop = point.value(QStringLiteral("loop_iteration_index"))
                             .toInt(point.value(QStringLiteral("frame_index")).toInt(-1));
        if (loop != loopIterationIndex) {
            continue;
        }
        const QJsonObject state = point.value(QStringLiteral("selected_state")).toObject();
        if (point.contains(QStringLiteral("sample_time_s"))) {
            tSeconds = point.value(QStringLiteral("sample_time_s")).toDouble();
            haveT = true;
        } else if (state.contains(QStringLiteral("time_s"))) {
            tSeconds = state.value(QStringLiteral("time_s")).toDouble();
            haveT = true;
        }
        if (point.contains(QStringLiteral("altitude_m"))) {
            altitude = point.value(QStringLiteral("altitude_m")).toDouble();
            haveH = true;
        } else if (state.contains(QStringLiteral("h"))) {
            altitude = state.value(QStringLiteral("h")).toDouble();
            haveH = true;
        }
        break;
    }
    QString text = QStringLiteral("帧 %1/%2 · loop %3").arg(idx + 1).arg(total).arg(loopIterationIndex);
    if (haveT) {
        text += QStringLiteral(" · t=%1s").arg(tSeconds, 0, 'f', 1);
    }
    if (haveH) {
        text += QStringLiteral(" · h=%1m").arg(altitude, 0, 'f', 0);
    }
    scrubberLabel_->setText(text);
}

void OnlinePage::updateOnlineSummary(const QJsonObject& timeline)
{
    const QJsonArray frames = timeline.value(QStringLiteral("online_frames")).toArray();
    const QString source = timeline.value(QStringLiteral("source_kind")).toString(
        timeline.value(QStringLiteral("mode")).toString(QStringLiteral("-")));
    if (dataSourceVal_) {
        dataSourceVal_->setText(source);
    }
    if (onlineFramesVal_) {
        onlineFramesVal_->setText(QStringLiteral("%1").arg(frames.size()));
    }

    const QJsonObject point = latestPointForBranch(timeline, primaryBranchId(timeline));
    if (!point.isEmpty()) {
        const double time = point.value(QStringLiteral("sample_time_s")).toDouble(
            point.value(QStringLiteral("source_time_s")).toDouble(0.0));
        const double h = numberFromPoint(point, QStringLiteral("altitude_m"), QStringLiteral("h"));
        const double ma = numberFromPoint(point, QStringLiteral("mach"), QStringLiteral("ma"));
        if (latestStateVal_) {
            latestStateVal_->setText(QStringLiteral("loop=%1 / h=%2m / Ma=%3")
                .arg(point.value(QStringLiteral("loop_iteration_index")).toInt())
                .arg(std::isfinite(h) ? QString::number(h, 'f', 0) : QStringLiteral("-"))
                .arg(std::isfinite(ma) ? QString::number(ma, 'f', 2) : QStringLiteral("-")));
        }
        if (freshnessVal_) {
            freshnessVal_->setText(QStringLiteral("%1 / t=%2s")
                .arg(timeline.value(QStringLiteral("view_mode")).toString(QStringLiteral("replay")))
                .arg(time, 0, 'f', 2));
        }
    }
}

void OnlinePage::updateFilterSummary(const QJsonObject& timeline)
{
    std::vector<double> essSeries;
    std::vector<double> residualSeries;
    int resampleCount = 0;
    QJsonObject lastFilter;
    for (const QJsonValue& value : timelinePoints(timeline)) {
        const QJsonObject point = value.toObject();
        if (point.value(QStringLiteral("branch_id")).toString(QStringLiteral("main.online")) != primaryBranchId(timeline)) {
            continue;
        }
        const QJsonObject filter = point.value(QStringLiteral("filter")).toObject();
        if (filter.isEmpty()) {
            continue;
        }
        essSeries.push_back(filter.value(QStringLiteral("effective_sample_size")).toDouble());
        residualSeries.push_back(filter.value(QStringLiteral("observation_residual_norm")).toDouble());
        if (filter.value(QStringLiteral("resampled")).toBool()) {
            ++resampleCount;
        }
        lastFilter = filter;
    }

    if (lastFilter.isEmpty()) {
        return;
    }
    essVal_->setText(valueText(lastFilter.value(QStringLiteral("effective_sample_size")), 3));
    particleVal_->setText(QString::number(lastFilter.value(QStringLiteral("particle_count")).toInt()));
    residualVal_->setText(valueText(lastFilter.value(QStringLiteral("observation_residual_norm")), 3));
    resampleVal_->setText(QString::number(resampleCount));
    essTrend_->setSamples(essSeries);
    residualTrend_->setSamples(residualSeries);
}

void OnlinePage::updateRuntimeSummary(const QJsonObject& timeline)
{
    const QJsonObject runtime = timeline.value(QStringLiteral("runtime")).toObject();
    if (runtimeVal_) {
        runtimeVal_->setText(QStringLiteral("迭代 %1 / 停止=%2")
            .arg(runtime.value(QStringLiteral("iteration_count")).toInt(
                timelinePoints(timeline).size()))
            .arg(stopReasonText(runtime.value(QStringLiteral("stop_reason")).toString())));
    }

    const QJsonObject scheduler = timeline.value(QStringLiteral("scheduler")).toObject();
    if (schedulerVal_) {
        schedulerVal_->setText(QStringLiteral("事件 %1 / worker %2 / 超期 %3")
            .arg(scheduler.value(QStringLiteral("event_count")).toInt())
            .arg(scheduler.value(QStringLiteral("worker_count")).toInt())
            .arg(scheduler.value(QStringLiteral("missed_deadline_count")).toInt()));
    }

    const int predictionCount = timeline.value(QStringLiteral("prediction_run_count")).toInt(
        timeline.value(QStringLiteral("prediction_runs")).toArray().size());
    if (predictionVal_) {
        predictionVal_->setText(QStringLiteral("%1 个预测分支").arg(predictionCount));
    }
}

} // namespace twin
