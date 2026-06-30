#include "EnvPredictorUIInternal.h"

using namespace flightenv::platform_ui::internal;

void EnvPredictorUI::buildGraphWorkflowTabs_(QVBoxLayout* root)
{
    lbGraphWorkflowSummary_ = new QLabel(QString::fromUtf8("完整链路：等待 GraphRuntime evidence。"), pageGraphRuntime_);
    lbGraphWorkflowSummary_->setWordWrap(true);
    lbGraphWorkflowSummary_->setStyleSheet(QStringLiteral("QLabel{padding:6px;border:1px solid #d1d5db;border-radius:4px;background:#ffffff;}"));
    root->addWidget(lbGraphWorkflowSummary_);

    graphWorkflowTabs_ = new QTabWidget(pageGraphRuntime_);
    graphWorkflowTabs_->setMinimumHeight(520);

    auto addTablePage = [&](const QString& title, QLabel** summary, QTableWidget** table, const QStringList& headers) {
        auto* page = new QWidget(graphWorkflowTabs_);
        auto* layout = new QVBoxLayout(page);
        layout->setContentsMargins(6, 6, 6, 6);
        layout->setSpacing(6);
        if (summary) {
            *summary = new QLabel(QString::fromUtf8("等待数据"), page);
            (*summary)->setWordWrap(true);
            (*summary)->setStyleSheet(QStringLiteral("QLabel{padding:5px;background:#f8fafc;border:1px solid #e5e7eb;border-radius:4px;}"));
            layout->addWidget(*summary);
        }
        *table = makeGraphTable(page, headers);
        layout->addWidget(*table, 1);
        graphWorkflowTabs_->addTab(page, title);
    };

    {
        auto* page = new QWidget(graphWorkflowTabs_);
        auto* layout = new QVBoxLayout(page);
        layout->setContentsMargins(6, 6, 6, 6);
        layout->setSpacing(6);
        lbGraphObjectSummary_ = new QLabel(QString::fromUtf8("对象画像：等待 catalog 和 runtime evidence。"), page);
        lbGraphObjectSummary_->setWordWrap(true);
        lbGraphObjectSummary_->setStyleSheet(QStringLiteral("QLabel{padding:5px;background:#f8fafc;border:1px solid #e5e7eb;border-radius:4px;}"));
        layout->addWidget(lbGraphObjectSummary_);

        auto* vertical = new QSplitter(Qt::Vertical, page);
        auto* top = new QSplitter(Qt::Horizontal, vertical);
        graphObjectTable_ = makeGraphTable(top, {
            QString::fromUtf8("对象/部位"),
            QString::fromUtf8("几何/数据资产"),
            QString::fromUtf8("物理场/传感器"),
            QString::fromUtf8("模型匹配"),
            QString::fromUtf8("说明")
        });
        graphBindingTable_ = makeGraphTable(top, {
            QString::fromUtf8("对象/绑定"),
            QString::fromUtf8("模型类型/领域"),
            QString::fromUtf8("模型 ID"),
            QString::fromUtf8("阶段"),
            QString::fromUtf8("优先级/启用"),
            QString::fromUtf8("配置")
        });
        top->addWidget(graphObjectTable_);
        top->addWidget(graphBindingTable_);
        top->setStretchFactor(0, 2);
        top->setStretchFactor(1, 2);

        auto* bottom = new QSplitter(Qt::Horizontal, vertical);
        graphEquationTable_ = makeGraphTable(bottom, {
            QString::fromUtf8("图/方程组"),
            QString::fromUtf8("算子/角色"),
            QString::fromUtf8("输入"),
            QString::fromUtf8("输出"),
            QString::fromUtf8("关系说明")
        });
        graphCatalogModelTable_ = makeGraphTable(bottom, {
            QString::fromUtf8("模型 ID"),
            QString::fromUtf8("类型/领域"),
            QString::fromUtf8("运行时/版本"),
            QString::fromUtf8("资产"),
            QString::fromUtf8("适用包络/状态")
        });
        bottom->addWidget(graphEquationTable_);
        bottom->addWidget(graphCatalogModelTable_);
        bottom->setStretchFactor(0, 2);
        bottom->setStretchFactor(1, 2);

        vertical->addWidget(top);
        vertical->addWidget(bottom);
        vertical->setStretchFactor(0, 1);
        vertical->setStretchFactor(1, 1);
        layout->addWidget(vertical, 1);
        graphWorkflowTabs_->addTab(page, QString::fromUtf8("对象/模型/方程"));
    }

    {
        auto* page = new QWidget(graphWorkflowTabs_);
        auto* layout = new QVBoxLayout(page);
        layout->setContentsMargins(6, 6, 6, 6);
        layout->setSpacing(6);
        graphWorkflowPathWidget_ = new flightenv::ui::display::WorkflowPathWidget(page);
        layout->addWidget(graphWorkflowPathWidget_);
        lbGraphThroughputSummary_ = new QLabel(QString::fromUtf8("实时吞吐：等待 operator_live_status 和 result ports。"), page);
        lbGraphThroughputSummary_->setWordWrap(true);
        lbGraphThroughputSummary_->setStyleSheet(QStringLiteral("QLabel{padding:5px;background:#f8fafc;border:1px solid #e5e7eb;border-radius:4px;}"));
        layout->addWidget(lbGraphThroughputSummary_);
        auto* splitter = new QSplitter(Qt::Horizontal, page);
        graphPathTable_ = makeGraphTable(splitter, {
            QString::fromUtf8("序号"), QString::fromUtf8("节点"), QString::fromUtf8("算子类型"),
            QString::fromUtf8("输入端口"), QString::fromUtf8("输出端口"), QString::fromUtf8("执行形态")
        });
        graphThroughputTable_ = makeGraphTable(splitter, {
            QString::fromUtf8("节点"), QString::fromUtf8("状态"), QString::fromUtf8("耗时/ms"),
            QString::fromUtf8("输出大小"), QString::fromUtf8("吞吐"), QString::fromUtf8("模型/资源")
        });
        splitter->addWidget(graphPathTable_);
        splitter->addWidget(graphThroughputTable_);
        splitter->setStretchFactor(0, 1);
        splitter->setStretchFactor(1, 1);
        layout->addWidget(splitter, 1);
        graphWorkflowTabs_->addTab(page, QString::fromUtf8("编排/吞吐"));
    }

    addTablePage(QString::fromUtf8("资产/训练"), nullptr, &graphAssetTable_,
        {QString::fromUtf8("类别"), QString::fromUtf8("ID"), QString::fromUtf8("位置/状态"), QString::fromUtf8("备注")});
    addTablePage(QString::fromUtf8("PF融合"), nullptr, &graphFusionTable_,
        {QString::fromUtf8("算子"), QString::fromUtf8("模型状态"), QString::fromUtf8("PF/运行配置"), QString::fromUtf8("场布局")});

    {
        auto* page = new QWidget(graphWorkflowTabs_);
        auto* layout = new QVBoxLayout(page);
        layout->setContentsMargins(6, 6, 6, 6);
        layout->setSpacing(6);
        lbGraphTrajectorySummary_ = new QLabel(QString::fromUtf8("等待弹道输出"), page);
        lbGraphTrajectorySummary_->setWordWrap(true);
        lbGraphTrajectorySummary_->setStyleSheet(QStringLiteral("QLabel{padding:5px;background:#f8fafc;border:1px solid #e5e7eb;border-radius:4px;}"));
        layout->addWidget(lbGraphTrajectorySummary_);
        graphTrajectoryPlot_ = new flightenv::ui::display::TrajectoryPathWidget(page);
        layout->addWidget(graphTrajectoryPlot_, 1);
        graphTrajectoryTable_ = makeGraphTable(page,
            {QString::fromUtf8("t/s"), QString::fromUtf8("高度/m"), QString::fromUtf8("Mach"), QString::fromUtf8("动压/Pa"), QString::fromUtf8("攻角/rad"), QString::fromUtf8("法向过载/g")});
        graphTrajectoryTable_->setMaximumHeight(180);
        layout->addWidget(graphTrajectoryTable_);
        graphWorkflowTabs_->addTab(page, QString::fromUtf8("弹道"));
    }

    {
        auto* page = new QWidget(graphWorkflowTabs_);
        auto* layout = new QVBoxLayout(page);
        layout->setContentsMargins(6, 6, 6, 6);
        layout->setSpacing(6);
        lbGraphFieldSummary_ = new QLabel(QString::fromUtf8("等待多物理场输出"), page);
        lbGraphFieldSummary_->setWordWrap(true);
        lbGraphFieldSummary_->setStyleSheet(QStringLiteral("QLabel{padding:5px;background:#f8fafc;border:1px solid #e5e7eb;border-radius:4px;}"));
        layout->addWidget(lbGraphFieldSummary_);
        auto* controls = new QHBoxLayout();
        controls->addWidget(new QLabel(QString::fromUtf8("云图 subject"), page));
        graphFieldSubjectCombo_ = makeSubjectCombo(page, QString::fromUtf8("选择预测场 subject，云图显示该 subject 最新 forecast step。"));
        graphFieldSubjectCombo_->setCurrentIndex(3);
        controls->addWidget(graphFieldSubjectCombo_);
        controls->addStretch();
        layout->addLayout(controls);
        graphMultiFieldScroll_ = new QScrollArea(page);
        graphMultiFieldScroll_->setWidgetResizable(true);
        graphMultiFieldContainer_ = new QWidget(graphMultiFieldScroll_);
        graphMultiFieldGrid_ = new QGridLayout(graphMultiFieldContainer_);
        graphMultiFieldGrid_->setContentsMargins(0, 0, 0, 0);
        graphMultiFieldGrid_->setSpacing(10);
        graphMultiFieldContainer_->setLayout(graphMultiFieldGrid_);
        graphMultiFieldScroll_->setWidget(graphMultiFieldContainer_);
        layout->addWidget(graphMultiFieldScroll_, 1);
        graphFieldVtk_ = new flightenv::ui::demo::VtkModelFieldWidget(page);
        graphFieldVtk_->hide();
        graphFieldTable_ = makeGraphTable(page,
            {QString::fromUtf8("subject"), QString::fromUtf8("field"), QString::fromUtf8("步数"), QString::fromUtf8("节点/值数"), QString::fromUtf8("首步统计"), QString::fromUtf8("末步统计")});
        graphFieldTable_->setMaximumHeight(170);
        layout->addWidget(graphFieldTable_);
        graphWorkflowTabs_->addTab(page, QString::fromUtf8("多物理场"));
    }

    {
        auto* page = new QWidget(graphWorkflowTabs_);
        auto* layout = new QVBoxLayout(page);
        layout->setContentsMargins(6, 6, 6, 6);
        layout->setSpacing(6);
        lbGraphDamageSummary_ = new QLabel(QString::fromUtf8("等待损伤输出"), page);
        lbGraphDamageSummary_->setWordWrap(true);
        lbGraphDamageSummary_->setStyleSheet(QStringLiteral("QLabel{padding:5px;background:#f8fafc;border:1px solid #e5e7eb;border-radius:4px;}"));
        layout->addWidget(lbGraphDamageSummary_);
        auto* controls = new QHBoxLayout();
        controls->addWidget(new QLabel(QString::fromUtf8("云图 subject"), page));
        graphDamageSubjectCombo_ = makeSubjectCombo(page, QString::fromUtf8("选择累计损伤场 subject，云图显示最新 forecast step。"));
        graphDamageSubjectCombo_->setCurrentIndex(3);
        controls->addWidget(graphDamageSubjectCombo_);
        controls->addStretch();
        layout->addLayout(controls);
        auto* splitter = new QSplitter(Qt::Horizontal, page);
        graphDamageVtk_ = new flightenv::ui::demo::VtkModelFieldWidget(splitter);
        graphDamageTrend_ = new flightenv::ui::display::ScalarTrendWidget(splitter);
        graphDamageTrend_->setTitle(QString::fromUtf8("最大累计损伤"), QStringLiteral("0-1"));
        graphDamageTrend_->setFixedRange(0.0, 1.0);
        splitter->addWidget(graphDamageVtk_);
        splitter->addWidget(graphDamageTrend_);
        splitter->setStretchFactor(0, 2);
        splitter->setStretchFactor(1, 1);
        layout->addWidget(splitter, 1);
        graphDamageTable_ = makeGraphTable(page,
            {QString::fromUtf8("时间/s"), QString::fromUtf8("增量数"), QString::fromUtf8("最大累计损伤"), QString::fromUtf8("最大增量"), QString::fromUtf8("首超")});
        graphDamageTable_->setMaximumHeight(170);
        layout->addWidget(graphDamageTable_);
        graphWorkflowTabs_->addTab(page, QString::fromUtf8("损伤"));
    }

    {
        auto* page = new QWidget(graphWorkflowTabs_);
        auto* layout = new QVBoxLayout(page);
        layout->setContentsMargins(6, 6, 6, 6);
        layout->setSpacing(6);
        lbGraphLifeSummary_ = new QLabel(QString::fromUtf8("等待寿命输出"), page);
        lbGraphLifeSummary_->setWordWrap(true);
        lbGraphLifeSummary_->setStyleSheet(QStringLiteral("QLabel{padding:5px;background:#f8fafc;border:1px solid #e5e7eb;border-radius:4px;}"));
        layout->addWidget(lbGraphLifeSummary_);
        auto* controls = new QHBoxLayout();
        controls->addWidget(new QLabel(QString::fromUtf8("寿命场 subject"), page));
        graphLifeSubjectCombo_ = makeSubjectCombo(page, QString::fromUtf8("优先显示 life.field 节点级剩余寿命场；缺失时才兼容旧 evidence 派生显示。"));
        graphLifeSubjectCombo_->setCurrentIndex(3);
        controls->addWidget(graphLifeSubjectCombo_);
        controls->addStretch();
        layout->addLayout(controls);
        auto* splitter = new QSplitter(Qt::Horizontal, page);
        graphLifeVtk_ = new flightenv::ui::demo::VtkModelFieldWidget(splitter);
        graphLifeTrend_ = new flightenv::ui::display::ScalarTrendWidget(splitter);
        graphLifeTrend_->setTitle(QString::fromUtf8("RUL/未来步"), QStringLiteral("s"));
        splitter->addWidget(graphLifeVtk_);
        splitter->addWidget(graphLifeTrend_);
        splitter->setStretchFactor(0, 2);
        splitter->setStretchFactor(1, 1);
        layout->addWidget(splitter, 1);
        graphLifeTable_ = makeGraphTable(page,
            {QString::fromUtf8("指标"), QString::fromUtf8("值"), QString::fromUtf8("说明")});
        graphLifeTable_->setMaximumHeight(170);
        layout->addWidget(graphLifeTable_);
        graphWorkflowTabs_->addTab(page, QString::fromUtf8("寿命"));
    }

    graphEvidenceText_ = new QPlainTextEdit(graphWorkflowTabs_);
    graphEvidenceText_->setReadOnly(true);
    graphEvidenceText_->setMaximumBlockCount(200);
    auto* evidencePage = new QWidget(graphWorkflowTabs_);
    auto* evidenceLayout = new QVBoxLayout(evidencePage);
    evidenceLayout->setContentsMargins(6, 6, 6, 6);
    evidenceLayout->addWidget(graphEvidenceText_);
    graphWorkflowTabs_->addTab(evidencePage, QString::fromUtf8("Evidence"));

    root->addWidget(graphWorkflowTabs_, 1);

    connect(graphFieldSubjectCombo_, qOverload<int>(&QComboBox::currentIndexChanged),
        this, &EnvPredictorUI::refreshGraphWorkflowEvidence_);
    connect(graphDamageSubjectCombo_, qOverload<int>(&QComboBox::currentIndexChanged),
        this, &EnvPredictorUI::refreshGraphWorkflowEvidence_);
    connect(graphLifeSubjectCombo_, qOverload<int>(&QComboBox::currentIndexChanged),
        this, &EnvPredictorUI::refreshGraphWorkflowEvidence_);
}

