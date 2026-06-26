#include "DamageAssessmentMonitorWidget.h"
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
#include <QPainterPath>
#include <QPen>
#include <QProgressBar>
#include <QSizePolicy>
#include <QSplitter>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace {

constexpr int kMaxSamples = 120;
constexpr double kTemperatureLimitK = 1200.0;
constexpr double kStressLimitMpa = 250.0;
constexpr double kHeatFluxLimitKwM2 = 800.0;
using json = nlohmann::json;

contracts::SubjectType subjectFromCombo(const QComboBox* combo)
{
    if (combo == nullptr) {
        return contracts::SubjectType::T;
    }
    return contracts::subject_type_from_string(combo->currentData().toString().toStdString());
}

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

double clampedDamage01(double value)
{
    if (!std::isfinite(value)) {
        return 0.0;
    }
    return std::clamp(value, 0.0, 1.0);
}

double threshold01(double threshold)
{
    return std::clamp(std::isfinite(threshold) ? threshold : 1.0, 0.01, 1.0);
}

double evidenceNumber(const contracts::DamageAssessmentFrame& frame,
                      std::initializer_list<const char*> keys,
                      double fallback)
{
    const auto& evidence = frame.diagnostic.evidence;
    if (evidence.is_object()) {
        for (const char* key : keys) {
            if (evidence.contains(key) && evidence.at(key).is_number()) {
                return evidence.at(key).get<double>();
            }
        }
    }
    return fallback;
}

QString criterionStatus(double ratio)
{
    if (!std::isfinite(ratio)) {
        return QStringLiteral("unknown");
    }
    if (ratio >= 1.0) {
        return QStringLiteral("exceeded");
    }
    if (ratio >= 0.8) {
        return QStringLiteral("warning");
    }
    return QStringLiteral("ok");
}

} // namespace

DamageCurveWidget::DamageCurveWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(260);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void DamageCurveWidget::setSamples(QVector<double> samples, double threshold)
{
    samples_.clear();
    samples_.reserve(samples.size());
    for (const double sample : samples) {
        samples_.push_back(clampedDamage01(sample));
    }
    threshold_ = threshold01(threshold);
    update();
}

void DamageCurveWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor(248, 250, 252));

    const QRectF outer = rect().adjusted(14, 14, -14, -14);
    painter.setPen(QPen(QColor(207, 217, 227), 1));
    painter.setBrush(QColor(255, 255, 255));
    painter.drawRoundedRect(outer, 6, 6);

    const QRectF plot = outer.adjusted(56, 36, -24, -44);
    painter.setPen(QPen(QColor(224, 231, 238), 1));
    painter.drawRect(plot);
    painter.setPen(QColor(37, 52, 66));
    painter.drawText(QRectF(plot.left(), outer.top() + 8, plot.width(), 24),
                     Qt::AlignCenter,
                     QStringLiteral("损伤累计趋势"));

    if (samples_.isEmpty()) {
        painter.setPen(QColor(103, 116, 130));
        painter.drawText(plot, Qt::AlignCenter, QStringLiteral("等待损伤帧"));
        return;
    }

    constexpr double yMax = 1.0;
    auto mapPoint = [&](int index, double value) {
        const double xRatio = samples_.size() == 1 ? 0.0 : static_cast<double>(index) / (samples_.size() - 1);
        const double yRatio = clampedDamage01(value) / yMax;
        return QPointF(plot.left() + xRatio * plot.width(), plot.bottom() - yRatio * plot.height());
    };

    painter.setPen(QPen(QColor(232, 237, 243), 1, Qt::DashLine));
    for (int i = 1; i <= 4; ++i) {
        const double y = plot.bottom() - i * plot.height() / 4.0;
        painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
    }
    painter.setPen(QColor(92, 108, 122));
    painter.drawText(QRectF(4, plot.top() - 4, 46, 18), Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("1.0"));
    painter.drawText(QRectF(4, plot.bottom() - 12, 46, 18), Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("0.0"));

    const double thresholdY = plot.bottom() - std::clamp(threshold_ / yMax, 0.0, 1.0) * plot.height();
    painter.setPen(QPen(QColor(204, 67, 67), 1.6, Qt::DashLine));
    painter.drawLine(QPointF(plot.left(), thresholdY), QPointF(plot.right(), thresholdY));
    painter.drawText(QRectF(plot.right() - 90, thresholdY - 22, 86, 20),
                     Qt::AlignRight | Qt::AlignVCenter,
                     QStringLiteral("threshold"));

    QPainterPath path;
    for (int i = 0; i < samples_.size(); ++i) {
        const QPointF point = mapPoint(i, samples_[i]);
        if (i == 0) {
            path.moveTo(point);
        } else {
            path.lineTo(point);
        }
    }
    painter.setPen(QPen(QColor(36, 123, 160), 2.5));
    painter.drawPath(path);
    painter.setBrush(QColor(36, 123, 160));
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(mapPoint(samples_.size() - 1, samples_.back()), 4.5, 4.5);
}

