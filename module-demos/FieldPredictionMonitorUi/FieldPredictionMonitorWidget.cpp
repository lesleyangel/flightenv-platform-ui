#include "FieldPredictionMonitorWidget.h"
#include "../common/RosJsonSubscriber.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFont>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>
#include <QPainter>
#include <QPen>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <numeric>
#include <utility>

namespace {

contracts::TimestampNs nowNs()
{
    return static_cast<contracts::TimestampNs>(QDateTime::currentMSecsSinceEpoch()) * 1000000;
}

QTableWidgetItem* item(const QString& text)
{
    auto* tableItem = new QTableWidgetItem(text);
    tableItem->setFlags(tableItem->flags() & ~Qt::ItemIsEditable);
    tableItem->setTextAlignment(Qt::AlignCenter);
    return tableItem;
}

void addRow(QFormLayout* form, const QString& label, QWidget* field)
{
    auto* caption = new QLabel(label);
    caption->setMinimumWidth(108);
    form->addRow(caption, field);
}

QString fixed(double value, int precision)
{
    return std::isfinite(value) ? QString::number(value, 'f', precision) : QStringLiteral("--");
}

using json = nlohmann::json;

contracts::SubjectType subjectFromCombo(const QComboBox* combo)
{
    if (combo == nullptr) {
        return contracts::SubjectType::P;
    }
    return contracts::subject_type_from_string(combo->currentData().toString().toStdString());
}

} // namespace

FieldPredictionMonitorWidget::FieldPredictionMonitorWidget(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
    connectUi();

    demoTimer_ = new QTimer(this);
    freshnessTimer_ = new QTimer(this);
    connect(demoTimer_, &QTimer::timeout, this, [this]() { onDemoTick(); });
    connect(freshnessTimer_, &QTimer::timeout, this, [this]() { updateFreshness(); });
    freshnessTimer_->start(250);
    reconnectRos();
    if (simulationCheck_->isChecked()) {
        startDemo();
    } else {
        updateFreshness();
    }
}

FieldPredictionMonitorWidget::~FieldPredictionMonitorWidget() = default;

