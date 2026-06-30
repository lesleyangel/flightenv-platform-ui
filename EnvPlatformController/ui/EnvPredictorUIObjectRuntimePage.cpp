#include "EnvPredictorUIInternal.h"

using namespace flightenv::platform_ui::internal;

void EnvPredictorUI::buildRuntimeChainPage_()
{
    if (pageRuntimeChain_) {
        return;
    }

    pageRuntimeChain_ = new QWidget(ui.tabWidget_main);
    auto* root = new QVBoxLayout(pageRuntimeChain_);
    root->setContentsMargins(4, 4, 4, 4);
    root->setSpacing(4);

    auto* summaryRow = new QWidget(pageRuntimeChain_);
    summaryRow->setMaximumHeight(86);
    auto* summaryLayout = new QHBoxLayout(summaryRow);
    summaryLayout->setContentsMargins(0, 0, 0, 0);
    summaryLayout->setSpacing(4);

    lbObjectSummary_ = new QLabel(summaryRow);
    lbObjectSummary_->setWordWrap(true);
    lbObjectSummary_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    lbObjectSummary_->setStyleSheet(QStringLiteral(
        "QLabel{padding:4px 8px;background:#eef6f6;border:1px solid #9cc9c9;border-radius:4px;font-weight:600;}"));
    summaryLayout->addWidget(lbObjectSummary_, 1);

    auto* objectActionBox = new QGroupBox(QStringLiteral("模型初始化"), summaryRow);
    objectActionBox->setMaximumWidth(190);
    objectActionBox->setMaximumHeight(82);
    auto* objectActionLayout = new QVBoxLayout(objectActionBox);
    objectActionLayout->setContentsMargins(6, 6, 6, 6);
    objectActionLayout->setSpacing(3);
    objectTrainButton_ = new QPushButton(QStringLiteral("初始化"), objectActionBox);
    objectTrainButton_->setMaximumHeight(30);
    objectTrainButton_->setToolTip(QStringLiteral("执行对象模型初始化或训练准备；运行控制仍在模拟演示页的任务时间轴面板中完成"));
    objectActionLayout->addWidget(objectTrainButton_);
    summaryLayout->addWidget(objectActionBox);
    root->addWidget(summaryRow);
    connect(objectTrainButton_, &QPushButton::clicked, this, &EnvPredictorUI::on_trainBtn_clicked, Qt::UniqueConnection);

    lbObjectFlow_ = new QLabel(pageRuntimeChain_);
    lbObjectFlow_->setWordWrap(true);
    lbObjectFlow_->setStyleSheet(QStringLiteral(
        "QLabel{padding:8px;background:#ffffff;border:1px solid #d1d5db;border-radius:4px;}"));
    root->addWidget(lbObjectFlow_);

    auto* scroll = new QScrollArea(pageRuntimeChain_);
    scroll->setWidgetResizable(true);
    auto* content = new QWidget(scroll);
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(8);
    scroll->setWidget(content);
    root->addWidget(scroll, 1);

    auto* topSplitter = new QSplitter(Qt::Horizontal, content);
    tblObjectBasic_ = makeGraphTable(topSplitter, {
        QStringLiteral("项目"), QStringLiteral("内容"), QStringLiteral("来源")
    });
    tblObjectBasic_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    tblObjectBasic_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    tblObjectBasic_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    tblObjectMeshes_ = makeGraphTable(topSplitter, {
        QStringLiteral("网格"), QStringLiteral("部件"), QStringLiteral("文件"), QStringLiteral("说明")
    });
    tblObjectMeshes_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    tblObjectMeshes_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    tblObjectMeshes_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    tblObjectMeshes_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    topSplitter->addWidget(tblObjectBasic_);
    topSplitter->addWidget(tblObjectMeshes_);
    topSplitter->setStretchFactor(0, 1);
    topSplitter->setStretchFactor(1, 2);
    contentLayout->addWidget(topSplitter);

    auto* sensorGroup = new QGroupBox(QStringLiteral("传感器布置"), content);
    auto* sensorLayout = new QHBoxLayout(sensorGroup);
    sensorLayout->setContentsMargins(8, 8, 8, 8);
    sensorLayout->setSpacing(8);
    auto* sensorLeftPanel = new QWidget(sensorGroup);
    auto* sensorLeftLayout = new QVBoxLayout(sensorLeftPanel);
    sensorLeftLayout->setContentsMargins(0, 0, 0, 0);
    sensorLeftLayout->setSpacing(6);
    auto* meshRow = new QHBoxLayout();
    meshRow->setSpacing(6);
    meshRow->addWidget(new QLabel(QStringLiteral("显示网格"), sensorLeftPanel));
    objectMeshCombo_ = new QComboBox(sensorLeftPanel);
    objectMeshCombo_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    objectMeshCombo_->setToolTip(QStringLiteral("选择传感器位置三维图里显示的一套对象网格，避免多套网格叠加"));
    meshRow->addWidget(objectMeshCombo_, 1);
    sensorLeftLayout->addLayout(meshRow);

    auto* sensorLayoutRow = new QHBoxLayout();
    sensorLayoutRow->setSpacing(6);
    sensorLayoutRow->addWidget(new QLabel(QStringLiteral("传感器组"), sensorLeftPanel));
    objectSensorLayoutCombo_ = new QComboBox(sensorLeftPanel);
    objectSensorLayoutCombo_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    objectSensorLayoutCombo_->setToolTip(QStringLiteral("按数据库 fieldnodeset / 对象包 sensor layout 选择一组真实传感器点位显示"));
    sensorLayoutRow->addWidget(objectSensorLayoutCombo_, 1);
    sensorLeftLayout->addLayout(sensorLayoutRow);

    objectSensorVtkHost_ = new QWidget(sensorGroup);
    objectSensorVtkHost_->setMinimumHeight(300);
    objectSensorVtkHost_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* objectSensorHostLayout = new QVBoxLayout(objectSensorVtkHost_);
    objectSensorHostLayout->setContentsMargins(0, 0, 0, 0);
    auto* sensorPlaceholder = new QLabel(QStringLiteral("启动运行后显示传感器在对象上的三维位置"), objectSensorVtkHost_);
    sensorPlaceholder->setAlignment(Qt::AlignCenter);
    sensorPlaceholder->setStyleSheet(QStringLiteral("QLabel{color:#64748b;border:1px dashed #cbd5e1;}"));
    objectSensorHostLayout->addWidget(sensorPlaceholder);
    sensorLeftLayout->addWidget(objectSensorVtkHost_, 1);

    tblObjectSensors_ = makeGraphTable(sensorGroup, {
        QStringLiteral("传感器组"), QStringLiteral("部件/位置"), QStringLiteral("测点"),
        QStringLiteral("通道数"), QStringLiteral("单位"), QStringLiteral("数据前缀")
    });
    tblObjectSensors_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    tblObjectSensors_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    tblObjectSensors_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    tblObjectSensors_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    tblObjectSensors_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    tblObjectSensors_->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    sensorLayout->addWidget(sensorLeftPanel, 2);
    sensorLayout->addWidget(tblObjectSensors_, 3);
    contentLayout->addWidget(sensorGroup);

    auto* runConfigGroup = new QGroupBox(QStringLiteral("运行配置（点开始前可改）"), content);
    auto* runConfigLayout = new QGridLayout(runConfigGroup);
    runConfigLayout->setContentsMargins(8, 8, 8, 8);
    runConfigLayout->setHorizontalSpacing(10);
    runConfigLayout->setVerticalSpacing(6);

    auto makeSpin = [&](const int minValue, const int maxValue, const QString& tooltip) {
        auto* spin = new QSpinBox(runConfigGroup);
        spin->setRange(minValue, maxValue);
        spin->setToolTip(tooltip);
        return spin;
    };
    objectOnlineFramesSpin_ = makeSpin(1, 100000, QStringLiteral("在线主线回放总帧数"));
    objectPredictionEveryFramesSpin_ = makeSpin(1, 100000, QStringLiteral("每隔多少在线帧开启一次未来预测分支"));
    objectFutureMaxIterationsSpin_ = makeSpin(1, 100000, QStringLiteral("每个未来预测分支最多向前预测多少步"));
    objectBranchChunkIterationsSpin_ = makeSpin(1, 100000, QStringLiteral("每次调度分支推进的步数"));
    objectMaxConcurrentBranchesSpin_ = makeSpin(1, 1024, QStringLiteral("同时运行的未来预测分支上限"));
    objectReplayTimeScaleSpin_ = new QDoubleSpinBox(runConfigGroup);
    objectReplayTimeScaleSpin_->setRange(0.01, 1000.0);
    objectReplayTimeScaleSpin_->setDecimals(2);
    objectReplayTimeScaleSpin_->setSingleStep(0.25);
    objectReplayTimeScaleSpin_->setToolTip(QStringLiteral("平台时钟回放速度倍率，不改变输入数据自身时间步长"));
    objectSensorStreamEdit_ = new QLineEdit(runConfigGroup);
    objectSensorStreamEdit_->setToolTip(QStringLiteral("传感器外部回放流文件，点开始前应用到 runtime host"));
    objectTrajectoryInputEdit_ = new QLineEdit(runConfigGroup);
    objectTrajectoryInputEdit_->setToolTip(QStringLiteral("仅用于显示/选择弹道输入文件，本轮不写入运行参数"));

    auto addConfigRow = [&](const int row, const QString& label, QWidget* widget) {
        runConfigLayout->addWidget(new QLabel(label, runConfigGroup), row, 0);
        runConfigLayout->addWidget(widget, row, 1);
    };
    addConfigRow(0, QStringLiteral("在线帧数"), objectOnlineFramesSpin_);
    addConfigRow(1, QStringLiteral("分支触发间隔（帧）"), objectPredictionEveryFramesSpin_);
    addConfigRow(2, QStringLiteral("未来预测步数"), objectFutureMaxIterationsSpin_);
    addConfigRow(3, QStringLiteral("分支块步数"), objectBranchChunkIterationsSpin_);
    addConfigRow(4, QStringLiteral("最大并发分支"), objectMaxConcurrentBranchesSpin_);
    addConfigRow(5, QStringLiteral("回放速度倍率"), objectReplayTimeScaleSpin_);
    runConfigLayout->addWidget(new QLabel(QStringLiteral("传感器输入流"), runConfigGroup), 6, 0);
    auto* streamRow = new QWidget(runConfigGroup);
    auto* streamLayout = new QHBoxLayout(streamRow);
    streamLayout->setContentsMargins(0, 0, 0, 0);
    streamLayout->setSpacing(6);
    streamLayout->addWidget(objectSensorStreamEdit_, 1);
    auto* browseStreamButton = new QPushButton(QStringLiteral("选择"), streamRow);
    streamLayout->addWidget(browseStreamButton);
    runConfigLayout->addWidget(streamRow, 6, 1);
    runConfigLayout->addWidget(new QLabel(QStringLiteral("弹道输入文件"), runConfigGroup), 7, 0);
    auto* trajectoryRow = new QWidget(runConfigGroup);
    auto* trajectoryLayout = new QHBoxLayout(trajectoryRow);
    trajectoryLayout->setContentsMargins(0, 0, 0, 0);
    trajectoryLayout->setSpacing(6);
    trajectoryLayout->addWidget(objectTrajectoryInputEdit_, 1);
    auto* browseTrajectoryButton = new QPushButton(QStringLiteral("选择"), trajectoryRow);
    trajectoryLayout->addWidget(browseTrajectoryButton);
    runConfigLayout->addWidget(trajectoryRow, 7, 1);
    auto* applyRunConfigButton = new QPushButton(QStringLiteral("应用到下一次开始"), runConfigGroup);
    objectRunConfigHint_ = new QLabel(QStringLiteral("修改后点“应用”或直接点“开始”生效；运行中修改需要重新开始。"), runConfigGroup);
    objectRunConfigHint_->setWordWrap(true);
    objectRunConfigHint_->setStyleSheet(QStringLiteral("QLabel{color:#475569;}"));
    runConfigLayout->addWidget(objectRunConfigHint_, 8, 0, 1, 2);
    runConfigLayout->addWidget(applyRunConfigButton, 9, 1, Qt::AlignRight);
    runConfigLayout->setColumnStretch(1, 1);
    contentLayout->addWidget(runConfigGroup);

    connect(browseStreamButton, &QPushButton::clicked, this, [this]() {
        const QString startPath = objectSensorStreamEdit_ && !objectSensorStreamEdit_->text().isEmpty()
            ? QFileInfo(objectSensorStreamEdit_->text()).absolutePath()
            : objectPackagePath(QStringLiteral("fixtures"));
        const QString selected = QFileDialog::getOpenFileName(
            this,
            QStringLiteral("选择传感器输入流"),
            startPath,
            QStringLiteral("JSON 文件 (*.json);;所有文件 (*.*)"));
        if (!selected.isEmpty() && objectSensorStreamEdit_) {
            objectSensorStreamEdit_->setText(QDir::toNativeSeparators(selected));
        }
    });
    connect(browseTrajectoryButton, &QPushButton::clicked, this, [this]() {
        const QString startPath = objectTrajectoryInputEdit_ && !objectTrajectoryInputEdit_->text().isEmpty()
            ? QFileInfo(objectTrajectoryInputEdit_->text()).absolutePath()
            : objectPackagePath(QStringLiteral("fixtures"));
        const QString selected = QFileDialog::getOpenFileName(
            this,
            QStringLiteral("选择弹道输入文件"),
            startPath,
            QStringLiteral("弹道/JSON/文本文件 (*.json *.txt *.csv);;所有文件 (*.*)"));
        if (!selected.isEmpty() && objectTrajectoryInputEdit_) {
            objectTrajectoryInputEdit_->setText(QDir::toNativeSeparators(selected));
        }
    });
    connect(applyRunConfigButton, &QPushButton::clicked, this, &EnvPredictorUI::applyPlatformRunConfigFromUi_);
    connect(objectMeshCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        rebuildObjectSensorView_();
    }, Qt::UniqueConnection);
    connect(objectSensorLayoutCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        rebuildObjectSensorView_();
    }, Qt::UniqueConnection);

    auto* bottomSplitter = new QSplitter(Qt::Horizontal, content);
    tblRuntimeChain_ = makeGraphTable(bottomSplitter, {
        QStringLiteral("阶段"), QStringLiteral("使用"), QStringLiteral("算子"),
        QStringLiteral("输出"), QStringLiteral("说明")
    });
    tblObjectRuntimeConfig_ = makeGraphTable(bottomSplitter, {
        QStringLiteral("配置"), QStringLiteral("当前值"), QStringLiteral("来源/说明")
    });
    bottomSplitter->addWidget(tblRuntimeChain_);
    bottomSplitter->addWidget(tblObjectRuntimeConfig_);
    bottomSplitter->setStretchFactor(0, 3);
    bottomSplitter->setStretchFactor(1, 2);
    contentLayout->addWidget(bottomSplitter);

    auto* refreshButton = new QPushButton(QString::fromUtf8("刷新"), pageRuntimeChain_);
    connect(refreshButton, &QPushButton::clicked, this, &EnvPredictorUI::refreshRuntimeChainPage_);
    root->addWidget(refreshButton, 0, Qt::AlignRight);

    ui.tabWidget_main->insertTab(0, pageRuntimeChain_, QString::fromUtf8("对象信息"));
    refreshRuntimeChainPage_();
}