DamageAssessmentMonitorWidget::DamageAssessmentMonitorWidget(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
    connectUi();

    demoTimer_ = new QTimer(this);
    freshnessTimer_ = new QTimer(this);
    connect(demoTimer_, &QTimer::timeout, this, [this]() { onDemoTick(); });
    connect(freshnessTimer_, &QTimer::timeout, this, [this]() { updateFreshness(); });
    freshnessTimer_->start(250);
    resetDemo();
    reconnectRos();
    if (simulationCheck_->isChecked()) {
        startDemo();
    } else {
        updateFreshness();
    }
}

DamageAssessmentMonitorWidget::~DamageAssessmentMonitorWidget() = default;

void DamageAssessmentMonitorWidget::buildUi()
{
    setWindowTitle(QStringLiteral("损伤累计模块示意 UI"));
    setStyleSheet(QStringLiteral(
        "QWidget{background:#f5f7fa;color:#1f2933;font-family:'Microsoft YaHei UI','Segoe UI';font-size:10pt;}"
        "QGroupBox{background:white;border:1px solid #d8e0e8;border-radius:6px;margin-top:14px;padding:10px;font-weight:600;}"
        "QGroupBox::title{subcontrol-origin:margin;left:10px;padding:0 6px;color:#27445c;}"
        "QLineEdit,QDoubleSpinBox{background:white;border:1px solid #c9d4df;border-radius:4px;padding:5px 6px;}"
        "QTableWidget{background:white;border:1px solid #d8e0e8;gridline-color:#e5ebf1;}"
        "QHeaderView::section{background:#edf3f8;border:0;border-right:1px solid #d8e0e8;padding:6px;font-weight:600;}"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(18, 16, 18, 18);
    root->setSpacing(12);

    auto* titleRow = new QHBoxLayout();
    auto* title = new QLabel(QStringLiteral("损伤累计模块示意 UI"));
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

    auto* subtitle = new QLabel(QStringLiteral("本界面展示 damage_assessment 公开 DTO 形态；默认用最小线性累计规则占位真实模型输出。"));
    subtitle->setStyleSheet(QStringLiteral("color:#5d6b78;"));
    root->addWidget(subtitle);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);
    root->addWidget(splitter, 1);

    auto* left = new QWidget(splitter);
    auto* leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 8, 0);
    leftLayout->setSpacing(10);
    curve_ = new DamageCurveWidget(left);
    leftLayout->addWidget(curve_, 1);

    auto* cloudGroup = new QGroupBox(QStringLiteral("飞船模型 VTK 损伤场"));
    auto* cloudLayout = new QVBoxLayout(cloudGroup);
    auto* fieldRow = new QHBoxLayout();
    fieldRow->addWidget(new QLabel(QStringLiteral("subject")));
    subjectCombo_ = new QComboBox(cloudGroup);
    subjectCombo_->addItem(QStringLiteral("T / 温度相关损伤"), QStringLiteral("T"));
    subjectCombo_->addItem(QStringLiteral("S / 应力相关损伤"), QStringLiteral("S"));
    subjectCombo_->addItem(QStringLiteral("P / 压力相关损伤"), QStringLiteral("P"));
    subjectCombo_->addItem(QStringLiteral("K / 热流相关损伤"), QStringLiteral("K"));
    fieldRow->addWidget(subjectCombo_, 1);
    fieldRow->addWidget(new QLabel(QStringLiteral("component")));
    componentSpin_ = new QSpinBox(cloudGroup);
    componentSpin_->setRange(0, 8);
    componentSpin_->setValue(0);
    fieldRow->addWidget(componentSpin_);
    cloudLayout->addLayout(fieldRow);
    damageField_ = new flightenv::ui::demo::VtkModelFieldWidget(cloudGroup);
    cloudLayout->addWidget(damageField_, 1);
    leftLayout->addWidget(cloudGroup, 1);

    auto* tableGroup = new QGroupBox(QStringLiteral("DamageIncrementDTO"));
    auto* tableLayout = new QVBoxLayout(tableGroup);
    incrementTable_ = new QTableWidget(0, 7, tableGroup);
    incrementTable_->setHorizontalHeaderLabels({
        QStringLiteral("increment_id"),
        QStringLiteral("mode"),
        QStringLiteral("location"),
        QStringLiteral("delta"),
        QStringLiteral("cumulative"),
        QStringLiteral("unit"),
        QStringLiteral("status")
    });
    incrementTable_->verticalHeader()->setVisible(false);
    incrementTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    incrementTable_->horizontalHeader()->setStretchLastSection(true);
    tableLayout->addWidget(incrementTable_);
    leftLayout->addWidget(tableGroup);

    auto* right = new QWidget(splitter);
    right->setMinimumWidth(350);
    auto* rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(8, 0, 0, 0);
    rightLayout->setSpacing(10);

    auto* configGroup = new QGroupBox(QStringLiteral("配置区"));
    auto* form = new QFormLayout(configGroup);
    modelIdEdit_ = new QLineEdit(QStringLiteral("damage-linear-miner-demo"));
    assetIdEdit_ = new QLineEdit(QStringLiteral("thermal-panel-A01"));
    inputTopicEdit_ = new QLineEdit(QStringLiteral("/flightenv/field_prediction"));
    outputTopicEdit_ = new QLineEdit(QStringLiteral("/flightenv/damage_assessment"));
    runtimeSnapshotTopicEdit_ = new QLineEdit(QStringLiteral("/flightenv/runtime_snapshot"));
    damageFieldTopicEdit_ = new QLineEdit(QStringLiteral("/flightenv/damage_field"));
    assetRootEdit_ = new QLineEdit(QStringLiteral("F:/code/FlightEnvMultiRepo/_deps/example"));
    damageRuleEdit_ = new QLineEdit(QStringLiteral("linear_miner_baseline"));
    thresholdSpin_ = new QDoubleSpinBox();
    thresholdSpin_->setRange(0.01, 1.0);
    thresholdSpin_->setDecimals(3);
    thresholdSpin_->setValue(1.0);
    updateRateSpin_ = new QDoubleSpinBox();
    updateRateSpin_->setRange(0.2, 10.0);
    updateRateSpin_->setDecimals(1);
    updateRateSpin_->setValue(1.0);
    updateRateSpin_->setSuffix(QStringLiteral(" Hz"));
    simulationCheck_ = new QCheckBox(QStringLiteral("启用本地线性累计源"));
    simulationCheck_->setChecked(false);
    addRow(form, QStringLiteral("model_type"), new QLabel(QStringLiteral("damage_assessment")));
    addRow(form, QStringLiteral("model_id"), modelIdEdit_);
    addRow(form, QStringLiteral("asset_id"), assetIdEdit_);
    addRow(form, QStringLiteral("input_topic"), inputTopicEdit_);
    addRow(form, QStringLiteral("output_topic"), outputTopicEdit_);
    addRow(form, QStringLiteral("runtime_snapshot"), runtimeSnapshotTopicEdit_);
    addRow(form, QStringLiteral("damage_field"), damageFieldTopicEdit_);
    addRow(form, QStringLiteral("asset_root"), assetRootEdit_);
    addRow(form, QStringLiteral("damage_rule"), damageRuleEdit_);
    addRow(form, QStringLiteral("threshold"), thresholdSpin_);
    addRow(form, QStringLiteral("update_rate"), updateRateSpin_);
    addRow(form, QStringLiteral("数据源"), simulationCheck_);
    rightLayout->addWidget(configGroup);

    auto* statusGroup = new QGroupBox(QStringLiteral("累计状态"));
    auto* statusForm = new QFormLayout(statusGroup);
    frameIdValue_ = makeValueLabel();
    sourceFrameValue_ = makeValueLabel();
    incrementValue_ = makeValueLabel();
    cumulativeValue_ = makeValueLabel();
    modeValue_ = makeValueLabel();
    freshnessValue_ = makeValueLabel();
    damageBar_ = new QProgressBar();
    damageBar_->setRange(0, 1000);
    damageBar_->setFormat(QStringLiteral("损伤阈值占比 %p%"));
    damageBar_->setStyleSheet(QStringLiteral(
        "QProgressBar{border:1px solid #c8d0da;border-radius:6px;background:#eef2f6;text-align:center;height:22px;color:#1f2933;}"
        "QProgressBar::chunk{border-radius:5px;background:#2f9e44;}"));
    addRow(statusForm, QStringLiteral("frame_id"), frameIdValue_);
    addRow(statusForm, QStringLiteral("source"), sourceFrameValue_);
    addRow(statusForm, QStringLiteral("delta"), incrementValue_);
    addRow(statusForm, QStringLiteral("cumulative"), cumulativeValue_);
    addRow(statusForm, QStringLiteral("mode"), modeValue_);
    addRow(statusForm, QStringLiteral("freshness"), freshnessValue_);
    addRow(statusForm, QStringLiteral("threshold"), damageBar_);
    rightLayout->addWidget(statusGroup);

    auto* criterionGroup = new QGroupBox(QStringLiteral("损伤准则判定"));
    auto* criterionLayout = new QVBoxLayout(criterionGroup);
    criterionTable_ = new QTableWidget(0, 6, criterionGroup);
    criterionTable_->setHorizontalHeaderLabels({
        QStringLiteral("准则"),
        QStringLiteral("当前值"),
        QStringLiteral("限值"),
        QStringLiteral("占比"),
        QStringLiteral("状态"),
        QStringLiteral("算法/来源")
    });
    criterionTable_->verticalHeader()->setVisible(false);
    criterionTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    criterionTable_->horizontalHeader()->setStretchLastSection(true);
    criterionLayout->addWidget(criterionTable_);
    rightLayout->addWidget(criterionGroup);

    auto* historyGroup = new QGroupBox(QStringLiteral("历史损伤帧"));
    auto* historyLayout = new QVBoxLayout(historyGroup);
    historyTable_ = new QTableWidget(0, 7, historyGroup);
    historyTable_->setHorizontalHeaderLabels({
        QStringLiteral("frame_id"),
        QStringLiteral("source"),
        QStringLiteral("delta"),
        QStringLiteral("cumulative"),
        QStringLiteral("Tmax/K"),
        QStringLiteral("stress/MPa"),
        QStringLiteral("status")
    });
    historyTable_->verticalHeader()->setVisible(false);
    historyTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    historyTable_->horizontalHeader()->setStretchLastSection(true);
    historyLayout->addWidget(historyTable_);
    rightLayout->addWidget(historyGroup);

    auto* boundary = new QLabel(QStringLiteral(
        "边界说明：本 demo 用 DamageAssessmentFrame/DamageIncrementDTO 公开契约表达 UI 数据；线性累计只为占位，无 runtime-private、Core、DB 或 source-supported 依赖。"));
    boundary->setWordWrap(true);
    boundary->setStyleSheet(QStringLiteral("background:#eef6f3;border:1px solid #c8ded6;border-radius:6px;padding:10px;color:#2f5a4c;"));
    rightLayout->addWidget(boundary);
    rightLayout->addStretch();

    splitter->addWidget(left);
    splitter->addWidget(right);
    splitter->setStretchFactor(0, 1);
}