void FieldPredictionMonitorWidget::buildUi()
{
    setWindowTitle(QStringLiteral("场预测模块示意 UI"));
    setStyleSheet(QStringLiteral(
        "QWidget{background:#f5f7fa;color:#1f2933;font-family:'Microsoft YaHei UI','Segoe UI';font-size:10pt;}"
        "QGroupBox{background:white;border:1px solid #d8e0e8;border-radius:6px;margin-top:14px;padding:10px;font-weight:600;}"
        "QGroupBox::title{subcontrol-origin:margin;left:10px;padding:0 6px;color:#27445c;}"
        "QLineEdit,QSpinBox,QDoubleSpinBox{background:white;border:1px solid #c9d4df;border-radius:4px;padding:5px 6px;}"
        "QTableWidget{background:white;border:1px solid #d8e0e8;gridline-color:#e5ebf1;}"
        "QHeaderView::section{background:#edf3f8;border:0;border-right:1px solid #d8e0e8;padding:6px;font-weight:600;}"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(18, 16, 18, 18);
    root->setSpacing(12);

    auto* titleRow = new QHBoxLayout();
    auto* title = new QLabel(QStringLiteral("场预测模块示意 UI"));
    QFont titleFont = title->font();
    titleFont.setPointSize(18);
    titleFont.setBold(true);
    title->setFont(titleFont);
    statusBadge_ = makeValueLabel(QStringLiteral("初始化"));
    statusBadge_->setAlignment(Qt::AlignCenter);
    statusBadge_->setMinimumWidth(150);
    titleRow->addWidget(title);
    titleRow->addStretch();
    titleRow->addWidget(statusBadge_);
    root->addLayout(titleRow);

    auto* subtitle = new QLabel(QStringLiteral("本界面订阅 /flightenv/field_prediction 的真实 JSON DTO；本地 baseline 仅作为无节点时 fallback。"));
    subtitle->setStyleSheet(QStringLiteral("color:#5d6b78;"));
    root->addWidget(subtitle);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);
    root->addWidget(splitter, 1);

    auto* left = new QWidget(splitter);
    auto* leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 8, 0);
    leftLayout->setSpacing(10);

    auto* cloudGroup = new QGroupBox(QStringLiteral("飞船模型 VTK 预测场"));
    auto* cloudLayout = new QVBoxLayout(cloudGroup);
    auto* layerRow = new QHBoxLayout();
    layerRow->addWidget(new QLabel(QStringLiteral("subject")));
    subjectCombo_ = new QComboBox(cloudGroup);
    subjectCombo_->addItem(QStringLiteral("P / 压力"), QStringLiteral("P"));
    subjectCombo_->addItem(QStringLiteral("K / 热流"), QStringLiteral("K"));
    subjectCombo_->addItem(QStringLiteral("S / 应力"), QStringLiteral("S"));
    subjectCombo_->addItem(QStringLiteral("T / 温度"), QStringLiteral("T"));
    subjectCombo_->setCurrentIndex(3);
    layerRow->addWidget(subjectCombo_, 1);
    layerRow->addWidget(new QLabel(QStringLiteral("component")));
    componentSpin_ = new QSpinBox(cloudGroup);
    componentSpin_->setRange(0, 8);
    componentSpin_->setValue(0);
    layerRow->addWidget(componentSpin_);
    cloudLayout->addLayout(layerRow);
    vtkField_ = new flightenv::ui::demo::VtkModelFieldWidget(cloudGroup);
    cloudLayout->addWidget(vtkField_, 1);
    leftLayout->addWidget(cloudGroup, 1);

    auto* tableGroup = new QGroupBox(QStringLiteral("FieldSummaryDTO"));
    auto* tableLayout = new QVBoxLayout(tableGroup);
    summaryTable_ = new QTableWidget(0, 6, tableGroup);
    summaryTable_->setHorizontalHeaderLabels({
        QStringLiteral("field_id"),
        QStringLiteral("quantity"),
        QStringLiteral("min"),
        QStringLiteral("mean"),
        QStringLiteral("max"),
        QStringLiteral("status")
    });
    summaryTable_->verticalHeader()->setVisible(false);
    summaryTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    summaryTable_->horizontalHeader()->setStretchLastSection(true);
    tableLayout->addWidget(summaryTable_);
    leftLayout->addWidget(tableGroup);

    auto* historyGroup = new QGroupBox(QStringLiteral("历史预测场"));
    auto* historyLayout = new QVBoxLayout(historyGroup);
    historyTable_ = new QTableWidget(0, 6, historyGroup);
    historyTable_->setHorizontalHeaderLabels({
        QStringLiteral("frame_id"),
        QStringLiteral("source"),
        QStringLiteral("status"),
        QStringLiteral("layers"),
        QStringLiteral("peak"),
        QStringLiteral("model_id")
    });
    historyTable_->verticalHeader()->setVisible(false);
    historyTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    historyTable_->horizontalHeader()->setStretchLastSection(true);
    historyLayout->addWidget(historyTable_);
    leftLayout->addWidget(historyGroup);

    auto* right = new QWidget(splitter);
    right->setMinimumWidth(350);
    auto* rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(8, 0, 0, 0);
    rightLayout->setSpacing(10);

    auto* configGroup = new QGroupBox(QStringLiteral("配置区"));
    auto* form = new QFormLayout(configGroup);
    modelIdEdit_ = new QLineEdit(QStringLiteral("field-baseline-grid-demo"));
    assetIdEdit_ = new QLineEdit(QStringLiteral("thermal-panel-A01"));
    inputTopicEdit_ = new QLineEdit(QStringLiteral("/flightenv/trajectory_prediction"));
    outputTopicEdit_ = new QLineEdit(QStringLiteral("/flightenv/field_prediction"));
    runtimeSnapshotTopicEdit_ = new QLineEdit(QStringLiteral("/flightenv/runtime_snapshot"));
    predictionResultTopicEdit_ = new QLineEdit(QStringLiteral("/flightenv/prediction_result"));
    assetRootEdit_ = new QLineEdit(QStringLiteral("F:/code/FlightEnvMultiRepo/_deps/example"));
    algorithmEdit_ = new QLineEdit(QStringLiteral("altitude_time_decay_baseline"));
    gridSizeSpin_ = new QSpinBox();
    gridSizeSpin_->setRange(8, 96);
    gridSizeSpin_->setValue(28);
    updateRateSpin_ = new QDoubleSpinBox();
    updateRateSpin_->setRange(0.2, 10.0);
    updateRateSpin_->setDecimals(1);
    updateRateSpin_->setValue(1.0);
    updateRateSpin_->setSuffix(QStringLiteral(" Hz"));
    simulationCheck_ = new QCheckBox(QStringLiteral("无实时数据时启用本地 baseline 源"));
    simulationCheck_->setChecked(false);
    addRow(form, QStringLiteral("model_type"), new QLabel(QStringLiteral("field_prediction")));
    addRow(form, QStringLiteral("model_id"), modelIdEdit_);
    addRow(form, QStringLiteral("asset_id"), assetIdEdit_);
    addRow(form, QStringLiteral("input_topic"), inputTopicEdit_);
    addRow(form, QStringLiteral("output_topic"), outputTopicEdit_);
    addRow(form, QStringLiteral("runtime_snapshot"), runtimeSnapshotTopicEdit_);
    addRow(form, QStringLiteral("prediction_result"), predictionResultTopicEdit_);
    addRow(form, QStringLiteral("asset_root"), assetRootEdit_);
    addRow(form, QStringLiteral("algorithm"), algorithmEdit_);
    addRow(form, QStringLiteral("grid_size"), gridSizeSpin_);
    addRow(form, QStringLiteral("update_rate"), updateRateSpin_);
    addRow(form, QStringLiteral("数据源"), simulationCheck_);
    rightLayout->addWidget(configGroup);

    auto* statusGroup = new QGroupBox(QStringLiteral("运行状态"));
    auto* statusForm = new QFormLayout(statusGroup);
    frameIdValue_ = makeValueLabel();
    sourceFrameValue_ = makeValueLabel();
    maxValue_ = makeValueLabel();
    meanValue_ = makeValueLabel();
    freshnessValue_ = makeValueLabel();
    gridValue_ = makeValueLabel();
    addRow(statusForm, QStringLiteral("frame_id"), frameIdValue_);
    addRow(statusForm, QStringLiteral("source"), sourceFrameValue_);
    addRow(statusForm, QStringLiteral("max"), maxValue_);
    addRow(statusForm, QStringLiteral("mean"), meanValue_);
    addRow(statusForm, QStringLiteral("freshness"), freshnessValue_);
    addRow(statusForm, QStringLiteral("grid"), gridValue_);
    rightLayout->addWidget(statusGroup);

    auto* boundary = new QLabel(QStringLiteral(
        "边界说明：本 demo 通过 ROS2 std_msgs/String 订阅 FieldPredictionFrame JSON；baseline 只作 fallback，无 runtime-private、Core、DB 或 source-supported 依赖。"));
    boundary->setWordWrap(true);
    boundary->setStyleSheet(QStringLiteral("background:#eef6f3;border:1px solid #c8ded6;border-radius:6px;padding:10px;color:#2f5a4c;"));
    rightLayout->addWidget(boundary);
    rightLayout->addStretch();

    splitter->addWidget(left);
    splitter->addWidget(right);
    splitter->setStretchFactor(0, 1);
}