void EnvPredictorUI::refreshRuntimeChainPage_()
{
    if (!pageRuntimeChain_) {
        return;
    }

    clearGraphTable(tblObjectBasic_);
    clearGraphTable(tblObjectMeshes_);
    clearGraphTable(tblObjectSensors_);
    clearGraphTable(tblRuntimeChain_);
    clearGraphTable(tblObjectRuntimeConfig_);

    const QJsonObject objectManifest = readJsonObject(objectPackagePath(QStringLiteral("object/twin_object.json")));
    const QJsonObject sensorLayoutsDoc = readJsonObject(objectPackagePath(QStringLiteral("assets/sensor_layouts.json")));
    const QJsonObject runProfile = readJsonObject(objectPackagePath(QStringLiteral("run_profiles/online_filtering_full.json")));
    const QJsonObject onlineWorkflow = readJsonObject(objectPackagePath(QStringLiteral("workflows/online_filtering_external_input.json")));

    const QJsonArray resources = objectManifest.value(QStringLiteral("resources")).toArray();
    auto resourceById = [&](const QString& resourceId) -> QJsonObject {
        for (const QJsonValue& value : resources) {
            const QJsonObject resource = value.toObject();
            if (resource.value(QStringLiteral("resource_id")).toString() == resourceId) {
                return resource;
            }
        }
        return {};
    };

    int meshCount = 0;
    int sensorGroupCount = 0;
    int declaredSensorPoints = 0;
    for (const QJsonValue& value : resources) {
        const QJsonObject resource = value.toObject();
        const QString type = resource.value(QStringLiteral("resource_type")).toString();
        if (type == QStringLiteral("mesh")) {
            ++meshCount;
        }
        if (type == QStringLiteral("sensor_layout")) {
            ++sensorGroupCount;
            declaredSensorPoints += resource.value(QStringLiteral("node_count")).toInt();
        }
    }

    QMap<QString, int> streamChannelCounts;
    const auto workspaceRoot = workspaceRootFromEnvironment();
    const auto configuredObjectRoot = environmentPath("FLIGHTENV_PLATFORM_OBJECT_ROOT").empty()
        ? (workspaceRoot / "flightenv-object-reentry-vehicle")
        : environmentPath("FLIGHTENV_PLATFORM_OBJECT_ROOT");
    const PlatformUiRunConfig uiRunConfig = loadPlatformUiRunConfig(workspaceRoot, configuredObjectRoot);
    const QString sensorStreamPath = environmentPath("FLIGHTENV_PLATFORM_EXTERNAL_OBSERVATION_STREAM").empty()
        ? platformPathText(uiRunConfig.external_observation_stream)
        : platformPathText(environmentPath("FLIGHTENV_PLATFORM_EXTERNAL_OBSERVATION_STREAM"));
    const QJsonObject streamDoc = readJsonObject(sensorStreamPath);
    const QJsonArray frames = streamDoc.value(QStringLiteral("frames")).toArray();
    if (!frames.isEmpty()) {
        const QJsonObject firstFrame = frames.first().toObject();
        QJsonArray sensorIds = firstFrame.value(QStringLiteral("sensor_ids")).toArray();
        if (sensorIds.isEmpty()) {
            sensorIds = firstFrame.value(QStringLiteral("frame")).toObject()
                .value(QStringLiteral("frame")).toObject()
                .value(QStringLiteral("sensor_ids")).toArray();
        }
        for (const QJsonValue& idValue : sensorIds) {
            const QString id = idValue.toString();
            const int suffix = id.lastIndexOf(QLatin1Char('_'));
            const QString prefix = suffix > 0 ? id.left(suffix) : id;
            streamChannelCounts[prefix] += 1;
        }
    }
    int channelCount = 0;
    for (auto it = streamChannelCounts.cbegin(); it != streamChannelCounts.cend(); ++it) {
        channelCount += it.value();
    }

    const QString objectName = objectManifest.value(QStringLiteral("display_name")).toString(QStringLiteral("再入飞行器"));
    const QString objectId = objectManifest.value(QStringLiteral("object_id")).toString(QStringLiteral("reentry_vehicle"));
    const QJsonArray components = objectManifest.value(QStringLiteral("components")).toArray();
    const QJsonArray workflows = objectManifest.value(QStringLiteral("workflows")).toArray();

    if (lbObjectSummary_) {
        lbObjectSummary_->setText(QStringLiteral(
            "对象：%1（%2）；部件=%3，网格=%4，传感器组=%5，传感器物理点=%6，当前数据通道=%7。对象已默认加载，无需选择。")
            .arg(objectName, objectId)
            .arg(components.size())
            .arg(meshCount)
            .arg(sensorGroupCount)
            .arg(declaredSensorPoints)
            .arg(channelCount));
    }
    if (lbObjectFlow_) {
        lbObjectFlow_->setText(QStringLiteral(
            "运行流程：弹道/传感器回放 -> 状态转移 -> 观测方程 -> 粒子滤波融合 -> 多场重构 -> 损伤/烧蚀累积 -> 失效与寿命评估 -> 按帧触发未来预测分支。"));
    }

    appendGraphRow(tblObjectBasic_, {QStringLiteral("对象"), objectName, QStringLiteral("object/twin_object.json")});
    appendGraphRow(tblObjectBasic_, {QStringLiteral("对象ID"), objectId, QStringLiteral("object_id")});
    appendGraphRow(tblObjectBasic_, {QStringLiteral("对象类型"), objectManifest.value(QStringLiteral("object_type")).toString(), QStringLiteral("object_type")});
    appendGraphRow(tblObjectBasic_, {QStringLiteral("部件数量"), QString::number(components.size()), QStringLiteral("components")});
    appendGraphRow(tblObjectBasic_, {QStringLiteral("工作流数量"), QString::number(workflows.size()), QStringLiteral("workflows")});
    appendGraphRow(tblObjectBasic_, {QStringLiteral("默认在线流程"), runProfile.value(QStringLiteral("workflow_id")).toString(), QStringLiteral("online_filtering_full")});

    for (const QJsonValue& value : resources) {
        const QJsonObject resource = value.toObject();
        if (resource.value(QStringLiteral("resource_type")).toString() != QStringLiteral("mesh")) {
            continue;
        }
        appendGraphRow(tblObjectMeshes_, {
            resource.value(QStringLiteral("display_name")).toString(resource.value(QStringLiteral("resource_id")).toString()),
            resource.value(QStringLiteral("component_id")).toString(QStringLiteral("-")),
            objectResourceDisplayPath(resource.value(QStringLiteral("path")).toString()),
            resource.value(QStringLiteral("layout_role")).toString(resource.value(QStringLiteral("resource_id")).toString())
        });
    }

    const QJsonArray layoutEntries = sensorLayoutsDoc.value(QStringLiteral("layouts")).toArray();
    auto layoutById = [&](const QString& resourceId) -> QJsonObject {
        for (const QJsonValue& value : layoutEntries) {
            const QJsonObject layout = value.toObject();
            if (layout.value(QStringLiteral("resource_id")).toString() == resourceId) {
                return layout;
            }
        }
        return {};
    };

    for (const QJsonValue& value : resources) {
        const QJsonObject resource = value.toObject();
        if (resource.value(QStringLiteral("resource_type")).toString() != QStringLiteral("sensor_layout")) {
            continue;
        }
        const QString resourceId = resource.value(QStringLiteral("resource_id")).toString();
        const QJsonObject layout = layoutById(resourceId);
        const QString prefix = resource.value(QStringLiteral("channel_prefix")).toString(
            layout.value(QStringLiteral("channel_prefix")).toString());
        int channels = streamChannelCounts.value(prefix, 0);
        if (channels == 0 && resourceId == QStringLiteral("sensor.ballistic.virtual")) {
            channels = streamChannelCounts.value(QStringLiteral("ballistic_actual"), 5);
        }
        if (channels == 0) {
            channels = resource.value(QStringLiteral("node_count")).toInt() *
                std::max(1, resource.value(QStringLiteral("value_dim")).toInt(1));
        }
        appendGraphRow(tblObjectSensors_, {
            resource.value(QStringLiteral("display_name")).toString(resourceId),
            resource.value(QStringLiteral("component_id")).toString(QStringLiteral("虚拟/对象级")),
            jsonNumberText(resource, QStringLiteral("node_count"),
                jsonNumberText(layout, QStringLiteral("node_count"), QStringLiteral("-"))),
            QString::number(channels),
            layout.value(QStringLiteral("unit")).toString(QStringLiteral("-")),
            prefix.isEmpty() ? QStringLiteral("-") : prefix
        });
    }

    const QJsonObject trajectoryDataset = resourceById(QStringLiteral("dataset.trajectory.replay.primary"));
    const QString trajectoryPath = objectResourcePath(trajectoryDataset.value(QStringLiteral("uri")).toString());
    const QString databasePath = objectResourcePath(trajectoryDataset.value(QStringLiteral("source_database_path")).toString());

    QString progressPath;
    if (!platform_snapshot_.run_dir.empty()) {
        progressPath = platformPathText(platform_snapshot_.run_dir / "mainline_progress.json");
    }
    if (progressPath.isEmpty() || !QFileInfo::exists(progressPath)) {
        progressPath = findNewestSummary(QStringLiteral("_local_artifacts/platform-runtime/mainline-runs"),
            QStringLiteral("mainline_progress.json"));
    }
    const QJsonObject progress = progressPath.isEmpty() ? QJsonObject{} : readJsonObject(progressPath);
    const QJsonObject clock = progress.value(QStringLiteral("clock")).toObject();

    if (objectOnlineFramesSpin_) {
        QSignalBlocker b0(objectOnlineFramesSpin_);
        QSignalBlocker b1(objectPredictionEveryFramesSpin_);
        QSignalBlocker b2(objectFutureMaxIterationsSpin_);
        QSignalBlocker b3(objectBranchChunkIterationsSpin_);
        QSignalBlocker b4(objectMaxConcurrentBranchesSpin_);
        QSignalBlocker b5(objectReplayTimeScaleSpin_);
        objectOnlineFramesSpin_->setValue(uiRunConfig.online_frames);
        objectPredictionEveryFramesSpin_->setValue(uiRunConfig.prediction_every_frames);
        objectFutureMaxIterationsSpin_->setValue(uiRunConfig.future_max_iterations);
        objectBranchChunkIterationsSpin_->setValue(uiRunConfig.branch_chunk_iterations);
        objectMaxConcurrentBranchesSpin_->setValue(uiRunConfig.max_concurrent_branches);
        objectReplayTimeScaleSpin_->setValue(uiRunConfig.replay_time_scale);
        if (objectSensorStreamEdit_) {
            objectSensorStreamEdit_->setText(QDir::toNativeSeparators(sensorStreamPath));
        }
        if (objectTrajectoryInputEdit_ && objectTrajectoryInputEdit_->text().trimmed().isEmpty()) {
            objectTrajectoryInputEdit_->setText(objectResourceDisplayPath(trajectoryDataset.value(QStringLiteral("uri")).toString()));
        }
    }

    appendGraphRow(tblObjectRuntimeConfig_, {
        QStringLiteral("平台时间步长"),
        clock.contains(QStringLiteral("delta_t_s"))
            ? QStringLiteral("%1 s").arg(jsonNumberText(clock, QStringLiteral("delta_t_s")))
            : QStringLiteral("由输入回放数据决定"),
        progressPath.isEmpty() ? QStringLiteral("对象默认") : progressPath
    });
    appendGraphRow(tblObjectRuntimeConfig_, {
        QStringLiteral("在线回放帧数"),
        QString::number(uiRunConfig.online_frames),
        QStringLiteral("_local_artifacts/platform-ui/env_platform_controller_run_config.json")
    });
    appendGraphRow(tblObjectRuntimeConfig_, {
        QStringLiteral("预测分支触发"),
        QStringLiteral("每 %1 帧开启一个未来预测分支").arg(uiRunConfig.prediction_every_frames),
        QStringLiteral("runtime host 参数：--prediction-every-frames")
    });
    appendGraphRow(tblObjectRuntimeConfig_, {
        QStringLiteral("未来预测长度"),
        QStringLiteral("%1 步").arg(uiRunConfig.future_max_iterations),
        QStringLiteral("runtime host 参数：--future-max-iterations")
    });
    appendGraphRow(tblObjectRuntimeConfig_, {
        QStringLiteral("最大并发分支"),
        QString::number(uiRunConfig.max_concurrent_branches),
        QStringLiteral("runtime host 参数：--max-concurrent-branches")
    });
    appendGraphRow(tblObjectRuntimeConfig_, {
        QStringLiteral("回放速度倍率"),
        QString::number(uiRunConfig.replay_time_scale, 'f', 2),
        QStringLiteral("runtime host 参数：--replay-time-scale")
    });
    appendGraphRow(tblObjectRuntimeConfig_, {
        QStringLiteral("传感器/弹道输入流"),
        sensorStreamPath,
        QStringLiteral("runtime_launch.external_observation_stream")
    });
    appendGraphRow(tblObjectRuntimeConfig_, {
        QStringLiteral("弹道回放文件"),
        trajectoryPath,
        QStringLiteral("dataset.trajectory.replay.primary")
    });
    appendGraphRow(tblObjectRuntimeConfig_, {
        QStringLiteral("原始数据库"),
        databasePath,
        QStringLiteral("source_database_path")
    });

    const auto appendOperatorNode = [&](const QString& stageLabel,
                                        const QJsonObject& stage,
                                        const QJsonObject& node) {
        if (!tblRuntimeChain_) {
            return;
        }
        const int row = tblRuntimeChain_->rowCount();
        tblRuntimeChain_->insertRow(row);
        const QString nodeId = node.value(QStringLiteral("node_id")).toString();
        const QString operatorRef = node.value(QStringLiteral("operator_ref")).toString();
        const QJsonObject activation = node.value(QStringLiteral("activation_policy")).toObject();
        const bool enabled = activation.value(QStringLiteral("enabled")).toBool(true);
        const bool required = activation.value(QStringLiteral("required")).toBool(false);
        const bool disabledByConfig = containsString(uiRunConfig.disabled_operator_refs, operatorRef);
        setTableText(tblRuntimeChain_, row, 0, stageLabel);
        auto* check = new QCheckBox(tblRuntimeChain_);
        check->setProperty("platform_operator_ref", operatorRef);
        check->setProperty("platform_node_id", nodeId);
        check->setProperty("platform_feature", activation.value(QStringLiteral("feature")).toString());
        check->setProperty("platform_required", required);
        check->setChecked(enabled && !disabledByConfig);
        check->setEnabled(!required);
        check->setToolTip(required
            ? QStringLiteral("必选算子，当前对象运行必须启用")
            : QStringLiteral("对象方案选择：取消勾选后，下一次训练/开始会重新编译 workflow 并移除此算子"));
        tblRuntimeChain_->setCellWidget(row, 1, check);
        connect(check, &QCheckBox::toggled, this, [this]() {
            platformRunConfigApplyOk_ = false;
            if (objectRunConfigHint_) {
                objectRunConfigHint_->setText(QStringLiteral("算子选择已修改；点初始化/开始前会自动应用，也可以先点应用运行设置。"));
            }
        });
        setTableText(tblRuntimeChain_, row, 2, operatorDisplayName(operatorRef, nodeId));

        QStringList outputs;
        const QJsonArray stageOutputs = stage.value(QStringLiteral("subgraph")).toObject()
            .value(QStringLiteral("stage_outputs")).toArray();
        const QString prefix = nodeId + QStringLiteral(".");
        for (const QJsonValue& outputValue : stageOutputs) {
            const QString output = outputValue.toString();
            if (output.startsWith(prefix)) {
                outputs << output.mid(prefix.size());
            }
        }
        setTableText(tblRuntimeChain_, row, 3, outputs.isEmpty() ? QStringLiteral("-") : outputs.join(QStringLiteral(", ")));
        setTableText(tblRuntimeChain_, row, 4,
            activation.value(QStringLiteral("feature")).toString().isEmpty()
                ? operatorRef
                : QStringLiteral("功能开关：%1；算子ID：%2")
                    .arg(activation.value(QStringLiteral("feature")).toString(), operatorRef));
    };

    const QJsonArray phases = onlineWorkflow.value(QStringLiteral("phases")).toArray();
    for (const QJsonValue& phaseValue : phases) {
        const QJsonObject phase = phaseValue.toObject();
        const QJsonArray stages = phase.value(QStringLiteral("stages")).toArray();
        for (const QJsonValue& stageValue : stages) {
            const QJsonObject stage = stageValue.toObject();
            const QString stageLabel = workflowStageLabel(stage.value(QStringLiteral("stage_id")).toString());
            const QJsonArray nodes = stage.value(QStringLiteral("subgraph")).toObject()
                .value(QStringLiteral("nodes")).toArray();
            for (const QJsonValue& nodeValue : nodes) {
                appendOperatorNode(stageLabel, stage, nodeValue.toObject());
            }
        }
    }

    if (objectMeshCombo_) {
        bool previousMeshOk = false;
        const int previousMesh = objectMeshCombo_->currentData().toInt(&previousMeshOk);
        const QSignalBlocker comboBlocker(objectMeshCombo_);
        objectMeshCombo_->clear();
        if (runtime_view_ && !runtime_view_->meshes.empty()) {
            for (std::size_t i = 0; i < runtime_view_->meshes.size(); ++i) {
                const auto& mesh = runtime_view_->meshes[i];
                QString name = QString::fromStdString(mesh.name);
                if (name.isEmpty()) {
                    name = QFileInfo(QString::fromStdString(mesh.path)).baseName();
                }
                objectMeshCombo_->addItem(
                    QStringLiteral("%1  (%2)").arg(name, QString::fromStdString(mesh.path)),
                    static_cast<int>(i));
            }
            const int oldIndex = previousMeshOk ? objectMeshCombo_->findData(previousMesh) : -1;
            objectMeshCombo_->setCurrentIndex(oldIndex >= 0 ? oldIndex : 0);
            objectMeshCombo_->setEnabled(true);
        } else {
            objectMeshCombo_->addItem(QStringLiteral("运行初始化后可选择网格"), -1);
            objectMeshCombo_->setEnabled(false);
        }
    }

    if (objectSensorLayoutCombo_) {
        const QString previousSensorLayout = objectSensorLayoutCombo_->currentData().toString();
        const QSignalBlocker comboBlocker(objectSensorLayoutCombo_);
        objectSensorLayoutCombo_->clear();
        loadPlatformSensorLayouts();
        if (!platformSensorLayouts_.empty()) {
            for (const auto& layout : platformSensorLayouts_) {
                QString name = layout.display_name.isEmpty() ? layout.resource_id : layout.display_name;
                if (name.isEmpty()) {
                    name = QStringLiteral("未命名传感器组");
                }
                objectSensorLayoutCombo_->addItem(
                    QStringLiteral("%1  (%2点)").arg(name).arg(static_cast<int>(layout.nodes.size())),
                    layout.resource_id);
            }
            const int oldIndex = previousSensorLayout.isEmpty()
                ? -1
                : objectSensorLayoutCombo_->findData(previousSensorLayout);
            objectSensorLayoutCombo_->setCurrentIndex(oldIndex >= 0 ? oldIndex : 0);
            objectSensorLayoutCombo_->setEnabled(true);
        } else {
            objectSensorLayoutCombo_->addItem(QStringLiteral("未读取到传感器组"), QString());
            objectSensorLayoutCombo_->setEnabled(false);
        }
    }
    rebuildObjectSensorView_();

    for (QTableWidget* table : {tblObjectBasic_, tblObjectMeshes_, tblObjectSensors_, tblRuntimeChain_, tblObjectRuntimeConfig_}) {
        if (table) {
            table->resizeRowsToContents();
        }
    }

}