void DamageAssessmentMonitorWidget::connectUi()
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
    connect(damageFieldTopicEdit_, &QLineEdit::editingFinished, this, [this]() { reconnectRos(); });
    connect(assetRootEdit_, &QLineEdit::editingFinished, this, [this]() {
        if (damageField_) {
            damageField_->setAssetRoot(assetRootEdit_->text().trimmed());
        }
    });
    connect(subjectCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        renderLatestDamageField();
    });
    connect(componentSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
        renderLatestDamageField();
    });
    connect(thresholdSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) {
        curve_->setSamples(cumulativeSamples_, thresholdSpin_->value());
        updateFreshness();
    });
}

void DamageAssessmentMonitorWidget::startDemo()
{
    const auto config = readConfig();
    const int intervalMs = std::max(80, static_cast<int>(1000.0 / std::max(0.1, config.update_rate_hz)));
    demoTimer_->start(intervalMs);
    if (!freshnessClock_.isValid()) {
        if (!simulationCheck_->isChecked()) {
            setStatusBadge(rosError_.isEmpty() ? QStringLiteral("等待 ROS") : QStringLiteral("ROS 未就绪"),
                           rosError_.isEmpty() ? QStringLiteral("#7a8794") : QStringLiteral("#b42318"));
        }
        onDemoTick();
    }
}