void EnvPredictorUI::clearGraphMultiFieldWidgets_()
{
    for (auto& item : graphMultiFieldWidgets_) {
        if (item.second) {
            item.second->deleteLater();
        }
    }
    graphMultiFieldWidgets_.clear();
    graphMultiFieldOrder_.clear();
}

void EnvPredictorUI::layoutGraphMultiFieldWidgets_()
{
    if (!graphMultiFieldGrid_ || !graphMultiFieldContainer_) {
        return;
    }

    QLayoutItem* item = nullptr;
    while ((item = graphMultiFieldGrid_->takeAt(0)) != nullptr) {
        if (item->widget()) {
            item->widget()->setParent(nullptr);
        }
        delete item;
    }

    const int columns = ui.windowNumspinBox
        ? std::max(1, ui.windowNumspinBox->value())
        : 2;
    for (int col = 0; col < columns; ++col) {
        graphMultiFieldGrid_->setColumnStretch(col, 1);
    }

    int visibleCount = 0;
    for (const QString& key : graphMultiFieldOrder_) {
        const auto it = graphMultiFieldWidgets_.find(key);
        if (it == graphMultiFieldWidgets_.end() || it->second == nullptr) {
            continue;
        }
        auto* widget = it->second;
        widget->show();
        widget->setParent(graphMultiFieldContainer_);
        graphMultiFieldGrid_->addWidget(widget, visibleCount / columns, visibleCount % columns);
        ++visibleCount;
    }

    if (visibleCount == 0) {
        auto*& widget = graphMultiFieldWidgets_[QStringLiteral("__empty__")];
        if (!widget) {
            widget = new flightenv::ui::demo::VtkModelFieldWidget(graphMultiFieldContainer_);
            widget->setMinimumHeight(220);
        }
        widget->clearField(QStringLiteral("No renderable field output in current graph evidence."));
        widget->show();
        widget->setParent(graphMultiFieldContainer_);
        graphMultiFieldGrid_->addWidget(widget, 0, 0);
    }

    graphMultiFieldContainer_->updateGeometry();
    if (graphMultiFieldScroll_) {
        graphMultiFieldScroll_->updateGeometry();
    }
}

void EnvPredictorUI::renderGraphPlatformFieldArtifacts_()
{
    if (!graphMultiFieldGrid_ || !runtime_view_) {
        return;
    }

    graphMultiFieldOrder_.clear();
    const auto fields = platformFieldsForCurrentFrame();
    const QString assetRoot = workspacePath(QStringLiteral("_deps/example"));

    for (const auto& field : fields) {
        const QString key = platformFieldIdentityKey(field);
        graphMultiFieldOrder_.push_back(key);

        auto*& widget = graphMultiFieldWidgets_[key];
        if (widget == nullptr) {
            widget = new flightenv::ui::demo::VtkModelFieldWidget(graphMultiFieldContainer_);
            widget->setMinimumHeight(220);
            widget->setAssetRoot(assetRoot);
            widget->setRuntimeSnapshot(runtime_view_->snapshot);
        }

        QString readError;
        bool loading = false;
        const std::vector<double> values = cachedPlatformFieldArtifactValues(field, &readError, &loading);
        if (loading && values.empty()) {
            widget->setStatusMessage(widget->hasRenderedValues()
                                         ? QStringLiteral("云图数据后台加载中，保留上一帧显示...")
                                         : QStringLiteral("云图数据后台加载中..."));
            continue;
        }
        if (!readError.isEmpty()) {
            widget->clearField(readError);
            continue;
        }
        if (field.node_count > 0 &&
            (values.size() < static_cast<std::size_t>(field.node_count) ||
             values.size() % static_cast<std::size_t>(field.node_count) != 0)) {
            widget->clearField(QStringLiteral("field artifact values cannot map to node_count: values=%1 node_count=%2")
                                   .arg(values.size())
                                   .arg(static_cast<qlonglong>(field.node_count)));
            continue;
        }

        widget->renderPlatformFieldArtifact(
            field,
            values,
            0,
            QStringLiteral("%1 · loop %2").arg(platformFieldTitle(field)).arg(field.loop_iteration_index));
    }

    layoutGraphMultiFieldWidgets_();
}