void FieldPredictionMonitorWidget::connectUi()
{
    connect(simulationCheck_, &QCheckBox::toggled, this, [this](bool checked) {
        checked ? startDemo() : stopDemo();
    });
    connect(updateRateSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) {
        if (simulationCheck_->isChecked()) {
            startDemo();
        }
    });
    connect(outputTopicEdit_, &QLineEdit::editingFinished, this, [this]() { reconnectRos(); });
    connect(runtimeSnapshotTopicEdit_, &QLineEdit::editingFinished, this, [this]() { reconnectRos(); });
    connect(predictionResultTopicEdit_, &QLineEdit::editingFinished, this, [this]() { reconnectRos(); });
    connect(assetRootEdit_, &QLineEdit::editingFinished, this, [this]() {
        if (vtkField_) {
            vtkField_->setAssetRoot(assetRootEdit_->text().trimmed());
        }
    });
    connect(subjectCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        if (latestPredictionResult_ && vtkField_) {
            const auto stats = vtkField_->renderPredictionResult(*latestPredictionResult_,
                                                                 subjectFromCombo(subjectCombo_),
                                                                 componentSpin_->value(),
                                                                 QStringLiteral("预测场"));
            maxValue_->setText(stats.ok ? QString::number(stats.maxValue, 'g', 6) : stats.message);
        }
    });
    connect(componentSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
        if (latestPredictionResult_ && vtkField_) {
            const auto stats = vtkField_->renderPredictionResult(*latestPredictionResult_,
                                                                 subjectFromCombo(subjectCombo_),
                                                                 componentSpin_->value(),
                                                                 QStringLiteral("预测场"));
            maxValue_->setText(stats.ok ? QString::number(stats.maxValue, 'g', 6) : stats.message);
        }
    });
}