void DamageAssessmentMonitorWidget::stopDemo()
{
    demoTimer_->stop();
    setStatusBadge(QStringLiteral("等待 ROS"), QStringLiteral("#7a8794"));
    updateFreshness();
    setStatusBadge(QStringLiteral("等待输入"), QStringLiteral("#7a8794"));
    setStatusBadge(QStringLiteral("等待 ROS"), QStringLiteral("#7a8794"));
}

void DamageAssessmentMonitorWidget::reconnectRos()
{
    ros_ = std::make_unique<flightenv::ui::demo::RosJsonSubscriber>("damage_assessment_monitor_ui");
    if (!ros_->ok()) {
        rosError_ = QString::fromStdString(ros_->error());
        setStatusBadge(QStringLiteral("ROS 未就绪"), QStringLiteral("#b42318"));
        sourceFrameValue_->setText(rosError_);
        return;
    }

    const auto config = readConfig();
    if (damageField_) {
        damageField_->setAssetRoot(config.asset_root);
    }
    rosError_.clear();
    ros_->subscribe(config.output_topic.toStdString(), [this](const std::string& topic, const std::string& payload) {
        onDamagePayload(QString::fromStdString(topic), QString::fromStdString(payload));
    });
    ros_->subscribe(config.runtime_snapshot_topic.toStdString(), [this](const std::string& topic, const std::string& payload) {
        onRuntimeSnapshotPayload(QString::fromStdString(topic), QString::fromStdString(payload));
    });
    ros_->subscribe(config.damage_field_topic.toStdString(), [this](const std::string& topic, const std::string& payload) {
        onDamageFieldPayload(QString::fromStdString(topic), QString::fromStdString(payload));
    });
    setStatusBadge(QStringLiteral("等待 ROS"), QStringLiteral("#7a8794"));
    sourceFrameValue_->setText(QStringLiteral("订阅: %1 / %2").arg(config.output_topic, config.damage_field_topic));
}