void EnvPredictorUI::buildGraphRuntimePage_()
{
    pageGraphRuntime_ = new QWidget(this);
    auto* root = new QVBoxLayout(pageGraphRuntime_);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);

    auto* controlGroup = new QGroupBox(QString::fromUtf8("GraphRuntimeControllerRunner"), pageGraphRuntime_);
    auto* controlLayout = new QGridLayout(controlGroup);
    controlLayout->setContentsMargins(10, 12, 10, 10);
    controlLayout->setHorizontalSpacing(8);
    controlLayout->setVerticalSpacing(6);

    graphCatalogPathEdit_ = new QLineEdit(
        workspacePath(QStringLiteral("_local_artifacts/flightenv-runtime-private/platform/platform-catalog.json")),
        controlGroup);
    graphOutputDirEdit_ = new QLineEdit(
        workspacePath(QStringLiteral("_local_artifacts/flightenv-runtime-private/graph-runtime-controller/ui_live_run")),
        controlGroup);
    // 这四个控件会原样传给 runner：预测步数控制单次未来链路长度，
    // 在线帧数和预测间隔帧控制数据库 replay 以及何时触发完整预测。
    graphMaxSamplesSpin_ = new QSpinBox(controlGroup);
    graphMaxSamplesSpin_->setRange(1, 2000);
    graphMaxSamplesSpin_->setValue(8);
    graphMaxSamplesSpin_->setToolTip(QString::fromUtf8("每次预测 run 的未来弹道/场/损伤推进步数。"));
    graphReplayFramesSpin_ = new QSpinBox(controlGroup);
    graphReplayFramesSpin_->setRange(1, 1000);
    graphReplayFramesSpin_->setValue(50);
    graphReplayFramesSpin_->setToolTip(QString::fromUtf8("从数据库 taskpoint replay 的在线输入帧数；每一帧对应一次在线融合/滤波状态更新。"));
    graphPredictionIntervalSpin_ = new QSpinBox(controlGroup);
    graphPredictionIntervalSpin_->setRange(1, 500);
    graphPredictionIntervalSpin_->setValue(10);
    graphPredictionIntervalSpin_->setToolTip(QString::fromUtf8("每隔多少个在线帧触发一次完整未来预测；runner 也会在第一帧和最后一帧触发。"));
    graphFrameDelayMsSpin_ = new QSpinBox(controlGroup);
    graphFrameDelayMsSpin_->setRange(0, 5000);
    graphFrameDelayMsSpin_->setValue(200);
    graphFrameDelayMsSpin_->setToolTip(QString::fromUtf8("在线帧 replay 延迟，用于 UI 中观察实时刷新。"));
    graphPredictToLandingCheck_ = new QCheckBox(QString::fromUtf8("预测到落点/完整 horizon"), controlGroup);
    graphPredictToLandingCheck_->setToolTip(QString::fromUtf8(
        "勾选后 UI 将单次预测步数提升到 2000，用于检查弹道从当前在线状态延伸到落点；不勾选时使用左侧步数做快速调试。"));
    graphPredictionOffCheck_ = new QCheckBox(QString::fromUtf8("只跑在线滤波"), controlGroup);
    graphPredictionOffCheck_->setToolTip(QString::fromUtf8(
        "勾选后只回放数据库观测并执行在线状态转移/观测/滤波，不触发未来预测/QoI 分支。"));

    graphStartButton_ = new QPushButton(QString::fromUtf8("启动算子图"), controlGroup);
    graphStopButton_ = new QPushButton(QString::fromUtf8("停止"), controlGroup);
    graphStopButton_->setEnabled(false);
    auto* refreshButton = new QPushButton(QString::fromUtf8("刷新状态"), controlGroup);

    controlLayout->addWidget(new QLabel(QString::fromUtf8("Catalog"), controlGroup), 0, 0);
    controlLayout->addWidget(graphCatalogPathEdit_, 0, 1, 1, 4);
    controlLayout->addWidget(new QLabel(QString::fromUtf8("Evidence"), controlGroup), 1, 0);
    controlLayout->addWidget(graphOutputDirEdit_, 1, 1, 1, 4);
    controlLayout->addWidget(new QLabel(QString::fromUtf8("每次预测步数"), controlGroup), 2, 0);
    controlLayout->addWidget(graphMaxSamplesSpin_, 2, 1);
    controlLayout->addWidget(new QLabel(QString::fromUtf8("在线帧数"), controlGroup), 2, 2);
    controlLayout->addWidget(graphReplayFramesSpin_, 2, 3);
    controlLayout->addWidget(new QLabel(QString::fromUtf8("预测间隔帧"), controlGroup), 2, 4);
    controlLayout->addWidget(graphPredictionIntervalSpin_, 2, 5);
    controlLayout->addWidget(new QLabel(QString::fromUtf8("帧延迟/ms"), controlGroup), 3, 0);
    controlLayout->addWidget(graphFrameDelayMsSpin_, 3, 1);
    controlLayout->addWidget(graphPredictToLandingCheck_, 3, 2, 1, 2);
    controlLayout->addWidget(graphPredictionOffCheck_, 3, 4, 1, 2);
    controlLayout->addWidget(graphStartButton_, 4, 2);
    controlLayout->addWidget(graphStopButton_, 4, 3);
    controlLayout->addWidget(refreshButton, 4, 4, 1, 2);
    controlLayout->setColumnStretch(1, 1);
    root->addWidget(controlGroup);

    lbGraphRuntimeStatus_ = new QLabel(QString::fromUtf8("未启动"), pageGraphRuntime_);
    lbGraphRuntimeStatus_->setWordWrap(true);
    lbGraphRuntimeStatus_->setStyleSheet(QStringLiteral("QLabel{padding:6px;border:1px solid #cbd5e1;border-radius:4px;background:#f8fafc;}"));
    root->addWidget(lbGraphRuntimeStatus_);

    graphOperatorScroll_ = new QScrollArea(pageGraphRuntime_);
    graphOperatorScroll_->setWidgetResizable(true);
    graphOperatorScroll_->setMinimumHeight(260);
    graphOperatorContainer_ = new QWidget(graphOperatorScroll_);
    graphOperatorGrid_ = new QGridLayout(graphOperatorContainer_);
    graphOperatorGrid_->setContentsMargins(4, 4, 4, 4);
    graphOperatorGrid_->setSpacing(8);
    graphOperatorScroll_->setWidget(graphOperatorContainer_);
    root->addWidget(graphOperatorScroll_, 1);

    buildGraphWorkflowTabs_(root);

    graphRuntimeLog_ = new QPlainTextEdit(pageGraphRuntime_);
    graphRuntimeLog_->setReadOnly(true);
    graphRuntimeLog_->setMaximumBlockCount(500);
    graphRuntimeLog_->setMinimumHeight(120);
    root->addWidget(graphRuntimeLog_);

    graphRunnerProcess_ = new QProcess(this);
    graphRunnerProcess_->setProcessChannelMode(QProcess::MergedChannels);
    graphRuntimePollTimer_ = new QTimer(this);
    graphRuntimePollTimer_->setInterval(500);

    connect(graphStartButton_, &QPushButton::clicked, this, &EnvPredictorUI::startGraphRuntimeRunner_);
    connect(graphStopButton_, &QPushButton::clicked, this, &EnvPredictorUI::stopGraphRuntimeRunner_);
    connect(refreshButton, &QPushButton::clicked, this, &EnvPredictorUI::refreshGraphRuntimePage_);
    connect(refreshButton, &QPushButton::clicked, this, &EnvPredictorUI::refreshGraphWorkflowEvidence_);
    connect(graphPredictToLandingCheck_, &QCheckBox::toggled, this, [this](const bool checked) {
        if (graphMaxSamplesSpin_) {
            graphMaxSamplesSpin_->setEnabled(!checked);
        }
    });
    connect(graphRuntimePollTimer_, &QTimer::timeout, this, &EnvPredictorUI::refreshGraphRuntimePage_);
    connect(graphRunnerProcess_, &QProcess::readyReadStandardOutput, this, [this]() {
        appendGraphRuntimeLog_(QString::fromUtf8(graphRunnerProcess_->readAllStandardOutput()));
    });
    connect(graphRunnerProcess_, &QProcess::readyReadStandardError, this, [this]() {
        appendGraphRuntimeLog_(QString::fromUtf8(graphRunnerProcess_->readAllStandardError()));
    });
    connect(graphRunnerProcess_,
        qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
        this,
        [this](int exitCode, QProcess::ExitStatus exitStatus) {
            appendGraphRuntimeLog_(QString::fromUtf8("GraphRuntimeControllerRunner 退出：code=%1 status=%2")
                .arg(exitCode)
                .arg(exitStatus == QProcess::NormalExit ? QStringLiteral("normal") : QStringLiteral("crash")));
            refreshGraphRuntimePage_();
            if (graphRuntimePollTimer_) {
                graphRuntimePollTimer_->stop();
            }
            if (graphStartButton_) {
                graphStartButton_->setEnabled(true);
            }
            if (graphStopButton_) {
                graphStopButton_->setEnabled(false);
            }
        });

    ui.tabWidget_main->insertTab(2, pageGraphRuntime_, QString::fromUtf8("算子运行"));
    refreshGraphRuntimePage_();
}