void FieldPredictionMonitorWidget::startDemo()
{
    const auto config = readConfig();
    const int intervalMs = std::max(80, static_cast<int>(1000.0 / std::max(0.1, config.update_rate_hz)));
    demoTimer_->start(intervalMs);
    if (!freshnessClock_.isValid()) {
        onDemoTick();
    }
}

void FieldPredictionMonitorWidget::stopDemo()
{
    demoTimer_->stop();
    setStatusBadge(QStringLiteral("等待 ROS"), QStringLiteral("#7a8794"));
}

void FieldPredictionMonitorWidget::reconnectRos()
{
    ros_ = std::make_unique<flightenv::ui::demo::RosJsonSubscriber>("field_prediction_monitor_ui");
    if (!ros_->ok()) {
        rosError_ = QString::fromStdString(ros_->error());
        setStatusBadge(QStringLiteral("ROS 未就绪"), QStringLiteral("#b42318"));
        sourceFrameValue_->setText(rosError_);
        return;
    }

    const auto config = readConfig();
    if (vtkField_) {
        vtkField_->setAssetRoot(config.asset_root);
    }
    rosError_.clear();
    ros_->subscribe(config.output_topic.toStdString(), [this](const std::string& topic, const std::string& payload) {
        onFieldPayload(QString::fromStdString(topic), QString::fromStdString(payload));
    });
    ros_->subscribe(config.runtime_snapshot_topic.toStdString(), [this](const std::string& topic, const std::string& payload) {
        onRuntimeSnapshotPayload(QString::fromStdString(topic), QString::fromStdString(payload));
    });
    ros_->subscribe(config.prediction_result_topic.toStdString(), [this](const std::string& topic, const std::string& payload) {
        onPredictionResultPayload(QString::fromStdString(topic), QString::fromStdString(payload));
    });
    setStatusBadge(QStringLiteral("等待 ROS"), QStringLiteral("#7a8794"));
    sourceFrameValue_->setText(QStringLiteral("订阅: %1").arg(config.output_topic));
}

void FieldPredictionMonitorWidget::onFieldPayload(const QString& topic, const QString& payload)
{
    QMetaObject::invokeMethod(this, [this, topic, payload]() {
        try {
            const auto frame = json::parse(payload.toStdString()).get<contracts::FieldPredictionFrame>();
            updateFrame(frame, {});
            sourceFrameValue_->setText(QStringLiteral("%1 / %2").arg(topic, QString::fromStdString(frame.source_frame_id)));
            setStatusBadge(QStringLiteral("ROS 实时"), QStringLiteral("#25835f"));
        } catch (const std::exception& ex) {
            rosError_ = QStringLiteral("场预测 JSON 解析失败: %1").arg(QString::fromLocal8Bit(ex.what()));
            setStatusBadge(QStringLiteral("解析失败"), QStringLiteral("#b42318"));
            sourceFrameValue_->setText(rosError_);
        }
    }, Qt::QueuedConnection);
}