void DamageAssessmentMonitorWidget::onDamagePayload(const QString& topic, const QString& payload)
{
    QMetaObject::invokeMethod(this, [this, topic, payload]() {
        try {
            const auto frame = json::parse(payload.toStdString()).get<contracts::DamageAssessmentFrame>();
            updateFrame(frame);
            sourceFrameValue_->setText(QStringLiteral("%1 / %2").arg(topic, QString::fromStdString(frame.source_frame_id)));
            setStatusBadge(QStringLiteral("ROS 实时"), QStringLiteral("#25835f"));
        } catch (const std::exception& ex) {
            rosError_ = QStringLiteral("损伤 JSON 解析失败: %1").arg(QString::fromLocal8Bit(ex.what()));
            setStatusBadge(QStringLiteral("解析失败"), QStringLiteral("#b42318"));
            sourceFrameValue_->setText(rosError_);
        }
    }, Qt::QueuedConnection);
}

void DamageAssessmentMonitorWidget::onRuntimeSnapshotPayload(const QString& topic, const QString& payload)
{
    QMetaObject::invokeMethod(this, [this, topic, payload]() {
        try {
            runtimeSnapshot_ = json::parse(payload.toStdString()).get<contracts::RuntimeSnapshotDTO>();
            if (damageField_) {
                damageField_->setAssetRoot(readConfig().asset_root);
                damageField_->setRuntimeSnapshot(*runtimeSnapshot_);
            }
            sourceFrameValue_->setText(QStringLiteral("%1 / runtime snapshot: fields=%2 meshes=%3")
                                           .arg(topic)
                                           .arg(runtimeSnapshot_->field_layouts.size())
                                           .arg(runtimeSnapshot_->meshes.size()));
            renderLatestDamageField();
        } catch (const std::exception& ex) {
            rosError_ = QStringLiteral("RuntimeSnapshotDTO JSON 解析失败: %1").arg(QString::fromLocal8Bit(ex.what()));
            setStatusBadge(QStringLiteral("布局解析失败"), QStringLiteral("#b42318"));
            sourceFrameValue_->setText(rosError_);
        }
    }, Qt::QueuedConnection);
}

void DamageAssessmentMonitorWidget::onDamageFieldPayload(const QString& topic, const QString& payload)
{
    QMetaObject::invokeMethod(this, [this, topic, payload]() {
        try {
            latestDamageField_ = json::parse(payload.toStdString()).get<contracts::FieldBundleDTO>();
            sourceFrameValue_->setText(QStringLiteral("%1 / 已收到真实损伤场").arg(topic));
            renderLatestDamageField();
        } catch (const std::exception& ex) {
            rosError_ = QStringLiteral("Damage FieldBundleDTO JSON 解析失败: %1").arg(QString::fromLocal8Bit(ex.what()));
            setStatusBadge(QStringLiteral("损伤场解析失败"), QStringLiteral("#b42318"));
            sourceFrameValue_->setText(rosError_);
        }
    }, Qt::QueuedConnection);
}

void DamageAssessmentMonitorWidget::renderLatestDamageField()
{
    if (damageField_ == nullptr) {
        return;
    }
    if (!runtimeSnapshot_) {
        damageField_->clearField(QStringLiteral("等待 RuntimeSnapshotDTO：需要真实飞船模型节点和面片索引后才能绘制损伤场"));
        return;
    }
    if (!latestDamageField_) {
        damageField_->clearField(QStringLiteral("等待 FieldBundleDTO：DamageAssessmentFrame 只是标量摘要，不能生成真实损伤云图"));
        return;
    }

    const auto stats = damageField_->renderFieldBundle(*latestDamageField_,
                                                       subjectFromCombo(subjectCombo_),
                                                       componentSpin_->value(),
                                                       QStringLiteral("累计损伤场"),
                                                       QStringLiteral("damage"));
    if (!stats.ok) {
        sourceFrameValue_->setText(stats.message);
        setStatusBadge(QStringLiteral("损伤场不可用"), QStringLiteral("#b54708"));
        return;
    }

    cumulativeValue_->setText(QStringLiteral("%1").arg(stats.maxValue, 0, 'g', 6));
    modeValue_->setText(QStringLiteral("max_node=%1").arg(stats.maxNodeIndex));
    setStatusBadge(QStringLiteral("VTK 损伤场实时"), QStringLiteral("#25835f"));
}

void DamageAssessmentMonitorWidget::resetDemo()
{
    frameCounter_ = 0;
    cumulativeDamage_ = threshold01(readConfig().threshold) * 0.08;
    cumulativeSamples_.clear();
}

void DamageAssessmentMonitorWidget::onDemoTick()
{
    const auto frame = makeFrame(readConfig());
    updateFrame(frame);
}