void EnvPredictorUI::applyPlatformRunConfigFromUi_()
{
    platformRunConfigApplyOk_ = false;
    if (!objectOnlineFramesSpin_ || !objectPredictionEveryFramesSpin_ ||
        !objectFutureMaxIterationsSpin_ || !objectBranchChunkIterationsSpin_ ||
        !objectMaxConcurrentBranchesSpin_ || !objectReplayTimeScaleSpin_) {
        platformRunConfigApplyOk_ = true;
        return;
    }

    const auto workspaceRoot = workspaceRootFromEnvironment();
    const auto configPath = platformUiRunConfigPath(workspaceRoot);
    QDir().mkpath(QFileInfo(QString::fromStdString(configPath.string())).absolutePath());

    QJsonObject config;
    config.insert(QStringLiteral("schema_version"), QStringLiteral("flightenv.platform_ui.run_config.v1"));
    config.insert(QStringLiteral("online_frames"), objectOnlineFramesSpin_->value());
    config.insert(QStringLiteral("prediction_every_frames"), objectPredictionEveryFramesSpin_->value());
    config.insert(QStringLiteral("future_max_iterations"), objectFutureMaxIterationsSpin_->value());
    config.insert(QStringLiteral("branch_chunk_iterations"), objectBranchChunkIterationsSpin_->value());
    config.insert(QStringLiteral("max_concurrent_branches"), objectMaxConcurrentBranchesSpin_->value());
    config.insert(QStringLiteral("replay_time_scale"), objectReplayTimeScaleSpin_->value());
    if (objectSensorStreamEdit_) {
        config.insert(
            QStringLiteral("external_observation_stream"),
            QDir::fromNativeSeparators(objectSensorStreamEdit_->text().trimmed()));
    }
    QJsonArray disabledOperators;
    std::set<QString> seenDisabledOperators;
    if (tblRuntimeChain_) {
        for (int row = 0; row < tblRuntimeChain_->rowCount(); ++row) {
            auto* check = qobject_cast<QCheckBox*>(tblRuntimeChain_->cellWidget(row, 1));
            if (!check || check->property("platform_required").toBool()) {
                continue;
            }
            const QString operatorRef = check->property("platform_operator_ref").toString().trimmed();
            if (!operatorRef.isEmpty() && !check->isChecked() && seenDisabledOperators.insert(operatorRef).second) {
                disabledOperators.push_back(operatorRef);
            }
        }
    }
    config.insert(QStringLiteral("disabled_operator_refs"), disabledOperators);

    QFile file(QString::fromStdString(configPath.string()));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (objectRunConfigHint_) {
            objectRunConfigHint_->setText(QStringLiteral("运行配置保存失败：%1").arg(file.errorString()));
        }
        return;
    }
    file.write(QJsonDocument(config).toJson(QJsonDocument::Indented));
    file.close();

    try {
        if (auto platformBackend = std::dynamic_pointer_cast<launchsupport::PlatformControllerBackend>(controller_backend_)) {
            platformBackend->configure(platformBackendOptions(true));
        }
    } catch (const std::exception& e) {
        if (objectRunConfigHint_) {
            objectRunConfigHint_->setText(QStringLiteral("运行配置编译失败：%1").arg(QString::fromUtf8(e.what())));
        }
        platformUiLogError( "Platform UI run config compile failed: %s", e.what());
        return;
    }

    if (objectRunConfigHint_) {
        objectRunConfigHint_->setText(QStringLiteral(
            "已应用到下一次初始化/开始：在线%1帧，每%2帧开分支，未来%3步，并发%4，回放倍率%5，禁用算子%6个。")
            .arg(objectOnlineFramesSpin_->value())
            .arg(objectPredictionEveryFramesSpin_->value())
            .arg(objectFutureMaxIterationsSpin_->value())
            .arg(objectMaxConcurrentBranchesSpin_->value())
            .arg(objectReplayTimeScaleSpin_->value(), 0, 'f', 2)
            .arg(disabledOperators.size()));
    }
    platformRunConfigApplyOk_ = true;
}