void EnvPredictorUI::setGraphRuntimeStatus_(const QString& text)
{
    if (lbGraphRuntimeStatus_) {
        lbGraphRuntimeStatus_->setText(text);
    }
}

void EnvPredictorUI::appendGraphRuntimeLog_(const QString& text)
{
    if (!graphRuntimeLog_ || text.isEmpty()) {
        return;
    }
    graphRuntimeLog_->appendPlainText(text.trimmed());
}

void EnvPredictorUI::startGraphRuntimeRunner_()
{
    if (!graphRunnerProcess_) {
        return;
    }
    if (graphRunnerProcess_->state() != QProcess::NotRunning) {
        setGraphRuntimeStatus_(QString::fromUtf8("算子图已经在运行中。"));
        return;
    }

    const QString runnerPath = workspacePath(QStringLiteral("_deps/workspace/x64/Release/GraphRuntimeControllerRunner.exe"));
    const QString catalogPath = graphCatalogPathEdit_ ? graphCatalogPathEdit_->text().trimmed() : QString();
    const QString outputDir = graphOutputDirEdit_ ? graphOutputDirEdit_->text().trimmed() : QString();
    if (!QFileInfo::exists(runnerPath)) {
        setGraphRuntimeStatus_(QString::fromUtf8("未找到 Runner：%1").arg(runnerPath));
        return;
    }
    if (!QFileInfo::exists(catalogPath)) {
        setGraphRuntimeStatus_(QString::fromUtf8("未找到 catalog：%1").arg(catalogPath));
        return;
    }
    QDir().mkpath(outputDir);
    // 启动前清掉上一轮 evidence，避免 UI 先显示旧的单帧/旧 run 结果。
    for (const QString& name : {
        QStringLiteral("operator_live_status.json"),
        QStringLiteral("graph_run_evidence.json"),
        QStringLiteral("graph_snapshot.json"),
        QStringLiteral("operator_snapshot.json"),
        QStringLiteral("resource_lock.json"),
        QStringLiteral("graph_outputs.json"),
        QStringLiteral("runtime_snapshot.json"),
        QStringLiteral("workflow_timeline.json")}) {
        QFile::remove(QDir(outputDir).filePath(name));
    }
    QDir staleOnlineDir(QDir(outputDir).filePath(QStringLiteral("online_filter")));
    if (staleOnlineDir.exists()) {
        staleOnlineDir.removeRecursively();
    }

    while (graphOperatorGrid_ && graphOperatorGrid_->count() > 0) {
        QLayoutItem* item = graphOperatorGrid_->takeAt(0);
        if (item && item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }
    graphOperatorCards_.clear();
    clearGraphTable(graphPathTable_);
    clearGraphTable(graphThroughputTable_);
    clearGraphTable(graphAssetTable_);
    clearGraphTable(graphFusionTable_);
    clearGraphTable(graphPathTable_);
    clearGraphTable(graphThroughputTable_);
    clearGraphTable(graphObjectTable_);
    clearGraphTable(graphBindingTable_);
    clearGraphTable(graphEquationTable_);
    clearGraphTable(graphCatalogModelTable_);
    clearGraphTable(graphTrajectoryTable_);
    clearGraphTable(graphFieldTable_);
    clearGraphTable(graphDamageTable_);
    clearGraphTable(graphLifeTable_);
    if (graphWorkflowPathWidget_) {
        graphWorkflowPathWidget_->clear();
    }
    if (graphTrajectoryPlot_) {
        graphTrajectoryPlot_->clear();
    }
    if (graphDamageTrend_) {
        graphDamageTrend_->clear();
    }
    if (graphLifeTrend_) {
        graphLifeTrend_->clear();
    }
    clearGraphMultiFieldWidgets_();
    if (graphFieldVtk_) {
        graphFieldVtk_->clearField(QString::fromUtf8("等待本轮 runtime_snapshot 和 field.forecast"));
    }
    if (graphDamageVtk_) {
        graphDamageVtk_->clearField(QString::fromUtf8("等待本轮 runtime_snapshot 和 damage.forecast"));
    }
    if (graphLifeVtk_) {
        graphLifeVtk_->clearField(QString::fromUtf8("等待本轮 runtime_snapshot 和 life.field"));
    }
    setLabelText(lbGraphWorkflowSummary_, QString::fromUtf8("完整链路：等待本轮运行输出。"));
    setLabelText(lbGraphObjectSummary_, QString::fromUtf8("对象画像：等待本轮 catalog/evidence 解析。"));
    setLabelText(lbGraphThroughputSummary_, QString::fromUtf8("实时吞吐：等待本轮运行输出。"));
    setLabelText(lbGraphTrajectorySummary_, QString::fromUtf8("等待弹道输出"));
    setLabelText(lbGraphFieldSummary_, QString::fromUtf8("等待多物理场输出"));
    setLabelText(lbGraphDamageSummary_, QString::fromUtf8("等待损伤输出"));
    setLabelText(lbGraphLifeSummary_, QString::fromUtf8("等待寿命输出"));
    if (graphEvidenceText_) {
        graphEvidenceText_->clear();
    }
    graphMultiFieldOrder_.clear();
    if (graphRuntimeLog_) {
        graphRuntimeLog_->clear();
    }

    QStringList args;
    args << catalogPath << outputDir;
    // 参数顺序必须和 GraphRuntimeControllerRunner 保持一致：
    // catalog evidence max_prediction_samples replay_frame_count prediction_interval_frames frame_delay_ms。
    const int requestedMaxSamples =
        (graphPredictToLandingCheck_ && graphPredictToLandingCheck_->isChecked())
            ? 2000
            : (graphMaxSamplesSpin_ ? graphMaxSamplesSpin_->value() : 8);
    args << QString::number(requestedMaxSamples);
    args << QString::number(graphReplayFramesSpin_ ? graphReplayFramesSpin_->value() : 50);
    args << QString::number(graphPredictionIntervalSpin_ ? graphPredictionIntervalSpin_->value() : 10);
    args << QString::number(graphFrameDelayMsSpin_ ? graphFrameDelayMsSpin_->value() : 200);
    if (graphPredictionOffCheck_ && graphPredictionOffCheck_->isChecked()) {
        args << QStringLiteral("keep") << QStringLiteral("prediction_off");
    }
    graphRunnerProcess_->setWorkingDirectory(QFileInfo(runnerPath).absolutePath());
    appendGraphRuntimeLog_(QString::fromUtf8("启动：%1 %2").arg(runnerPath, args.join(QStringLiteral(" "))));
    graphRunnerProcess_->start(runnerPath, args);
    if (!graphRunnerProcess_->waitForStarted(3000)) {
        setGraphRuntimeStatus_(QString::fromUtf8("Runner 启动失败：%1").arg(graphRunnerProcess_->errorString()));
        return;
    }
    if (graphStartButton_) {
        graphStartButton_->setEnabled(false);
    }
    if (graphStopButton_) {
        graphStopButton_->setEnabled(true);
    }
    if (graphRuntimePollTimer_) {
        graphRuntimePollTimer_->start();
    }
    setGraphRuntimeStatus_(QString::fromUtf8("运行中：正在等待 operator_live_status.json。"));
}

void EnvPredictorUI::stopGraphRuntimeRunner_()
{
    if (graphRuntimePollTimer_) {
        graphRuntimePollTimer_->stop();
    }
    if (graphRunnerProcess_ && graphRunnerProcess_->state() != QProcess::NotRunning) {
        appendGraphRuntimeLog_(QString::fromUtf8("请求停止 GraphRuntimeControllerRunner。"));
        graphRunnerProcess_->terminate();
        if (!graphRunnerProcess_->waitForFinished(3000)) {
            graphRunnerProcess_->kill();
            graphRunnerProcess_->waitForFinished(3000);
        }
    }
    if (graphStartButton_) {
        graphStartButton_->setEnabled(true);
    }
    if (graphStopButton_) {
        graphStopButton_->setEnabled(false);
    }
}

void EnvPredictorUI::refreshGraphRuntimePage_()
{
    if (!graphOutputDirEdit_ || !graphOperatorGrid_) {
        return;
    }
    const QString outputDir = graphOutputDirEdit_->text().trimmed();
    try {
        const auto live = launchsupport::read_graph_operator_live_status(
            std::filesystem::path(outputDir.toStdWString()));
        if (live.status == "missing") {
            setGraphRuntimeStatus_(QString::fromUtf8("等待实时状态文件：%1")
                .arg(QString::fromStdWString(live.live_status_path.wstring())));
            return;
        }

        setGraphRuntimeStatus_(QString::fromUtf8("状态=%1，算子=%2，run=%3")
            .arg(liveStatusText(live.status))
            .arg(static_cast<int>(live.operators.size()))
            .arg(QString::fromUtf8(live.run_id.c_str())));

        for (const auto& op : live.operators) {
            const QString key = QString::fromUtf8(
                !op.node_id.empty() ? op.node_id.c_str() : op.operator_ref.c_str());
            QGroupBox* box = nullptr;
            auto found = graphOperatorCards_.find(key);
            if (found == graphOperatorCards_.end()) {
                box = new QGroupBox(key, graphOperatorContainer_);
                auto* form = new QFormLayout(box);
                form->setContentsMargins(8, 12, 8, 8);
                form->setSpacing(4);
                const QList<QPair<QString, QString>> rows = {
                    {QStringLiteral("status"), QString::fromUtf8("状态")},
                    {QStringLiteral("type"), QString::fromUtf8("类型")},
                    {QStringLiteral("kind"), QString::fromUtf8("执行")},
                    {QStringLiteral("ports"), QString::fromUtf8("端口")},
                    {QStringLiteral("resources"), QString::fromUtf8("资源")},
                    {QStringLiteral("duration"), QString::fromUtf8("耗时")},
                    {QStringLiteral("reason"), QString::fromUtf8("诊断")}
                };
                for (const auto& row : rows) {
                    auto* label = new QLabel(box);
                    label->setObjectName(row.first);
                    label->setWordWrap(true);
                    form->addRow(row.second, label);
                }
                const int cardIndex = static_cast<int>(graphOperatorCards_.size());
                graphOperatorGrid_->addWidget(box, cardIndex / 2, cardIndex % 2);
                graphOperatorCards_[key] = box;
            }
            else {
                box = found->second;
            }
            box->setTitle(QString::fromUtf8("%1  %2").arg(key, QString::fromUtf8(op.operator_id.c_str())));
            box->setStyleSheet(liveStatusStyle(QString::fromUtf8(op.status.c_str())));
            if (auto* label = namedLabel(box, "status")) {
                label->setText(liveStatusText(op.status));
            }
            if (auto* label = namedLabel(box, "type")) {
                label->setText(QString::fromUtf8("%1 / %2")
                    .arg(QString::fromUtf8(op.operator_type.c_str()), QString::fromUtf8(op.role.c_str())));
            }
            if (auto* label = namedLabel(box, "kind")) {
                label->setText(QString::fromUtf8("%1  %2")
                    .arg(QString::fromUtf8(op.execution_kind.c_str()), QString::fromUtf8(op.operator_ref.c_str())));
            }
            if (auto* label = namedLabel(box, "ports")) {
                label->setText(QString::fromUtf8("%1 -> %2")
                    .arg(joinStdStrings(op.input_ports), joinStdStrings(op.output_ports)));
            }
            if (auto* label = namedLabel(box, "resources")) {
                label->setText(joinStdStrings(op.input_resource_ids, 3));
            }
            if (auto* label = namedLabel(box, "duration")) {
                label->setText(op.duration_ms > 0
                    ? QString::fromUtf8("%1 ms").arg(static_cast<qlonglong>(op.duration_ms))
                    : QString::fromUtf8("-"));
            }
            if (auto* label = namedLabel(box, "reason")) {
                label->setText(QString::fromUtf8(op.reason.c_str()));
            }
        }
        refreshGraphWorkflowEvidence_();
    }
    catch (const std::exception& e) {
        setGraphRuntimeStatus_(QString::fromUtf8("读取 GraphRuntime 状态失败：%1").arg(QString::fromUtf8(e.what())));
    }
}

void EnvPredictorUI::refreshGraphWorkflowEvidence_()
{
    if (!graphOutputDirEdit_) {
        return;
    }

    clearGraphTable(graphAssetTable_);
    clearGraphTable(graphFusionTable_);
    clearGraphTable(graphPathTable_);
    clearGraphTable(graphThroughputTable_);
    clearGraphTable(graphObjectTable_);
    clearGraphTable(graphBindingTable_);
    clearGraphTable(graphEquationTable_);
    clearGraphTable(graphCatalogModelTable_);
    clearGraphTable(graphTrajectoryTable_);
    clearGraphTable(graphFieldTable_);
    clearGraphTable(graphDamageTable_);
    clearGraphTable(graphLifeTable_);
    if (graphEvidenceText_) {
        graphEvidenceText_->clear();
    }
    graphMultiFieldOrder_.clear();
    const bool graphDtoVtkEnabled = false;

    const QString outputDirText = graphOutputDirEdit_->text().trimmed();
    const QString catalogPathText = graphCatalogPathEdit_ ? graphCatalogPathEdit_->text().trimmed() : QString();
    populateCatalogWorkbench(
        catalogPathText,
        lbGraphObjectSummary_,
        graphObjectTable_,
        graphBindingTable_,
        graphCatalogModelTable_);
    appendConceptualEquationRows(graphEquationTable_);

    const std::filesystem::path runDir(outputDirText.toStdWString());
    const std::filesystem::path evidencePath = runDir / "graph_run_evidence.json";
    if (!std::filesystem::exists(evidencePath)) {
        // 首个 graph_run_evidence.json 通常要等真实模型初始化和第一次预测结束。
        // 这段先读 workflow_timeline.json，让 UI 能看到在线帧/预测 run 的过程状态。
        QString timelineError;
        const auto timeline = readJsonFileNlohmann(runDir / "workflow_timeline.json", &timelineError);
        if (timeline && timeline->is_object()) {
            const QString summary = QString::fromUtf8("在线帧=%1/%2，滤波帧=%3，预测run=%4，状态=%5；等待首个 graph evidence")
                .arg(static_cast<qlonglong>(jsonDouble(*timeline, "observed_frame_count", 0.0)))
                .arg(static_cast<qlonglong>(jsonDouble(*timeline, "requested_frame_count", 0.0)))
                .arg(static_cast<qlonglong>(jsonDouble(*timeline, "filter_update_count", 0.0)))
                .arg(static_cast<qlonglong>(jsonDouble(*timeline, "prediction_run_count", 0.0)))
                .arg(jsonString(*timeline, "status"));
            setLabelText(lbGraphWorkflowSummary_, QString::fromUtf8("完整链路：%1").arg(summary));
            setLabelText(lbGraphThroughputSummary_, QString::fromUtf8("实时吞吐：%1").arg(summary));
            appendGraphRow(graphThroughputTable_, {
                QString::fromUtf8("workflow_timeline"),
                jsonString(*timeline, "status"),
                QStringLiteral("-"),
                QStringLiteral("-"),
                summary,
                QString::fromStdWString((runDir / "workflow_timeline.json").wstring())
            });
        }
        else {
            setLabelText(lbGraphWorkflowSummary_, QString::fromUtf8("完整链路：等待 graph_run_evidence.json。"));
        }
        return;
    }

    try {
        // graph_run_evidence 出现后，所有正式展示都走 node-sdk reader；
        // UI 只消费公开 JSON/DTO，不直接 include runtime-private 内核算法。
        const auto view = launchsupport::read_graph_run_evidence(runDir);
        setLabelText(lbGraphWorkflowSummary_,
            QString::fromUtf8("完整链路：run=%1，graph=%2，状态=%3，节点=%4，算子=%5，输出端口=%6")
                .arg(QString::fromUtf8(view.run_id.c_str()),
                    QString::fromUtf8(view.graph_template_id.c_str()),
                    liveStatusText(view.status))
                .arg(static_cast<int>(view.graph_nodes.size()))
                .arg(static_cast<int>(view.operator_snapshots.size()))
                .arg(static_cast<int>(view.result_ports.size())));

        clearGraphTable(graphEquationTable_);
        const std::filesystem::path onlineRunDir = runDir / "online_filter";
        if (std::filesystem::exists(onlineRunDir / "graph_run_evidence.json")) {
            try {
                const auto onlineView = launchsupport::read_graph_run_evidence(onlineRunDir);
                appendEquationRowsFromView(
                    graphEquationTable_,
                    QString::fromUtf8("在线滤波子图：状态转移 + 观测 + PF"),
                    onlineView.graph_nodes);
            }
            catch (const std::exception& e) {
                appendGraphRow(graphEquationTable_, {
                    QString::fromUtf8("在线滤波子图"),
                    QString::fromUtf8("读取失败"),
                    QStringLiteral("-"),
                    QStringLiteral("-"),
                    QString::fromUtf8(e.what())
                });
                appendConceptualOnlineEquationRows(graphEquationTable_);
            }
        }
        else {
            appendConceptualOnlineEquationRows(graphEquationTable_);
        }
        appendEquationRowsFromView(
            graphEquationTable_,
            QString::fromUtf8("未来预测/QoI 子图：当前状态到落点预测"),
            view.graph_nodes);

        const auto portSizes = resultPortSizes(view.result_ports);
        std::map<std::string, launchsupport::GraphOperatorLiveStatusView> liveByNode;
        try {
            const auto live = launchsupport::read_graph_operator_live_status(runDir);
            for (const auto& op : live.operators) {
                liveByNode[op.node_id] = op;
            }
        }
        catch (...) {
        }

        std::map<std::string, launchsupport::GraphOperatorSnapshotView> snapshotByNode;
        for (const auto& snapshot : view.operator_snapshots) {
            snapshotByNode[snapshot.node_id] = snapshot;
        }

        std::vector<flightenv::ui::display::WorkflowStepView> workflowSteps;
        std::size_t totalBytes = 0;
        std::int64_t totalDurationMs = 0;
        int graphRow = 0;
        for (const auto& node : view.graph_nodes) {
            const auto liveIt = liveByNode.find(node.node_id);
            const auto snapshotIt = snapshotByNode.find(node.node_id);
            const std::int64_t durationMs = liveIt == liveByNode.end() ? 0 : liveIt->second.duration_ms;
            const std::size_t outputBytes = outputBytesForPorts(node.outputs, portSizes);
            totalBytes += outputBytes;
            totalDurationMs += std::max<std::int64_t>(0, durationMs);

            appendGraphRow(graphPathTable_, {
                QString::number(++graphRow),
                QString::fromUtf8(node.node_id.c_str()),
                QString::fromUtf8(node.operator_type.c_str()),
                joinStdStrings(node.inputs, 6),
                joinStdStrings(node.outputs, 6),
                snapshotIt == snapshotByNode.end()
                    ? QStringLiteral("-")
                    : QString::fromUtf8(snapshotIt->second.execution_kind.c_str())
            });
            appendGraphRow(graphThroughputTable_, {
                QString::fromUtf8(node.node_id.c_str()),
                liveIt == liveByNode.end()
                    ? QString::fromUtf8("未上报")
                    : liveStatusText(liveIt->second.status),
                durationMs > 0 ? QString::number(static_cast<qlonglong>(durationMs)) : QStringLiteral("-"),
                bytesText(outputBytes),
                throughputText(outputBytes, durationMs),
                snapshotIt == snapshotByNode.end()
                    ? QStringLiteral("-")
                    : joinStdStrings(snapshotIt->second.input_resource_ids, 3)
            });

            flightenv::ui::display::WorkflowStepView step;
            step.node_id = QString::fromUtf8(node.node_id.c_str());
            step.operator_type = QString::fromUtf8(node.operator_type.c_str());
            step.status = liveIt == liveByNode.end()
                ? QStringLiteral("unknown")
                : QString::fromUtf8(liveIt->second.status.c_str());
            step.inputs = joinStdStrings(node.inputs, 3);
            step.outputs = joinStdStrings(node.outputs, 3);
            step.duration_ms = durationMs;
            step.output_bytes = outputBytes;
            workflowSteps.push_back(std::move(step));
        }
        if (graphWorkflowPathWidget_) {
            graphWorkflowPathWidget_->setSteps(std::move(workflowSteps));
        }
        QString timelineSummary;
        QString timelineError;
        // workflow_timeline 是多帧调度账本，补充单次 graph evidence 看不到的总帧数和总步数。
        const auto workflowTimeline = view.has_workflow_timeline
            ? readJsonFileNlohmann(view.workflow_timeline_path, &timelineError)
            : std::optional<nlohmann::json>{};
        if (workflowTimeline && workflowTimeline->is_object()) {
            timelineSummary = QString::fromUtf8("在线帧=%1，滤波帧=%2，预测run=%3，弹道步=%4，场步=%5，损伤步=%6，寿命场节点值=%7")
                .arg(static_cast<qlonglong>(jsonDouble(*workflowTimeline, "observed_frame_count", 0.0)))
                .arg(static_cast<qlonglong>(jsonDouble(*workflowTimeline, "filter_update_count", 0.0)))
                .arg(static_cast<qlonglong>(jsonDouble(*workflowTimeline, "prediction_run_count", 0.0)))
                .arg(static_cast<qlonglong>(jsonDouble(*workflowTimeline, "trajectory_step_count_total", 0.0)))
                .arg(static_cast<qlonglong>(jsonDouble(*workflowTimeline, "field_step_count_total", 0.0)))
                .arg(static_cast<qlonglong>(jsonDouble(*workflowTimeline, "damage_step_count_total", 0.0)))
                .arg(static_cast<qlonglong>(jsonDouble(*workflowTimeline, "life_field_node_value_count", 0.0)));
            appendGraphRow(graphThroughputTable_, {
                QString::fromUtf8("workflow_timeline"),
                jsonString(*workflowTimeline, "status"),
                QStringLiteral("-"),
                bytesText(static_cast<std::size_t>(std::max<qint64>(
                    0,
                    QFileInfo(QString::fromStdWString(view.workflow_timeline_path.wstring())).size()))),
                timelineSummary,
                QString::fromStdWString(view.workflow_timeline_path.wstring())
            });
            const auto runs = workflowTimeline->value("prediction_runs", nlohmann::json::array());
            for (const auto& run : runs) {
                appendGraphRow(graphThroughputTable_, {
                    QString::fromUtf8("prediction_run[%1]").arg(static_cast<int>(jsonDouble(run, "run_index", 0.0))),
                    jsonString(run, "trajectory_status"),
                    QStringLiteral("-"),
                    QString::fromUtf8("traj=%1 field=%2 damage=%3 life_nodes=%4")
                        .arg(static_cast<qlonglong>(jsonDouble(run, "trajectory_sample_count", 0.0)))
                        .arg(static_cast<qlonglong>(jsonDouble(run, "field_step_count", 0.0)))
                        .arg(static_cast<qlonglong>(jsonDouble(run, "damage_step_count", 0.0)))
                        .arg(static_cast<qlonglong>(jsonDouble(run, "life_field_node_value_count", 0.0))),
                    QString::fromUtf8("RUL=%1s").arg(formatNumber(jsonDouble(run, "rul_s", -1.0), 7)),
                    jsonString(run, "source_state_frame_id")
                });
            }
        }
        else {
            timelineSummary = view.has_workflow_timeline
                ? QString::fromUtf8("timeline读取失败：%1").arg(timelineError)
                : QString::fromUtf8("timeline缺失：当前只能看单次 graph evidence");
        }

        setLabelText(lbGraphThroughputSummary_,
            QString::fromUtf8("实时吞吐：端口总输出=%1，累计执行耗时=%2 ms，平均吞吐=%3，runtime_snapshot=%4；%5")
                .arg(bytesText(totalBytes))
                .arg(static_cast<qlonglong>(totalDurationMs))
                .arg(throughputText(totalBytes, totalDurationMs))
                .arg(view.has_runtime_snapshot ? QString::fromUtf8("可用") : QString::fromUtf8("缺失"),
                    timelineSummary));

        const QString runnerPath = workspacePath(QStringLiteral("_deps/workspace/x64/Release/GraphRuntimeControllerRunner.exe"));
        const QString trainerPath = workspacePath(QStringLiteral("_deps/workspace/x64/Release/EnvTrainer.exe"));
        appendGraphRow(graphAssetTable_, {
            QString::fromUtf8("运行入口"),
            QString::fromUtf8("GraphRuntimeControllerRunner"),
            QFileInfo::exists(runnerPath) ? QString::fromUtf8("可用") : QString::fromUtf8("缺失"),
            runnerPath
        });
        appendGraphRow(graphAssetTable_, {
            QString::fromUtf8("训练入口"),
            QString::fromUtf8("EnvTrainer"),
            QFileInfo::exists(trainerPath) ? QString::fromUtf8("可用") : QString::fromUtf8("缺失"),
            trainerPath
        });
        for (const auto& lock : view.resource_locks) {
            QString kind = QString::fromUtf8(lock.resource_type.c_str());
            const QString id = QString::fromUtf8(lock.resource_id.c_str());
            if (id.contains(QStringLiteral("database"), Qt::CaseInsensitive)) kind = QString::fromUtf8("数据库");
            else if (id.contains(QStringLiteral("pod"), Qt::CaseInsensitive)) kind = QString::fromUtf8("POD基/训练资产");
            else if (id.contains(QStringLiteral("bpnn"), Qt::CaseInsensitive)) kind = QString::fromUtf8("BPNN网络/训练资产");
            else if (id.contains(QStringLiteral("geometry"), Qt::CaseInsensitive)) kind = QString::fromUtf8("三维模型");
            else if (id.contains(QStringLiteral("trajectory"), Qt::CaseInsensitive)) kind = QString::fromUtf8("弹道模型/包");
            appendGraphRow(graphAssetTable_, {
                kind,
                id,
                compactPathText(lock.uri),
                lock.checksum.empty()
                    ? QString::fromUtf8(lock.description.c_str())
                    : QString::fromUtf8("checksum=%1").arg(QString::fromUtf8(lock.checksum.substr(0, 12).c_str()))
            });
        }

        QString jsonError;
        const auto operatorJson = readJsonFileNlohmann(view.operator_snapshot_path, &jsonError);
        if (operatorJson && operatorJson->is_array()) {
            for (const auto& op : *operatorJson) {
                const QString type = jsonString(op, "operator_type");
                if (type != QStringLiteral("field_reconstruction") &&
                    type != QStringLiteral("field_merge") &&
                    type != QStringLiteral("state_transition") &&
                    type != QStringLiteral("qoi_equation")) {
                    continue;
                }

                const auto metadata = op.value("metadata", nlohmann::json::object());
                const auto modelSnapshot = metadata.value("model_snapshot", nlohmann::json::object());
                const auto modelMeta = modelSnapshot.value("metadata", nlohmann::json::object());
                const auto cfg = modelMeta.value("cfg_override", nlohmann::json::object());
                const auto runtimeMeta = modelMeta.value("runtime_meta_summary", nlohmann::json::object());
                const auto layouts = runtimeMeta.value("field_layouts", nlohmann::json::array());
                QString layoutText;
                for (const auto& layout : layouts) {
                    if (!layoutText.isEmpty()) {
                        layoutText += QStringLiteral("; ");
                    }
                    layoutText += QString::fromUtf8("%1:%2 nodes=%3")
                        .arg(jsonString(layout, "subject"),
                            jsonString(layout, "name"),
                            jsonString(layout, "node_count"));
                }
                if (layoutText.isEmpty()) {
                    layoutText = QStringLiteral("-");
                }

                QString cfgText;
                if (cfg.is_object()) {
                    cfgText = QString::fromUtf8("pfkit.init_state.mode=%1, runtime.mode=%2")
                        .arg(jsonString(cfg, "pfkit.init_state.mode"), jsonString(cfg, "runtime.mode"));
                }
                else if (type == QStringLiteral("state_transition")) {
                    cfgText = QString::fromUtf8("弹道状态转移 / 未来预测");
                }
                else if (type == QStringLiteral("qoi_equation")) {
                    cfgText = QString::fromUtf8("状态到 QoI 映射");
                }
                else {
                    cfgText = QStringLiteral("-");
                }

                appendGraphRow(graphFusionTable_, {
                    QString::fromUtf8("%1 / %2").arg(jsonString(op, "node_id"), type),
                    jsonString(modelSnapshot, "model_status", jsonString(modelSnapshot, "model_id")),
                    cfgText,
                    layoutText
                });
            }
        }
        else if (!jsonError.isEmpty()) {
            appendGraphRow(graphFusionTable_, {
                QString::fromUtf8("operator_snapshot"),
                QString::fromUtf8("读取失败"),
                jsonError,
                QStringLiteral("-")
            });
        }

        QString runtimeSnapshotError;
        const auto runtimeSnapshot = view.has_runtime_snapshot
            ? readRuntimeSnapshotDto(view.runtime_snapshot_path, &runtimeSnapshotError)
            : std::optional<contracts::RuntimeSnapshotDTO>{};
        const QString assetRoot = workspacePath(QStringLiteral("_deps/example"));
        if (graphDtoVtkEnabled) {
        for (auto* vtk : {graphFieldVtk_, graphDamageVtk_, graphLifeVtk_}) {
            if (!vtk) {
                continue;
            }
            vtk->setAssetRoot(assetRoot);
            if (runtimeSnapshot.has_value()) {
                vtk->setRuntimeSnapshot(runtimeSnapshot.value());
            }
        }
        if (!runtimeSnapshot.has_value()) {
            const QString message = runtimeSnapshotError.isEmpty()
                ? QString::fromUtf8("缺少 runtime_snapshot.json：无法把场值映射到真实飞船模型")
                : runtimeSnapshotError;
            if (graphFieldVtk_) graphFieldVtk_->clearField(message);
            if (graphDamageVtk_) graphDamageVtk_->clearField(message);
            if (graphLifeVtk_) graphLifeVtk_->clearField(message);
        }

        } else {
            const QString message = QStringLiteral("平台版 Controller 的三维云图只使用 PlatformFieldArtifact，旧 DTO result port 不再驱动渲染。");
            if (graphFieldVtk_) graphFieldVtk_->clearField(message);
            if (graphDamageVtk_) graphDamageVtk_->clearField(message);
            if (graphLifeVtk_) graphLifeVtk_->clearField(message);
        }

        std::optional<contracts::DamageForecastFrame> damageForecastForLife;

        QString parseError;
        if (const auto trajectoryJson = parsePortPayload(view.result_ports, "state.future", &parseError)) {
            const auto trajectory = trajectoryFrameFromGraphJson(*trajectoryJson);
            setLabelText(lbGraphTrajectorySummary_,
                QString::fromUtf8("弹道：status=%1，model=%2，horizon=%3s，step=%4s，samples=%5")
                    .arg(QString::fromUtf8(trajectory.status.c_str()),
                        QString::fromUtf8(trajectory.model_snapshot.model_id.c_str()),
                        formatNumber(trajectory.horizon_s),
                        formatNumber(trajectory.step_s))
                    .arg(static_cast<int>(trajectory.samples.size())));
            if (graphTrajectoryPlot_) {
                graphTrajectoryPlot_->setTrajectory(trajectory);
            }
            for (const auto& sample : trajectory.samples) {
                const double altitude = sample.lla_rad_m.size() >= 3 ? sample.lla_rad_m[2] : std::numeric_limits<double>::quiet_NaN();
                appendGraphRow(graphTrajectoryTable_, {
                    formatNumber(sample.time_s, 6),
                    formatNumber(altitude, 7),
                    formatNumber(sample.mach, 6),
                    formatNumber(sample.dynamic_pressure_pa, 7),
                    formatNumber(sample.angle_of_attack_rad, 6),
                    formatNumber(sample.normal_load_g, 6)
                });
            }
        }
        else {
            setLabelText(lbGraphTrajectorySummary_, QString::fromUtf8("弹道：%1").arg(parseError));
            if (graphTrajectoryPlot_) {
                graphTrajectoryPlot_->clear();
            }
        }

        parseError.clear();
        if (const auto fieldJson = parsePortPayload(view.result_ports, "field.forecast", &parseError)) {
            const auto fieldFrame = fieldJson->get<contracts::FieldForecastFrame>();
            const auto steps = fieldJson->value("steps", nlohmann::json::array());
            setLabelText(lbGraphFieldSummary_,
                QString::fromUtf8("多物理场：status=%1，frame=%2，steps=%3，source=%4")
                    .arg(jsonString(*fieldJson, "status"),
                        jsonString(*fieldJson, "frame_id"))
                    .arg(static_cast<int>(steps.size()))
                    .arg(jsonString(*fieldJson, "source_trajectory_frame_id")));

            struct FieldAgg {
                QString subject;
                QString field;
                QString unit;
                int stepCount = 0;
                std::size_t valueCount = 0;
                ValueStats first;
                ValueStats last;
            };
            std::map<QString, FieldAgg> fields;
            for (int stepIndex = 0; stepIndex < static_cast<int>(steps.size()); ++stepIndex) {
                const auto tensors = steps[static_cast<std::size_t>(stepIndex)].value("fields", nlohmann::json::array());
                for (const auto& tensor : tensors) {
                    const QString subject = jsonString(tensor, "subject");
                    const QString field = jsonString(tensor, "field_kind");
                    const QString key = subject + QStringLiteral("/") + field;
                    auto& agg = fields[key];
                    agg.subject = subject;
                    agg.field = field;
                    agg.unit = jsonString(tensor, "unit");
                    agg.stepCount += 1;
                    const auto values = tensor.value("values", nlohmann::json::array());
                    agg.valueCount = std::max<std::size_t>(agg.valueCount, values.size());
                    for (const auto& value : values) {
                        if (!value.is_number()) {
                            continue;
                        }
                        if (stepIndex == 0) {
                            addStats(agg.first, value.get<double>());
                        }
                        if (stepIndex == static_cast<int>(steps.size()) - 1) {
                            addStats(agg.last, value.get<double>());
                        }
                    }
                }
            }
            for (const auto& item : fields) {
                const auto& agg = item.second;
                appendGraphRow(graphFieldTable_, {
                    agg.subject,
                    agg.field + (agg.unit.isEmpty() || agg.unit == QStringLiteral("-") ? QString() : QStringLiteral(" / ") + agg.unit),
                    QString::number(agg.stepCount),
                    QString::number(static_cast<qulonglong>(agg.valueCount)),
                    statsText(agg.first),
                    statsText(agg.last)
                });
            }
            const auto vtkSubject = selectedSubject(graphFieldSubjectCombo_, contracts::SubjectType::T);
            if (graphDtoVtkEnabled && graphFieldVtk_ && runtimeSnapshot.has_value()) {
                if (const auto bundle = latestFieldBundleForSubject(fieldFrame, vtkSubject)) {
                    const auto stats = graphFieldVtk_->renderFieldBundle(
                        bundle.value(),
                        vtkSubject,
                        0,
                        QString::fromUtf8("预测场 %1").arg(subjectText(vtkSubject)));
                    if (!stats.ok) {
                        appendGraphRow(graphFieldTable_, {
                            subjectText(vtkSubject),
                            QString::fromUtf8("VTK渲染"),
                            QStringLiteral("-"),
                            QStringLiteral("-"),
                            stats.message,
                            QStringLiteral("-")
                        });
                    }
                }
                else {
                    graphFieldVtk_->clearField(QString::fromUtf8("field.forecast 中没有 subject=%1 的场值")
                        .arg(subjectText(vtkSubject)));
                }
            }
        }
        else {
            setLabelText(lbGraphFieldSummary_, QString::fromUtf8("多物理场：%1").arg(parseError));
        }

        parseError.clear();
        if (const auto damageJson = parsePortPayload(view.result_ports, "damage.forecast", &parseError)) {
            const auto damageFrame = damageJson->get<contracts::DamageForecastFrame>();
            damageForecastForLife = damageFrame;
            const auto evidence = damageJson->value("diagnostic", nlohmann::json::object())
                .value("evidence", nlohmann::json::object());
            const auto initialVars = damageJson->value("initial_state", nlohmann::json::object())
                .value("variables", nlohmann::json::array());
            const double initialDamage = initialVars.empty()
                ? 0.0
                : jsonDouble(initialVars.front(), "cumulative_damage", 0.0);
            const double maxDamage = jsonDouble(evidence, "max_damage", 0.0);
            const double threshold = jsonDouble(evidence, "failure_threshold", 1.0);
            setLabelText(lbGraphDamageSummary_,
                QString::fromUtf8("损伤：status=%1，current=%2，max_future=%3，threshold=%4，mode=%5")
                    .arg(jsonString(*damageJson, "status"),
                        formatNumber(initialDamage),
                        formatNumber(maxDamage),
                        formatNumber(threshold),
                        jsonString(initialVars.empty() ? nlohmann::json::object() : initialVars.front(), "damage_mode")));

            const auto steps = damageJson->value("steps", nlohmann::json::array());
            const auto firstExceedance = damageJson->value("first_exceedance", nlohmann::json::object());
            std::vector<double> damageTrend;
            for (const auto& step : steps) {
                const auto increments = step.value("increments", nlohmann::json::array());
                ValueStats cumulativeStats;
                ValueStats incrementStats;
                for (const auto& increment : increments) {
                    addStats(cumulativeStats, jsonDouble(increment, "cumulative_value", std::numeric_limits<double>::quiet_NaN()));
                    addStats(incrementStats, jsonDouble(increment, "increment_value", std::numeric_limits<double>::quiet_NaN()));
                }
                appendGraphRow(graphDamageTable_, {
                    formatNumber(jsonDouble(step, "time_s", 0.0), 6),
                    QString::number(static_cast<int>(increments.size())),
                    cumulativeStats.count == 0 ? QStringLiteral("-") : formatNumber(cumulativeStats.max, 6),
                    incrementStats.count == 0 ? QStringLiteral("-") : formatNumber(incrementStats.max, 6),
                    jsonString(firstExceedance, "exceeded")
                });
                if (cumulativeStats.count > 0) {
                    damageTrend.push_back(std::clamp(cumulativeStats.max, 0.0, 1.0));
                }
            }
            if (graphDamageTrend_) {
                graphDamageTrend_->setSamples(damageTrend);
            }
            const auto vtkSubject = selectedSubject(graphDamageSubjectCombo_, contracts::SubjectType::T);
            if (graphDtoVtkEnabled && graphDamageVtk_ && runtimeSnapshot.has_value()) {
                if (const auto bundle = latestDamageFieldForSubject(damageFrame, vtkSubject)) {
                    const auto stats = graphDamageVtk_->renderFieldBundle(
                        bundle.value(),
                        vtkSubject,
                        0,
                        QString::fromUtf8("累计损伤 %1").arg(subjectText(vtkSubject)),
                        QStringLiteral("damage"));
                    if (!stats.ok) {
                        appendGraphRow(graphDamageTable_, {
                            QStringLiteral("-"),
                            QStringLiteral("-"),
                            QStringLiteral("-"),
                            QStringLiteral("-"),
                            stats.message
                        });
                    }
                }
                else {
                    graphDamageVtk_->clearField(QString::fromUtf8("damage.forecast 中没有 subject=%1 的累计损伤场")
                        .arg(subjectText(vtkSubject)));
                }
            }
        }
        else {
            setLabelText(lbGraphDamageSummary_, QString::fromUtf8("损伤：%1").arg(parseError));
            if (graphDamageTrend_) {
                graphDamageTrend_->clear();
            }
        }

        parseError.clear();
        if (const auto lifeJson = parsePortPayload(view.result_ports, "life.assessment", &parseError)) {
            const auto lifeFrame = lifeJson->get<contracts::LifeAssessmentFrame>();
            const auto health = lifeJson->value("health_state", nlohmann::json::object());
            const auto diagnostic = lifeJson->value("diagnostic", nlohmann::json::object());
            setLabelText(lbGraphLifeSummary_,
                QString::fromUtf8("寿命：status=%1，RUL=%2s，first_exceed=%3s，damage=%4，reason=%5")
                    .arg(jsonString(*lifeJson, "status"),
                        formatNumber(jsonDouble(*lifeJson, "rul_s", -1.0)),
                        formatNumber(jsonDouble(*lifeJson, "first_limit_exceedance_s", -1.0)),
                        formatNumber(jsonDouble(health, "damage_index", 0.0)),
                        jsonString(*lifeJson, "reason")));
            appendGraphRow(graphLifeTable_, {QString::fromUtf8("RUL/s"), formatNumber(jsonDouble(*lifeJson, "rul_s", -1.0), 7), jsonString(diagnostic, "message")});
            appendGraphRow(graphLifeTable_, {QString::fromUtf8("首超时间/s"), formatNumber(jsonDouble(*lifeJson, "first_limit_exceedance_s", -1.0), 7), jsonString(*lifeJson, "controlling_location_id")});
            appendGraphRow(graphLifeTable_, {QString::fromUtf8("当前损伤"), formatNumber(jsonDouble(health, "damage_index", 0.0), 7), jsonString(*lifeJson, "controlling_damage_mode")});
            appendGraphRow(graphLifeTable_, {QString::fromUtf8("评估方法"), jsonString(*lifeJson, "assessment_method"), jsonString(*lifeJson, "reason")});

            if (graphLifeTrend_) {
                std::vector<double> samples;
                if (damageForecastForLife.has_value()) {
                    samples.reserve(damageForecastForLife->steps.size());
                    for (const auto& step : damageForecastForLife->steps) {
                        samples.push_back(std::max(0.0, lifeFrame.rul_s - step.time_s));
                    }
                }
                if (samples.empty()) {
                    samples.push_back(std::max(0.0, lifeFrame.rul_s));
                }
                graphLifeTrend_->setSamples(samples);
            }

            const auto vtkSubject = selectedSubject(graphLifeSubjectCombo_, contracts::SubjectType::T);
            QString lifeFieldParseError;
            std::optional<contracts::FieldBundleDTO> lifeFieldFrame;
            if (const auto lifeFieldJson = parsePortPayload(view.result_ports, "life.field", &lifeFieldParseError)) {
                lifeFieldFrame = lifeFieldJson->get<contracts::FieldBundleDTO>();
            }
            if (graphDtoVtkEnabled && graphLifeVtk_ && runtimeSnapshot.has_value()) {
                if (lifeFieldFrame.has_value()) {
                    if (const auto bundle = fieldBundleForSubject(lifeFieldFrame.value(), vtkSubject)) {
                        const auto stats = graphLifeVtk_->renderFieldBundle(
                            bundle.value(),
                            vtkSubject,
                            0,
                            QString::fromUtf8("剩余寿命场 %1").arg(subjectText(vtkSubject)),
                            QStringLiteral("s"));
                        appendGraphRow(graphLifeTable_, {
                            QString::fromUtf8("寿命场来源"),
                            QString::fromUtf8("life.field"),
                            stats.ok ? QString::fromUtf8("节点级 RUL 场，单位 s") : stats.message
                        });
                    }
                    else {
                        graphLifeVtk_->clearField(QString::fromUtf8("life.field 中没有 subject=%1 的寿命场")
                            .arg(subjectText(vtkSubject)));
                    }
                }
                else if (damageForecastForLife.has_value()) {
                    appendGraphRow(graphLifeTable_, {
                        QString::fromUtf8("life.field"),
                        QString::fromUtf8("缺失/解析失败"),
                        lifeFieldParseError.isEmpty() ? QString::fromUtf8("兼容旧 evidence 派生显示") : lifeFieldParseError
                    });
                    if (const auto bundle = deriveLifeFieldForSubject(damageForecastForLife.value(), lifeFrame, vtkSubject)) {
                        const auto stats = graphLifeVtk_->renderFieldBundle(
                            bundle.value(),
                            vtkSubject,
                            0,
                            QString::fromUtf8("剩余寿命场 %1").arg(subjectText(vtkSubject)),
                            lifeFrame.rul_s > 0.0 ? QStringLiteral("s") : QStringLiteral("fraction"));
                        appendGraphRow(graphLifeTable_, {
                            QString::fromUtf8("寿命场来源"),
                            QString::fromUtf8("damage.forecast -> life.assessment 派生"),
                            stats.ok ? QString::fromUtf8("兼容旧 evidence；正式链路应输出 life.field") : stats.message
                        });
                    }
                    else {
                        graphLifeVtk_->clearField(QString::fromUtf8("无法从 damage.forecast 派生 subject=%1 的寿命场")
                            .arg(subjectText(vtkSubject)));
                    }
                }
                else {
                    graphLifeVtk_->clearField(lifeFieldParseError.isEmpty()
                        ? QString::fromUtf8("缺少 life.field：无法显示寿命场")
                        : lifeFieldParseError);
                }
            }
        }
        else {
            setLabelText(lbGraphLifeSummary_, QString::fromUtf8("寿命：%1").arg(parseError));
            if (graphLifeTrend_) {
                graphLifeTrend_->clear();
            }
        }

        if (graphEvidenceText_) {
            QStringList lines;
            lines << QString::fromUtf8("graph_run_evidence = %1")
                .arg(QString::fromStdWString(view.graph_run_evidence_path.wstring()));
            lines << QString::fromUtf8("graph_snapshot = %1")
                .arg(QString::fromStdWString(view.graph_snapshot_path.wstring()));
            lines << QString::fromUtf8("operator_snapshot = %1")
                .arg(QString::fromStdWString(view.operator_snapshot_path.wstring()));
            lines << QString::fromUtf8("resource_lock = %1")
                .arg(QString::fromStdWString(view.resource_lock_path.wstring()));
            lines << QString::fromUtf8("graph_outputs = %1")
                .arg(QString::fromStdWString(view.graph_outputs_path.wstring()));
            lines << QString::fromUtf8("runtime_snapshot = %1 (%2)")
                .arg(QString::fromStdWString(view.runtime_snapshot_path.wstring()),
                    view.has_runtime_snapshot ? QString::fromUtf8("exists") : QString::fromUtf8("missing"));
            lines << QString::fromUtf8("workflow_timeline = %1 (%2)")
                .arg(QString::fromStdWString(view.workflow_timeline_path.wstring()),
                    view.has_workflow_timeline ? QString::fromUtf8("exists") : QString::fromUtf8("missing"));
            if (workflowTimeline && workflowTimeline->is_object()) {
                lines << QString::fromUtf8("timeline_counts = observed:%1 filter:%2 predict:%3 trajectory_steps:%4 field_steps:%5 damage_steps:%6 life_nodes:%7")
                    .arg(static_cast<qlonglong>(jsonDouble(*workflowTimeline, "observed_frame_count", 0.0)))
                    .arg(static_cast<qlonglong>(jsonDouble(*workflowTimeline, "filter_update_count", 0.0)))
                    .arg(static_cast<qlonglong>(jsonDouble(*workflowTimeline, "prediction_run_count", 0.0)))
                    .arg(static_cast<qlonglong>(jsonDouble(*workflowTimeline, "trajectory_step_count_total", 0.0)))
                    .arg(static_cast<qlonglong>(jsonDouble(*workflowTimeline, "field_step_count_total", 0.0)))
                    .arg(static_cast<qlonglong>(jsonDouble(*workflowTimeline, "damage_step_count_total", 0.0)))
                    .arg(static_cast<qlonglong>(jsonDouble(*workflowTimeline, "life_field_node_value_count", 0.0)));
            }
            lines << QStringLiteral("");
            lines << QString::fromUtf8("workflow path:");
            for (const auto& node : view.graph_nodes) {
                lines << QString::fromUtf8("  - %1 [%2] %3 -> %4")
                    .arg(QString::fromUtf8(node.node_id.c_str()),
                        QString::fromUtf8(node.operator_type.c_str()),
                        joinStdStrings(node.inputs, 8),
                        joinStdStrings(node.outputs, 8));
            }
            lines << QStringLiteral("");
            lines << QString::fromUtf8("result ports:");
            for (const auto& port : view.result_ports) {
                lines << QString::fromUtf8("  - %1 (%2 bytes)")
                    .arg(QString::fromUtf8(port.port_name.c_str()))
                    .arg(static_cast<qulonglong>(port.payload_json.size()));
            }
            graphEvidenceText_->setPlainText(lines.join(QStringLiteral("\n")));
        }
    }
    catch (const std::exception& e) {
        setLabelText(lbGraphWorkflowSummary_,
            QString::fromUtf8("完整链路读取失败：%1").arg(QString::fromUtf8(e.what())));
    }
}

// 弹窗实现（占位）