void DamageAssessmentMonitorWidget::updateFreshness()
{
    if (!freshnessClock_.isValid()) {
        if (!simulationCheck_->isChecked()) {
            setStatusBadge(rosError_.isEmpty() ? QStringLiteral("等待 ROS") : QStringLiteral("ROS 未就绪"),
                           rosError_.isEmpty() ? QStringLiteral("#7a8794") : QStringLiteral("#b42318"));
        }
        freshnessValue_->setText(QStringLiteral("无样本"));
        return;
    }
    const qint64 ageMs = freshnessClock_.elapsed();
    freshnessValue_->setText(QStringLiteral("%1 ms").arg(ageMs));
    const double ratio = std::clamp(cumulativeDamage_ / threshold01(thresholdSpin_->value()), 0.0, 1.0);
    if (ratio >= 1.0) {
        setStatusBadge(QStringLiteral("超阈值"), QStringLiteral("#b42318"));
        damageBar_->setStyleSheet(QStringLiteral(
            "QProgressBar{border:1px solid #c8d0da;border-radius:6px;background:#eef2f6;text-align:center;height:22px;color:#1f2933;}"
            "QProgressBar::chunk{border-radius:5px;background:#d92d20;}"));
    } else if (ratio >= 0.75) {
        setStatusBadge(QStringLiteral("预警"), QStringLiteral("#b54708"));
        damageBar_->setStyleSheet(QStringLiteral(
            "QProgressBar{border:1px solid #c8d0da;border-radius:6px;background:#eef2f6;text-align:center;height:22px;color:#1f2933;}"
            "QProgressBar::chunk{border-radius:5px;background:#f59f00;}"));
    } else if (simulationCheck_->isChecked() && ageMs < 2000) {
        setStatusBadge(QStringLiteral("fresh / 运行中"), QStringLiteral("#25835f"));
    }
}

void DamageAssessmentMonitorWidget::updateFrame(const contracts::DamageAssessmentFrame& frame)
{
    if (frame.increments.empty()) {
        return;
    }

    const auto& increment = frame.increments.front();
    cumulativeDamage_ = clampedDamage01(increment.cumulative_value);
    if (cumulativeSamples_.isEmpty() || std::abs(cumulativeSamples_.back() - cumulativeDamage_) > 1.0e-12) {
        cumulativeSamples_.push_back(cumulativeDamage_);
        while (cumulativeSamples_.size() > kMaxSamples) {
            cumulativeSamples_.remove(0);
        }
    }
    freshnessClock_.restart();
    frameIdValue_->setText(QString::fromStdString(frame.frame_id));
    sourceFrameValue_->setText(QString::fromStdString(frame.source_frame_id));
    incrementValue_->setText(fixed(increment.increment_value, 6));
    cumulativeValue_->setText(fixed(cumulativeDamage_, 6));
    modeValue_->setText(QString::fromStdString(increment.damage_mode));
    damageBar_->setValue(static_cast<int>(std::clamp(cumulativeDamage_ / threshold01(readConfig().threshold), 0.0, 1.0) * 1000.0));
    history_.push_front(frame);
    while (history_.size() > 80) {
        history_.pop_back();
    }
    updateIncrementTable(frame);
    updateCriterionTable(frame);
    updateHistoryTable();
    curve_->setSamples(cumulativeSamples_, readConfig().threshold);
    updateFreshness();
}

void DamageAssessmentMonitorWidget::updateIncrementTable(const contracts::DamageAssessmentFrame& frame)
{
    incrementTable_->setRowCount(static_cast<int>(frame.increments.size()));
    for (int row = 0; row < static_cast<int>(frame.increments.size()); ++row) {
        const auto& increment = frame.increments[static_cast<size_t>(row)];
        incrementTable_->setItem(row, 0, item(QString::fromStdString(increment.increment_id)));
        incrementTable_->setItem(row, 1, item(QString::fromStdString(increment.damage_mode)));
        incrementTable_->setItem(row, 2, item(QString::fromStdString(increment.location_id)));
        incrementTable_->setItem(row, 3, item(fixed(increment.increment_value, 6)));
        incrementTable_->setItem(row, 4, item(fixed(clampedDamage01(increment.cumulative_value), 6)));
        incrementTable_->setItem(row, 5, item(QString::fromStdString(increment.unit)));
        incrementTable_->setItem(row, 6, item(QString::fromStdString(increment.status)));
    }
    incrementTable_->resizeColumnsToContents();
}