void EnvPredictorUI::rebuildObjectSensorView_()
{
    if (!objectSensorVtkHost_) {
        return;
    }

    auto* layout = objectSensorVtkHost_->layout();
    if (!layout) {
        layout = new QVBoxLayout(objectSensorVtkHost_);
        layout->setContentsMargins(0, 0, 0, 0);
        objectSensorVtkHost_->setLayout(layout);
    }

    auto clearHost = [&]() {
        QLayoutItem* item = nullptr;
        while ((item = layout->takeAt(0)) != nullptr) {
            if (item->widget()) {
                item->widget()->deleteLater();
            }
            delete item;
        }
        objectSensorVtk_ = nullptr;
    };

    if (!runtime_view_ || runtime_view_->meshes.empty()) {
        return;
    }

    loadPlatformSensorLayouts();
    int selectedLayoutIndex = 0;
    if (objectSensorLayoutCombo_ && objectSensorLayoutCombo_->currentIndex() >= 0) {
        const QString selectedResourceId = objectSensorLayoutCombo_->currentData().toString();
        const auto it = std::find_if(
            platformSensorLayouts_.begin(),
            platformSensorLayouts_.end(),
            [&](const PlatformSensorLayoutView& layoutView) {
                return layoutView.resource_id == selectedResourceId;
            });
        if (it != platformSensorLayouts_.end()) {
            selectedLayoutIndex = static_cast<int>(std::distance(platformSensorLayouts_.begin(), it));
        }
    }
    if (selectedLayoutIndex < 0 || selectedLayoutIndex >= static_cast<int>(platformSensorLayouts_.size())) {
        selectedLayoutIndex = 0;
    }

    clearHost();
    if (platformSensorLayouts_.empty() || platformSensorLayouts_[selectedLayoutIndex].nodes.empty()) {
        auto* emptyLabel = new QLabel(QStringLiteral("对象包中未读取到传感器三维坐标"), objectSensorVtkHost_);
        emptyLabel->setAlignment(Qt::AlignCenter);
        emptyLabel->setStyleSheet(QStringLiteral("QLabel{color:#64748b;border:1px dashed #cbd5e1;}"));
        layout->addWidget(emptyLabel);
        return;
    }

    int meshIndex = 0;
    if (objectMeshCombo_ && objectMeshCombo_->currentData().isValid()) {
        bool meshOk = false;
        const int selectedMesh = objectMeshCombo_->currentData().toInt(&meshOk);
        if (meshOk && selectedMesh >= 0 && selectedMesh < static_cast<int>(runtime_view_->meshes.size())) {
            meshIndex = selectedMesh;
        }
    }
    const auto& mesh = runtime_view_->meshes[meshIndex];
    const auto& sensorPoints = platformSensorLayouts_[selectedLayoutIndex].nodes;

    int fieldIndex = 2;
    if (fieldIndex < 0 || static_cast<std::size_t>(fieldIndex) >= runtime_view_->fields.size()) {
        fieldIndex = 0;
        if (!mesh.subject_indices.empty()) {
            fieldIndex = mesh.subject_indices.front();
        }
    }
    if (fieldIndex < 0 || static_cast<std::size_t>(fieldIndex) >= runtime_view_->fields.size()) {
        fieldIndex = 0;
    }

    QString meshName = QString::fromStdString(mesh.name);
    if (meshName.isEmpty()) {
        meshName = QFileInfo(QString::fromStdString(mesh.path)).baseName();
    }
    const QString meshPath = QDir(workspacePath(QStringLiteral("_deps/example")))
        .filePath(QString::fromStdString(mesh.path));

    objectSensorVtk_ = new VTKSingleDialog(
        meshName.toStdString(),
        QDir::toNativeSeparators(meshPath).toStdString(),
        objectSensorVtkHost_);
    objectSensorVtk_->setRuntimeView(runtime_view_);
    objectSensorVtk_->flg = fieldIndex;
    objectSensorVtk_->countFlg = 0;
    objectSensorVtk_->isModelLoaded = objectSensorVtk_->loadModelData();
    objectSensorVtk_->RemoveActor();
    objectSensorVtk_->setMarkerPoints(sensorPoints);
    layout->addWidget(objectSensorVtk_);
    vtkSizeTimer->start(500);
}