void FieldPredictionMonitorWidget::onRuntimeSnapshotPayload(const QString& topic, const QString& payload)
{
    QMetaObject::invokeMethod(this, [this, topic, payload]() {
        try {
            runtimeSnapshot_ = json::parse(payload.toStdString()).get<contracts::RuntimeSnapshotDTO>();
            if (vtkField_) {
                vtkField_->setAssetRoot(readConfig().asset_root);
                vtkField_->setRuntimeSnapshot(*runtimeSnapshot_);
            }
            sourceFrameValue_->setText(QStringLiteral("%1 / runtime snapshot: fields=%2 meshes=%3")
                                           .arg(topic)
                                           .arg(runtimeSnapshot_->field_layouts.size())
                                           .arg(runtimeSnapshot_->meshes.size()));
            setStatusBadge(QStringLiteral("模型布局已接收"), QStringLiteral("#25835f"));
        } catch (const std::exception& ex) {
            rosError_ = QStringLiteral("RuntimeSnapshotDTO JSON 解析失败: %1").arg(QString::fromLocal8Bit(ex.what()));
            setStatusBadge(QStringLiteral("布局解析失败"), QStringLiteral("#b42318"));
            sourceFrameValue_->setText(rosError_);
        }
    }, Qt::QueuedConnection);
}

void FieldPredictionMonitorWidget::onPredictionResultPayload(const QString& topic, const QString& payload)
{
    QMetaObject::invokeMethod(this, [this, topic, payload]() {
        try {
            latestPredictionResult_ = json::parse(payload.toStdString()).get<contracts::PredictionResultDTO>();
            if (!runtimeSnapshot_) {
                sourceFrameValue_->setText(QStringLiteral("%1 / 已收到 PredictionResultDTO，但缺少 RuntimeSnapshotDTO，不能画 VTK 场").arg(topic));
                return;
            }
            if (vtkField_) {
                const auto stats = vtkField_->renderPredictionResult(*latestPredictionResult_,
                                                                     subjectFromCombo(subjectCombo_),
                                                                     componentSpin_->value(),
                                                                     QStringLiteral("预测场"));
                if (stats.ok) {
                    maxValue_->setText(QStringLiteral("%1").arg(stats.maxValue, 0, 'g', 6));
                    meanValue_->setText(QStringLiteral("%1").arg(stats.meanValue, 0, 'g', 6));
                    gridValue_->setText(QStringLiteral("nodes=%1 vertices=%2").arg(stats.nodeCount).arg(stats.vertexCount));
                    sourceFrameValue_->setText(QStringLiteral("%1 / max_node=%2 min_node=%3")
                                                   .arg(topic)
                                                   .arg(stats.maxNodeIndex)
                                                   .arg(stats.minNodeIndex));
                    setStatusBadge(QStringLiteral("VTK 场实时"), QStringLiteral("#25835f"));
                } else {
                    sourceFrameValue_->setText(stats.message);
                    setStatusBadge(QStringLiteral("场值不匹配"), QStringLiteral("#b54708"));
                }
            }
        } catch (const std::exception& ex) {
            rosError_ = QStringLiteral("PredictionResultDTO JSON 解析失败: %1").arg(QString::fromLocal8Bit(ex.what()));
            setStatusBadge(QStringLiteral("场解析失败"), QStringLiteral("#b42318"));
            sourceFrameValue_->setText(rosError_);
        }
    }, Qt::QueuedConnection);
}

void FieldPredictionMonitorWidget::onDemoTick()
{
    QVector<double> grid;
    const auto frame = makeFrame(readConfig(), &grid);
    updateFrame(frame, grid);
}

void FieldPredictionMonitorWidget::updateFreshness()
{
    if (!freshnessClock_.isValid()) {
        freshnessValue_->setText(QStringLiteral("无样本"));
        if (!simulationCheck_->isChecked()) {
            setStatusBadge(rosError_.isEmpty() ? QStringLiteral("等待 ROS") : QStringLiteral("ROS 未就绪"),
                           rosError_.isEmpty() ? QStringLiteral("#7a8794") : QStringLiteral("#b42318"));
        }
        return;
    }
    const auto ageMs = freshnessClock_.elapsed();
    freshnessValue_->setText(QStringLiteral("%1 ms").arg(ageMs));
    if (simulationCheck_->isChecked() && ageMs < 2000) {
        setStatusBadge(QStringLiteral("fallback / 运行中"), QStringLiteral("#25835f"));
    } else if (simulationCheck_->isChecked()) {
        setStatusBadge(QStringLiteral("stale / 超时"), QStringLiteral("#bd6b28"));
    }
}