void DamageAssessmentMonitorWidget::updateCriterionTable(const contracts::DamageAssessmentFrame& frame)
{
    const double threshold = threshold01(readConfig().threshold);
    const double delta = frame.increments.empty() ? 0.0 : frame.increments.front().increment_value;
    const double cumulative = frame.increments.empty() ? cumulativeDamage_ : clampedDamage01(frame.increments.front().cumulative_value);
    const double tempK = evidenceNumber(frame,
                                        {"wall_temperature_max", "temperature_max", "surface_temperature_max"},
                                        300.0 + 760.0 * std::clamp(cumulative / threshold, 0.0, 1.0));
    const double qPa = evidenceNumber(frame, {"dynamic_pressure_max", "q_max", "dynamic_pressure_pa"}, 24000.0);
    const double loadG = evidenceNumber(frame, {"normal_load_max", "load_factor_max", "normal_load_g"}, 1.0);
    const double stressMpa = evidenceNumber(frame,
                                            {"stress_max_mpa", "max_stress_mpa"},
                                            qPa * std::max(1.0, loadG) / 1.0e6);
    const double heatFlux = evidenceNumber(frame,
                                           {"heat_flux_max", "wall_heat_flux_max", "heat_flux_kw_m2"},
                                           std::max(0.0, (tempK - 300.0) * 0.045));

    struct Row {
        QString name;
        double value;
        QString valueUnit;
        double limit;
        QString limitUnit;
        QString algorithm;
    };
    const Row rows[] = {
        {QStringLiteral("Miner 累计损伤"), cumulative, QStringLiteral(""), threshold, QStringLiteral(""), QStringLiteral("sum(delta_damage)")},
        {QStringLiteral("最大温度准则"), tempK, QStringLiteral(" K"), kTemperatureLimitK, QStringLiteral(" K"), QStringLiteral("diagnostic.evidence.wall_temperature_max")},
        {QStringLiteral("最大应力准则"), stressMpa, QStringLiteral(" MPa"), kStressLimitMpa, QStringLiteral(" MPa"), QStringLiteral("q_max * n_max / 1e6 代理值")},
        {QStringLiteral("最大热流准则"), heatFlux, QStringLiteral(" kW/m2"), kHeatFluxLimitKwM2, QStringLiteral(" kW/m2"), QStringLiteral("diagnostic.evidence.heat_flux_max")},
        {QStringLiteral("单帧损伤增量"), delta, QStringLiteral(""), threshold * 0.02, QStringLiteral(""), QStringLiteral("DamageIncrementDTO.increment_value")}
    };

    const int rowCount = static_cast<int>(sizeof(rows) / sizeof(rows[0]));
    criterionTable_->setRowCount(rowCount);
    for (int row = 0; row < rowCount; ++row) {
        const auto& value = rows[row];
        const double ratio = value.limit > 0.0 ? value.value / value.limit : 0.0;
        criterionTable_->setItem(row, 0, item(value.name));
        criterionTable_->setItem(row, 1, item(fixed(value.value, row == 0 || row == 4 ? 6 : 2) + value.valueUnit));
        criterionTable_->setItem(row, 2, item(fixed(value.limit, row == 0 || row == 4 ? 6 : 2) + value.limitUnit));
        criterionTable_->setItem(row, 3, item(QStringLiteral("%1%").arg(fixed(ratio * 100.0, 1))));
        criterionTable_->setItem(row, 4, item(criterionStatus(ratio)));
        criterionTable_->setItem(row, 5, item(value.algorithm));
    }
    criterionTable_->resizeColumnsToContents();
}

void DamageAssessmentMonitorWidget::updateHistoryTable()
{
    historyTable_->setRowCount(static_cast<int>(history_.size()));
    for (int row = 0; row < static_cast<int>(history_.size()); ++row) {
        const auto& frame = history_[static_cast<size_t>(row)];
        const auto& increment = frame.increments.empty() ? contracts::DamageIncrementDTO{} : frame.increments.front();
        const double threshold = threshold01(readConfig().threshold);
        const double cumulative = clampedDamage01(increment.cumulative_value);
        const double tempK = evidenceNumber(frame,
                                            {"wall_temperature_max", "temperature_max", "surface_temperature_max"},
                                            300.0 + 760.0 * std::clamp(cumulative / threshold, 0.0, 1.0));
        const double qPa = evidenceNumber(frame, {"dynamic_pressure_max", "q_max", "dynamic_pressure_pa"}, 24000.0);
        const double loadG = evidenceNumber(frame, {"normal_load_max", "load_factor_max", "normal_load_g"}, 1.0);
        const double stressMpa = evidenceNumber(frame, {"stress_max_mpa", "max_stress_mpa"}, qPa * std::max(1.0, loadG) / 1.0e6);
        historyTable_->setItem(row, 0, item(QString::fromStdString(frame.frame_id)));
        historyTable_->setItem(row, 1, item(QString::fromStdString(frame.source_frame_id)));
        historyTable_->setItem(row, 2, item(fixed(increment.increment_value, 6)));
        historyTable_->setItem(row, 3, item(fixed(cumulative, 6)));
        historyTable_->setItem(row, 4, item(fixed(tempK, 1)));
        historyTable_->setItem(row, 5, item(fixed(stressMpa, 3)));
        historyTable_->setItem(row, 6, item(QString::fromStdString(frame.status)));
    }
    historyTable_->resizeColumnsToContents();
}

DamageAssessmentDemoConfig DamageAssessmentMonitorWidget::readConfig() const
{
    DamageAssessmentDemoConfig config;
    config.model_id = modelIdEdit_->text().trimmed();
    config.asset_id = assetIdEdit_->text().trimmed();
    config.input_topic = inputTopicEdit_->text().trimmed();
    config.output_topic = outputTopicEdit_->text().trimmed();
    config.runtime_snapshot_topic = runtimeSnapshotTopicEdit_->text().trimmed();
    config.damage_field_topic = damageFieldTopicEdit_->text().trimmed();
    config.asset_root = assetRootEdit_->text().trimmed();
    config.damage_rule = damageRuleEdit_->text().trimmed();
    config.threshold = thresholdSpin_->value();
    config.update_rate_hz = updateRateSpin_->value();
    return config;
}

contracts::DamageAssessmentFrame DamageAssessmentMonitorWidget::makeFrame(const DamageAssessmentDemoConfig& config)
{
    ++frameCounter_;
    const double loadWave = 0.5 + 0.5 * std::sin(frameCounter_ * 0.31);
    const double fieldIntensity = 0.72 + 0.38 * loadWave;
    const double threshold = threshold01(config.threshold);
    const double requestedDelta = std::max(0.0, threshold * 0.003 * fieldIntensity);
    const double previousDamage = clampedDamage01(cumulativeDamage_);
    const double delta = previousDamage >= 1.0
        ? 0.0
        : std::min(requestedDelta, std::max(0.0, 1.0 - previousDamage));
    cumulativeDamage_ = clampedDamage01(previousDamage + delta);
    cumulativeSamples_.push_back(cumulativeDamage_);
    while (cumulativeSamples_.size() > kMaxSamples) {
        cumulativeSamples_.remove(0);
    }

    contracts::DamageAssessmentFrame frame;
    frame.frame_id = QStringLiteral("damage-%1").arg(frameCounter_, 5, 10, QChar('0')).toStdString();
    frame.run_id = "damage-assessment-monitor-ui-demo";
    frame.object_id = config.asset_id.toStdString();
    frame.source_frame_id = QStringLiteral("field-%1").arg(frameCounter_, 5, 10, QChar('0')).toStdString();
    frame.stamp_ns = nowNs();
    frame.source_stamp_ns = frame.stamp_ns;
    frame.status = cumulativeDamage_ >= threshold ? "limit_exceeded" : "simulated";
    frame.reason = "local linear Miner-like baseline for UI demo";
    frame.model_snapshot.model_id = config.model_id.toStdString();
    frame.model_snapshot.model_type = "damage_assessment";
    frame.model_snapshot.version = "local-demo";
    frame.model_snapshot.artifact_uri = "local://DamageAssessmentMonitorUi/baseline";
    frame.diagnostic.status = frame.status;
    frame.diagnostic.code = "damage_demo_evidence";
    frame.diagnostic.message = "demo evidence for cumulative, max temperature, heat flux and stress-proxy criteria";
    const double tempK = 620.0 + 520.0 * fieldIntensity + 80.0 * std::sin(frameCounter_ * 0.17);
    const double qMax = 22000.0 + 9000.0 * loadWave;
    const double loadMax = 1.05 + 2.2 * loadWave;
    const double heatFlux = std::max(0.0, (tempK - 300.0) * 0.052);
    frame.diagnostic.evidence = json{
        {"wall_temperature_max", tempK},
        {"heat_flux_max", heatFlux},
        {"dynamic_pressure_max", qMax},
        {"normal_load_max", loadMax},
        {"stress_max_mpa", qMax * loadMax / 1.0e6},
        {"normalized_field_intensity", fieldIntensity}
    };

    contracts::DamageIncrementDTO increment;
    increment.increment_id = QStringLiteral("inc-%1").arg(frameCounter_, 5, 10, QChar('0')).toStdString();
    increment.run_id = frame.run_id;
    increment.object_id = frame.object_id;
    increment.source_frame_id = frame.source_frame_id;
    increment.damage_mode = config.damage_rule.toStdString();
    increment.location_id = "panel-hotspot";
    increment.stamp_ns = frame.stamp_ns;
    increment.source_stamp_ns = frame.source_stamp_ns;
    increment.increment_value = delta;
    increment.cumulative_value = cumulativeDamage_;
    increment.unit = "damage_index";
    increment.status = cumulativeDamage_ >= threshold ? "limit_exceeded" : "ok";
    increment.reason = "delta = min(requested_delta, 1 - cumulative_damage)";
    increment.model_snapshot = frame.model_snapshot;
    frame.increments.push_back(increment);
    return frame;
}

QLabel* DamageAssessmentMonitorWidget::makeValueLabel(const QString& text) const
{
    auto* label = new QLabel(text);
    label->setMinimumHeight(28);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setStyleSheet(QStringLiteral("background:#f8fafc;border:1px solid #d9e2ea;border-radius:4px;padding:4px 7px;font-weight:500;"));
    return label;
}

void DamageAssessmentMonitorWidget::setStatusBadge(const QString& text, const QString& color)
{
    statusBadge_->setText(text);
    statusBadge_->setStyleSheet(QStringLiteral("background:%1;color:white;border-radius:14px;padding:5px 12px;font-weight:600;").arg(color));
}