void FieldPredictionMonitorWidget::updateFrame(const contracts::FieldPredictionFrame& frame, const QVector<double>& grid)
{
    Q_UNUSED(grid);
    double minValue = 0.0;
    double maxValue = 1.0;
    double meanValue = 0.0;
    bool found = false;
    for (const auto& field : frame.fields) {
        if (field.status != "ok") {
            continue;
        }
        if (!found) {
            minValue = field.min_value;
            maxValue = field.max_value;
            meanValue = field.mean_value;
            found = true;
        } else {
            minValue = std::min(minValue, field.min_value);
            maxValue = std::max(maxValue, field.max_value);
            meanValue = (meanValue + field.mean_value) * 0.5;
        }
    }

    freshnessClock_.restart();
    frameIdValue_->setText(QString::fromStdString(frame.frame_id));
    sourceFrameValue_->setText(QString::fromStdString(frame.source_frame_id));
    maxValue_->setText(found ? fixed(maxValue, 1) : QStringLiteral("--"));
    meanValue_->setText(found ? fixed(meanValue, 1) : QStringLiteral("--"));
    gridValue_->setText(QStringLiteral("摘要帧不含节点场；VTK 等待 PredictionResultDTO"));
    history_.push_front(frame);
    while (history_.size() > 80) {
        history_.pop_back();
    }
    updateSummaryTable(frame);
    updateHistoryTable();
    updateFreshness();
}

void FieldPredictionMonitorWidget::updateSummaryTable(const contracts::FieldPredictionFrame& frame)
{
    summaryTable_->setRowCount(static_cast<int>(frame.fields.size()));
    for (int row = 0; row < static_cast<int>(frame.fields.size()); ++row) {
        const auto& field = frame.fields[static_cast<size_t>(row)];
        summaryTable_->setItem(row, 0, item(QString::fromStdString(field.field_id)));
        summaryTable_->setItem(row, 1, item(QString::fromStdString(field.quantity)));
        summaryTable_->setItem(row, 2, item(fixed(field.min_value, 1)));
        summaryTable_->setItem(row, 3, item(fixed(field.mean_value, 1)));
        summaryTable_->setItem(row, 4, item(fixed(field.max_value, 1)));
        summaryTable_->setItem(row, 5, item(QString::fromStdString(field.status)));
    }
    summaryTable_->resizeColumnsToContents();
}

void FieldPredictionMonitorWidget::updateHistoryTable()
{
    historyTable_->setRowCount(static_cast<int>(history_.size()));
    for (int row = 0; row < static_cast<int>(history_.size()); ++row) {
        const auto& frame = history_[static_cast<size_t>(row)];
        const auto peakIt = std::max_element(
            frame.fields.begin(),
            frame.fields.end(),
            [](const contracts::FieldSummaryDTO& left, const contracts::FieldSummaryDTO& right) {
                return left.max_value < right.max_value;
            });
        const QString peakText = peakIt == frame.fields.end()
            ? QStringLiteral("--")
            : QStringLiteral("%1 %2").arg(fixed(peakIt->max_value, 1), QString::fromStdString(peakIt->unit));
        historyTable_->setItem(row, 0, item(QString::fromStdString(frame.frame_id)));
        historyTable_->setItem(row, 1, item(QString::fromStdString(frame.source_frame_id)));
        historyTable_->setItem(row, 2, item(QString::fromStdString(frame.status)));
        historyTable_->setItem(row, 3, item(QString::number(frame.fields.size())));
        historyTable_->setItem(row, 4, item(peakText));
        historyTable_->setItem(row, 5, item(QString::fromStdString(frame.model_snapshot.model_id)));
    }
    historyTable_->resizeColumnsToContents();
}

FieldPredictionDemoConfig FieldPredictionMonitorWidget::readConfig() const
{
    FieldPredictionDemoConfig config;
    config.model_id = modelIdEdit_->text().trimmed();
    config.asset_id = assetIdEdit_->text().trimmed();
    config.input_topic = inputTopicEdit_->text().trimmed();
    config.output_topic = outputTopicEdit_->text().trimmed();
    config.runtime_snapshot_topic = runtimeSnapshotTopicEdit_->text().trimmed();
    config.prediction_result_topic = predictionResultTopicEdit_->text().trimmed();
    config.asset_root = assetRootEdit_->text().trimmed();
    config.algorithm = algorithmEdit_->text().trimmed();
    config.grid_size = gridSizeSpin_->value();
    config.update_rate_hz = updateRateSpin_->value();
    return config;
}

contracts::FieldPredictionFrame FieldPredictionMonitorWidget::makeFrame(const FieldPredictionDemoConfig& config, QVector<double>* grid)
{
    ++frameCounter_;
    simTime_s_ += 1.0 / std::max(0.1, config.update_rate_hz);

    const int n = config.grid_size;
    grid->clear();
    grid->reserve(n * n);

    double minValue = std::numeric_limits<double>::max();
    double maxValue = std::numeric_limits<double>::lowest();
    double sum = 0.0;
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            const double nx = (static_cast<double>(x) / std::max(1, n - 1) - 0.5) * 2.0;
            const double ny = (static_cast<double>(y) / std::max(1, n - 1) - 0.5) * 2.0;
            const double hotSpot = std::exp(-(nx * nx * 4.0 + ny * ny * 8.0));
            const double sweep = 0.5 + 0.5 * std::sin(simTime_s_ * 0.55 + nx * 2.2);
            const double value = 650.0 + hotSpot * 820.0 + sweep * 160.0 - ny * 85.0;
            grid->push_back(value);
            minValue = std::min(minValue, value);
            maxValue = std::max(maxValue, value);
            sum += value;
        }
    }
    const double meanValue = grid->isEmpty() ? 0.0 : sum / grid->size();

    contracts::FieldPredictionFrame frame;
    frame.frame_id = QStringLiteral("field-%1").arg(frameCounter_, 5, 10, QChar('0')).toStdString();
    frame.run_id = "field-prediction-monitor-ui-demo";
    frame.object_id = config.asset_id.toStdString();
    frame.source_frame_id = QStringLiteral("trajectory-%1").arg(frameCounter_, 5, 10, QChar('0')).toStdString();
    frame.stamp_ns = nowNs();
    frame.source_stamp_ns = frame.stamp_ns;
    frame.status = "simulated";
    frame.reason = "local baseline field grid for UI demo";
    frame.model_snapshot.model_id = config.model_id.toStdString();
    frame.model_snapshot.model_type = "field_prediction";
    frame.model_snapshot.version = "local-demo";
    frame.model_snapshot.artifact_uri = "local://FieldPredictionMonitorUi/baseline";

    contracts::FieldSummaryDTO temp;
    temp.field_id = "surface_temperature_demo";
    temp.quantity = "temperature";
    temp.unit = "K";
    temp.min_value = minValue;
    temp.mean_value = meanValue;
    temp.max_value = maxValue;
    temp.status = "ok";
    temp.reason = config.algorithm.toStdString();
    frame.fields.push_back(temp);

    contracts::FieldSummaryDTO heatFlux;
    heatFlux.field_id = "heat_flux_demo";
    heatFlux.quantity = "heat_flux";
    heatFlux.unit = "kW/m2";
    heatFlux.min_value = minValue * 0.015;
    heatFlux.mean_value = meanValue * 0.015;
    heatFlux.max_value = maxValue * 0.015;
    heatFlux.status = "ok";
    heatFlux.reason = "derived from demo temperature grid";
    frame.fields.push_back(heatFlux);
    return frame;
}

QLabel* FieldPredictionMonitorWidget::makeValueLabel(const QString& text) const
{
    auto* label = new QLabel(text);
    label->setMinimumHeight(28);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setStyleSheet(QStringLiteral("background:#f8fafc;border:1px solid #d9e2ea;border-radius:4px;padding:4px 7px;font-weight:500;"));
    return label;
}

void FieldPredictionMonitorWidget::setStatusBadge(const QString& text, const QString& color)
{
    statusBadge_->setText(text);
    statusBadge_->setStyleSheet(QStringLiteral("background:%1;color:white;border-radius:14px;padding:5px 12px;font-weight:600;").arg(color));
}
