#include "TrajectoryMonitorWidget.h"
#include "../common/RosJsonSubscriber.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
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
#include <limits>
#include <utility>

namespace {

constexpr double kPi = 3.14159265358979323846;
using json = nlohmann::json;

QString metersText(double value)
{
    return QStringLiteral("%1 m").arg(value, 0, 'f', 1);
}

QString secondsText(double value)
{
    return QStringLiteral("%1 s").arg(value, 0, 'f', 1);
}

QString numberText(double value, int precision = 2)
{
    return QString::number(value, 'f', precision);
}

contracts::TimestampNs currentTimestampNs()
{
    return static_cast<contracts::TimestampNs>(QDateTime::currentMSecsSinceEpoch()) * 1000000;
}

QTableWidgetItem* readOnlyItem(const QString& text, Qt::Alignment alignment = Qt::AlignCenter)
{
    auto* item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    item->setTextAlignment(alignment);
    return item;
}

void addFormRow(QFormLayout* form, const QString& label, QWidget* field)
{
    auto* caption = new QLabel(label);
    caption->setMinimumWidth(108);
    form->addRow(caption, field);
}

contracts::StateFrame stateFromTrajectory(const contracts::TrajectoryPredictionFrame& trajectory)
{
    contracts::StateFrame state;
    if (!trajectory.samples.empty()) {
        const auto& sample = trajectory.samples.front();
        state.time = sample.time_s;
        if (sample.position_ned_m.size() >= 3) {
            state.pos_x = sample.position_ned_m[0];
            state.pos_y = sample.position_ned_m[1];
            state.pos_z = -sample.position_ned_m[2];
            state.h = -sample.position_ned_m[2];
        } else if (sample.lla_rad_m.size() >= 3) {
            state.h = sample.lla_rad_m[2];
            state.pos_z = state.h;
        }
        state.ma = sample.mach;
        state.q = sample.dynamic_pressure_pa;
        state.alpha = sample.angle_of_attack_rad * 180.0 / kPi;
        state.beta = sample.sideslip_rad * 180.0 / kPi;
        state.stamp_ns = trajectory.source_stamp_ns;
    }
    return state;
}

double sampleAltitude(const contracts::TrajectorySampleDTO& sample)
{
    if (sample.position_ned_m.size() >= 3) {
        return -sample.position_ned_m[2];
    }
    return sample.lla_rad_m.size() >= 3 ? sample.lla_rad_m[2] : 0.0;
}

double sampleX(const contracts::TrajectorySampleDTO& sample)
{
    return sample.position_ned_m.empty() ? 0.0 : sample.position_ned_m[0];
}

double sampleAlphaDeg(const contracts::TrajectorySampleDTO& sample)
{
    return sample.angle_of_attack_rad * 180.0 / kPi;
}

double trajectoryRangeM(const contracts::TrajectoryPredictionFrame& trajectory)
{
    if (trajectory.samples.size() < 2) {
        return 0.0;
    }
    return sampleX(trajectory.samples.back()) - sampleX(trajectory.samples.front());
}

double startContinuityErrorM(const contracts::StateFrame& state,
                             const contracts::TrajectoryPredictionFrame& trajectory)
{
    if (trajectory.samples.empty()) {
        return 0.0;
    }
    const auto& first = trajectory.samples.front();
    const double dx = sampleX(first) - state.pos_x;
    const double dh = sampleAltitude(first) - state.h;
    return std::sqrt(dx * dx + dh * dh);
}

size_t displaySampleIndex(const size_t sampleCount, const int row, const int rowCount)
{
    if (sampleCount == 0 || rowCount <= 1) {
        return 0;
    }
    const double ratio = static_cast<double>(row) /
        static_cast<double>(std::max(1, rowCount - 1));
    return std::min(sampleCount - 1,
                    static_cast<size_t>(std::llround(ratio * static_cast<double>(sampleCount - 1))));
}

} // namespace

TrajectoryPlotWidget::TrajectoryPlotWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(620, 360);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void TrajectoryPlotWidget::setFrames(
    const contracts::StateFrame& state,
    const contracts::TrajectoryPredictionFrame& trajectory)
{
    state_ = state;
    trajectory_ = trajectory;
    has_frames_ = true;
    update();
}

void TrajectoryPlotWidget::clearFrames()
{
    has_frames_ = false;
    trajectory_ = contracts::TrajectoryPredictionFrame{};
    update();
}

QPointF TrajectoryPlotWidget::statePoint(const contracts::StateFrame& state)
{
    const double altitude = state.h != 0.0 ? state.h : state.pos_z;
    return { state.time, altitude };
}

QPointF TrajectoryPlotWidget::samplePoint(const contracts::TrajectorySampleDTO& sample)
{
    return { sample.time_s, sampleAltitude(sample) };
}

void TrajectoryPlotWidget::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF bounds = rect().adjusted(14, 14, -14, -14);
    painter.fillRect(bounds, QColor(255, 255, 255));
    painter.setPen(QPen(QColor(206, 216, 224), 1));
    painter.drawRoundedRect(bounds, 6, 6);

    const QRectF plotRect = bounds.adjusted(70, 36, -28, -58);
    painter.setPen(QPen(QColor(226, 232, 238), 1));
    painter.drawRect(plotRect);

    if (!has_frames_ || trajectory_.samples.empty()) {
        painter.setPen(QColor(97, 111, 125));
        painter.drawText(
            plotRect,
            Qt::AlignCenter,
            QStringLiteral("等待 SDK/DTO 输入，或开启本地模拟源"));
        return;
    }

    double minX = std::numeric_limits<double>::max();
    double maxX = std::numeric_limits<double>::lowest();
    double minZ = std::numeric_limits<double>::max();
    double maxZ = std::numeric_limits<double>::lowest();

    auto includePoint = [&](const QPointF& point) {
        minX = std::min(minX, point.x());
        maxX = std::max(maxX, point.x());
        minZ = std::min(minZ, point.y());
        maxZ = std::max(maxZ, point.y());
    };

    includePoint(statePoint(state_));
    for (const auto& sample : trajectory_.samples) {
        includePoint(samplePoint(sample));
    }

    if (maxX - minX < 1.0e-6) {
        maxX += 0.5;
        minX -= 0.5;
    }
    if (maxZ - minZ < 1.0) {
        maxZ += 100.0;
        minZ -= 100.0;
    }

    const double padX = (maxX - minX) * 0.08;
    const double padZ = (maxZ - minZ) * 0.12;
    minX -= padX;
    maxX += padX;
    minZ = std::max(0.0, minZ - padZ);
    maxZ += padZ;

    auto mapPoint = [&](const QPointF& world) {
        const double xRatio = (world.x() - minX) / (maxX - minX);
        const double zRatio = (world.y() - minZ) / (maxZ - minZ);
        return QPointF(
            plotRect.left() + xRatio * plotRect.width(),
            plotRect.bottom() - zRatio * plotRect.height());
    };

    painter.setFont(QApplication::font());
    for (int i = 0; i <= 5; ++i) {
        const double x = plotRect.left() + i * plotRect.width() / 5.0;
        painter.setPen(QPen(QColor(236, 241, 245), 1));
        painter.drawLine(QPointF(x, plotRect.top()), QPointF(x, plotRect.bottom()));

        const double xValue = minX + i * (maxX - minX) / 5.0;
        painter.setPen(QColor(92, 108, 122));
        painter.drawText(
            QRectF(x - 45, plotRect.bottom() + 10, 90, 22),
            Qt::AlignCenter,
            QStringLiteral("%1 s").arg(xValue, 0, 'f', 2));
    }

    for (int i = 0; i <= 5; ++i) {
        const double y = plotRect.bottom() - i * plotRect.height() / 5.0;
        painter.setPen(QPen(QColor(236, 241, 245), 1));
        painter.drawLine(QPointF(plotRect.left(), y), QPointF(plotRect.right(), y));

        const double zValue = minZ + i * (maxZ - minZ) / 5.0;
        painter.setPen(QColor(92, 108, 122));
        painter.drawText(
            QRectF(bounds.left() + 6, y - 11, 58, 22),
            Qt::AlignRight | Qt::AlignVCenter,
            QStringLiteral("%1 km").arg(zValue / 1000.0, 0, 'f', 1));
    }

    painter.setPen(QColor(45, 58, 71));
    painter.drawText(
        QRectF(plotRect.left(), bounds.top() + 4, plotRect.width(), 24),
        Qt::AlignCenter,
        QStringLiteral("time-height prediction curve"));
    painter.drawText(
        QRectF(plotRect.left(), plotRect.bottom() + 34, plotRect.width(), 20),
        Qt::AlignCenter,
        QStringLiteral("time"));
    painter.save();
    painter.translate(bounds.left() + 18, plotRect.center().y());
    painter.rotate(-90);
    painter.drawText(QRectF(-80, -10, 160, 20), Qt::AlignCenter, QStringLiteral("height"));
    painter.restore();

    QPainterPath path;
    bool first = true;
    for (const auto& sample : trajectory_.samples) {
        const QPointF point = mapPoint(samplePoint(sample));
        if (first) {
            path.moveTo(point);
            first = false;
        } else {
            path.lineTo(point);
        }
    }

    painter.setPen(QPen(QColor(23, 116, 165), 2.6));
    painter.drawPath(path);

    const QPointF firstPrediction = mapPoint(samplePoint(trajectory_.samples.front()));
    const QPointF lastPrediction = mapPoint(samplePoint(trajectory_.samples.back()));
    painter.setBrush(QColor(31, 154, 92));
    painter.setPen(QPen(QColor(255, 255, 255), 1.6));
    painter.drawEllipse(firstPrediction, 6.0, 6.0);
    painter.setPen(QColor(42, 77, 61));
    painter.drawText(QRectF(firstPrediction.x() + 10, firstPrediction.y() - 18, 116, 20),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("预测首点"));

    painter.setBrush(QColor(23, 116, 165));
    painter.setPen(QPen(QColor(255, 255, 255), 1.6));
    painter.drawEllipse(lastPrediction, 6.0, 6.0);
    painter.setPen(QColor(42, 68, 92));
    painter.drawText(QRectF(lastPrediction.x() - 92, lastPrediction.y() - 22, 86, 20),
                     Qt::AlignRight | Qt::AlignVCenter,
                     QStringLiteral("预测末点"));

    const QPointF current = mapPoint(statePoint(state_));
    painter.setBrush(QColor(235, 125, 57));
    painter.setPen(QPen(QColor(255, 255, 255), 2));
    painter.drawEllipse(current, 7.5, 7.5);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(235, 125, 57), 1.5));
    painter.drawEllipse(current, 13.0, 13.0);
    painter.setPen(QColor(55, 64, 73));
    painter.drawText(
        QRectF(current.x() + 12, current.y() - 22, 112, 22),
        Qt::AlignLeft | Qt::AlignVCenter,
        QStringLiteral("当前状态点"));

    const QRectF legend(plotRect.right() - 168, plotRect.top() + 10, 150, 48);
    painter.fillRect(legend, QColor(255, 255, 255, 226));
    painter.setPen(QPen(QColor(210, 220, 228), 1));
    painter.drawRoundedRect(legend, 5, 5);
    painter.setPen(QPen(QColor(23, 116, 165), 2.6));
    painter.drawLine(legend.left() + 12, legend.top() + 16, legend.left() + 44, legend.top() + 16);
    painter.setPen(QColor(62, 73, 84));
    painter.drawText(
        legend.adjusted(52, 6, -8, -24),
        Qt::AlignLeft | Qt::AlignVCenter,
        QStringLiteral("未来弹道"));
    painter.setBrush(QColor(235, 125, 57));
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(QPointF(legend.left() + 28, legend.top() + 34), 5, 5);
    painter.setPen(QColor(62, 73, 84));
    painter.drawText(
        legend.adjusted(52, 24, -8, -6),
        Qt::AlignLeft | Qt::AlignVCenter,
        QStringLiteral("当前状态"));
}

TrajectoryMonitorWidget::TrajectoryMonitorWidget(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
    connectUi();

    simulationTimer_ = new QTimer(this);
    freshnessTimer_ = new QTimer(this);
    connect(simulationTimer_, &QTimer::timeout, this, [this]() { onSimulationTick(); });
    connect(freshnessTimer_, &QTimer::timeout, this, [this]() { updateFreshness(); });
    freshnessTimer_->start(250);

    reconnectRos();
    if (simulationCheck_->isChecked()) {
        startSimulation();
    } else {
        updateFreshness();
    }
}

TrajectoryMonitorWidget::~TrajectoryMonitorWidget() = default;

void TrajectoryMonitorWidget::ingestFrame(
    const contracts::StateFrame& state,
    const contracts::TrajectoryPredictionFrame& trajectory,
    const QString& source_label)
{
    latestTrajectory_ = trajectory;
    latestState_ = state;
    hasLatestState_ = true;
    hasLatestTrajectory_ = !trajectory.samples.empty();
    recentStates_.push_front(state);
    trimRecentStates();
    if (hasLatestTrajectory_) {
        recentTrajectories_.push_front(trajectory);
        trimRecentTrajectories();
    }

    if (hasLatestTrajectory_) {
        plot_->setFrames(state, trajectory);
    }
    freshnessClock_.restart();
    updateReadouts(state, trajectory, source_label);
    rebuildRecentStateTable();
    rebuildTrajectoryHistoryTable();
    rebuildSampleTable();
    updateFreshness();
}

void TrajectoryMonitorWidget::buildUi()
{
    setWindowTitle(QStringLiteral("弹道模块示意 UI"));
    setStyleSheet(QStringLiteral(
        "QWidget { background: #f5f7fa; color: #1f2a33; font-family: 'Microsoft YaHei UI', 'Segoe UI'; font-size: 10pt; }"
        "QGroupBox { background: #ffffff; border: 1px solid #d8e0e8; border-radius: 6px; margin-top: 14px; padding: 10px; font-weight: 600; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 6px; color: #27445c; }"
        "QLineEdit, QDoubleSpinBox, QSpinBox { background: #ffffff; border: 1px solid #c9d4df; border-radius: 4px; padding: 5px 6px; }"
        "QTableWidget { background: #ffffff; border: 1px solid #d8e0e8; gridline-color: #e5ebf1; selection-background-color: #d7ebf8; }"
        "QHeaderView::section { background: #edf3f8; border: 0; border-right: 1px solid #d8e0e8; padding: 6px; font-weight: 600; }"
        "QCheckBox { spacing: 8px; }"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(18, 16, 18, 18);
    root->setSpacing(12);

    auto* titleRow = new QHBoxLayout();
    auto* title = new QLabel(QStringLiteral("弹道模块示意 UI"));
    QFont titleFont = title->font();
    titleFont.setPointSize(18);
    titleFont.setBold(true);
    title->setFont(titleFont);
    title->setStyleSheet(QStringLiteral("color: #193447;"));

    statusBadge_ = makeValueLabel(QStringLiteral("初始化"));
    statusBadge_->setAlignment(Qt::AlignCenter);
    statusBadge_->setMinimumWidth(150);
    setStatusBadge(QStringLiteral("初始化"), QStringLiteral("#7a8794"));

    titleRow->addWidget(title);
    titleRow->addStretch();
    titleRow->addWidget(statusBadge_);
    root->addLayout(titleRow);

    auto* subtitle = new QLabel(QStringLiteral("Qt Widgets + EnvContracts DTO 层演示；默认使用本地模拟源，不直连 trajectory core/runtime-private。"));
    subtitle->setStyleSheet(QStringLiteral("color: #5d6b78;"));
    root->addWidget(subtitle);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);
    root->addWidget(splitter, 1);

    auto* left = new QWidget(splitter);
    auto* leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 8, 0);
    leftLayout->setSpacing(12);

    plot_ = new TrajectoryPlotWidget(left);
    leftLayout->addWidget(plot_, 1);

    auto* tableGroup = new QGroupBox(QStringLiteral("最近状态帧"));
    auto* tableLayout = new QVBoxLayout(tableGroup);
    recentTable_ = new QTableWidget(0, 7, tableGroup);
    recentTable_->setHorizontalHeaderLabels({
        QStringLiteral("帧"),
        QStringLiteral("t/s"),
        QStringLiteral("距离X/m"),
        QStringLiteral("高度/m"),
        QStringLiteral("Ma"),
        QStringLiteral("q/Pa"),
        QStringLiteral("状态")
    });
    recentTable_->horizontalHeader()->setStretchLastSection(true);
    recentTable_->verticalHeader()->setVisible(false);
    recentTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    recentTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    recentTable_->setAlternatingRowColors(true);
    tableLayout->addWidget(recentTable_);
    leftLayout->addWidget(tableGroup, 0);

    auto* trajectoryHistoryGroup = new QGroupBox(QStringLiteral("历史预测弹道"));
    auto* trajectoryHistoryLayout = new QVBoxLayout(trajectoryHistoryGroup);
    trajectoryHistoryTable_ = new QTableWidget(0, 10, trajectoryHistoryGroup);
    trajectoryHistoryTable_->setHorizontalHeaderLabels({
        QStringLiteral("frame_id"),
        QStringLiteral("source"),
        QStringLiteral("samples"),
        QStringLiteral("t0/s"),
        QStringLiteral("t1/s"),
        QStringLiteral("range/km"),
        QStringLiteral("h0/m"),
        QStringLiteral("h_end/m"),
        QStringLiteral("连续误差/m"),
        QStringLiteral("status")
    });
    trajectoryHistoryTable_->horizontalHeader()->setStretchLastSection(true);
    trajectoryHistoryTable_->verticalHeader()->setVisible(false);
    trajectoryHistoryTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    trajectoryHistoryTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    trajectoryHistoryTable_->setAlternatingRowColors(true);
    trajectoryHistoryLayout->addWidget(trajectoryHistoryTable_);
    leftLayout->addWidget(trajectoryHistoryGroup, 0);

    auto* sampleGroup = new QGroupBox(QStringLiteral("预测采样点明细"));
    auto* sampleLayout = new QVBoxLayout(sampleGroup);
    sampleTable_ = new QTableWidget(0, 9, sampleGroup);
    sampleTable_->setHorizontalHeaderLabels({
        QStringLiteral("#"),
        QStringLiteral("t/s"),
        QStringLiteral("x/m"),
        QStringLiteral("h/m"),
        QStringLiteral("vx/mps"),
        QStringLiteral("Mach"),
        QStringLiteral("q/Pa"),
        QStringLiteral("n/g"),
        QStringLiteral("alpha/deg")
    });
    sampleTable_->horizontalHeader()->setStretchLastSection(true);
    sampleTable_->verticalHeader()->setVisible(false);
    sampleTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    sampleTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    sampleTable_->setAlternatingRowColors(true);
    sampleLayout->addWidget(sampleTable_);
    leftLayout->addWidget(sampleGroup, 0);

    auto* right = new QWidget(splitter);
    right->setMinimumWidth(340);
    auto* rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(8, 0, 0, 0);
    rightLayout->setSpacing(10);

    auto* configGroup = new QGroupBox(QStringLiteral("配置区"));
    auto* configForm = new QFormLayout(configGroup);
    configForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    configForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    topicEdit_ = new QLineEdit(QStringLiteral("/flightenv/state_estimate"), configGroup);
    trajectoryTopicEdit_ = new QLineEdit(QStringLiteral("/flightenv/trajectory_prediction"), configGroup);
    modelIdEdit_ = new QLineEdit(QStringLiteral("trajectory-demo.dto-local"), configGroup);

    rateSpin_ = new QDoubleSpinBox(configGroup);
    rateSpin_->setRange(0.2, 20.0);
    rateSpin_->setDecimals(1);
    rateSpin_->setSingleStep(0.5);
    rateSpin_->setValue(2.0);
    rateSpin_->setSuffix(QStringLiteral(" Hz"));

    horizonSpin_ = new QDoubleSpinBox(configGroup);
    horizonSpin_->setRange(1.0, 180.0);
    horizonSpin_->setDecimals(1);
    horizonSpin_->setSingleStep(2.0);
    horizonSpin_->setValue(24.0);
    horizonSpin_->setSuffix(QStringLiteral(" s"));

    stepSpin_ = new QDoubleSpinBox(configGroup);
    stepSpin_->setRange(0.1, 10.0);
    stepSpin_->setDecimals(1);
    stepSpin_->setSingleStep(0.5);
    stepSpin_->setValue(1.0);
    stepSpin_->setSuffix(QStringLiteral(" s"));

    maxSamplesSpin_ = new QSpinBox(configGroup);
    maxSamplesSpin_->setRange(5, 500);
    maxSamplesSpin_->setValue(80);

    simulationCheck_ = new QCheckBox(QStringLiteral("无实时数据时启用本地模拟源"), configGroup);
    simulationCheck_->setChecked(false);

    addFormRow(configForm, QStringLiteral("状态 topic"), topicEdit_);
    addFormRow(configForm, QStringLiteral("弹道 topic"), trajectoryTopicEdit_);
    addFormRow(configForm, QStringLiteral("model_id"), modelIdEdit_);
    addFormRow(configForm, QStringLiteral("rate_hz"), rateSpin_);
    addFormRow(configForm, QStringLiteral("horizon_s"), horizonSpin_);
    addFormRow(configForm, QStringLiteral("step_s"), stepSpin_);
    addFormRow(configForm, QStringLiteral("max_samples"), maxSamplesSpin_);
    addFormRow(configForm, QStringLiteral("数据源"), simulationCheck_);
    rightLayout->addWidget(configGroup);

    auto* statusGroup = new QGroupBox(QStringLiteral("freshness / status"));
    auto* statusForm = new QFormLayout(statusGroup);
    sourceValue_ = makeValueLabel();
    freshnessValue_ = makeValueLabel();
    frameIdValue_ = makeValueLabel();
    sampleCountValue_ = makeValueLabel();
    addFormRow(statusForm, QStringLiteral("source"), sourceValue_);
    addFormRow(statusForm, QStringLiteral("freshness"), freshnessValue_);
    addFormRow(statusForm, QStringLiteral("frame_id"), frameIdValue_);
    addFormRow(statusForm, QStringLiteral("samples"), sampleCountValue_);
    rightLayout->addWidget(statusGroup);

    auto* stateGroup = new QGroupBox(QStringLiteral("当前状态"));
    auto* stateForm = new QFormLayout(stateGroup);
    timeValue_ = makeValueLabel();
    distanceValue_ = makeValueLabel();
    altitudeValue_ = makeValueLabel();
    machValue_ = makeValueLabel();
    dynamicPressureValue_ = makeValueLabel();
    alphaValue_ = makeValueLabel();
    addFormRow(stateForm, QStringLiteral("time"), timeValue_);
    addFormRow(stateForm, QStringLiteral("X"), distanceValue_);
    addFormRow(stateForm, QStringLiteral("Z / h"), altitudeValue_);
    addFormRow(stateForm, QStringLiteral("Mach"), machValue_);
    addFormRow(stateForm, QStringLiteral("q"), dynamicPressureValue_);
    addFormRow(stateForm, QStringLiteral("alpha"), alphaValue_);
    rightLayout->addWidget(stateGroup);

    auto* boundary = new QLabel(QStringLiteral(
        "边界说明：本 demo 通过 ROS2 std_msgs/String 订阅公开 JSON DTO；模拟源只作 fallback，不 include、不链接 runtime-private、node-sdk/source-supported 或 trajectory 算法源码。"));
    boundary->setWordWrap(true);
    boundary->setStyleSheet(QStringLiteral("background: #eef6f3; border: 1px solid #c8ded6; border-radius: 6px; padding: 10px; color: #2f5a4c;"));
    rightLayout->addWidget(boundary);
    rightLayout->addStretch(1);

    splitter->addWidget(left);
    splitter->addWidget(right);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
}

void TrajectoryMonitorWidget::connectUi()
{
    connect(simulationCheck_, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked) {
            startSimulation();
        } else {
            stopSimulation();
        }
    });

    connect(rateSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) {
        if (simulationCheck_->isChecked()) {
            startSimulation();
        }
        updateFreshness();
    });

    connect(maxSamplesSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
        trimRecentStates();
        trimRecentTrajectories();
        rebuildRecentStateTable();
        rebuildTrajectoryHistoryTable();
        rebuildSampleTable();
    });

    connect(topicEdit_, &QLineEdit::editingFinished, this, [this]() { reconnectRos(); });
    connect(trajectoryTopicEdit_, &QLineEdit::editingFinished, this, [this]() { reconnectRos(); });
}

void TrajectoryMonitorWidget::startSimulation()
{
    if (!simulationTimer_) {
        return;
    }

    const auto config = readConfig();
    const int intervalMs = std::max(50, static_cast<int>(1000.0 / std::max(0.1, config.rate_hz)));
    simulationTimer_->start(intervalMs);

    if (!freshnessClock_.isValid()) {
        onSimulationTick();
    }
    updateFreshness();
}

void TrajectoryMonitorWidget::stopSimulation()
{
    if (simulationTimer_) {
        simulationTimer_->stop();
    }
    setStatusBadge(QStringLiteral("等待 ROS"), QStringLiteral("#7a8794"));
    sourceValue_->setText(QStringLiteral("ROS 实时订阅中"));
    updateFreshness();
}

void TrajectoryMonitorWidget::reconnectRos()
{
    ros_ = std::make_unique<flightenv::ui::demo::RosJsonSubscriber>("trajectory_monitor_ui");
    if (!ros_->ok()) {
        rosError_ = QString::fromStdString(ros_->error());
        setStatusBadge(QStringLiteral("ROS 未就绪"), QStringLiteral("#b42318"));
        sourceValue_->setText(rosError_);
        return;
    }

    const auto config = readConfig();
    rosError_.clear();
    ros_->subscribe(config.state_topic.toStdString(), [this](const std::string& topic, const std::string& payload) {
        onStatePayload(QString::fromStdString(topic), QString::fromStdString(payload));
    });
    ros_->subscribe(config.trajectory_topic.toStdString(), [this](const std::string& topic, const std::string& payload) {
        onTrajectoryPayload(QString::fromStdString(topic), QString::fromStdString(payload));
    });
    setStatusBadge(QStringLiteral("等待 ROS"), QStringLiteral("#7a8794"));
    sourceValue_->setText(QStringLiteral("订阅: %1 / %2").arg(config.state_topic, config.trajectory_topic));
}

void TrajectoryMonitorWidget::onStatePayload(const QString& topic, const QString& payload)
{
    QMetaObject::invokeMethod(this, [this, topic, payload]() {
        try {
            const auto frame = json::parse(payload.toStdString()).get<contracts::StateEstimateFrame>();
            latestState_ = frame.state;
            hasLatestState_ = true;
            recentStates_.push_front(latestState_);
            trimRecentStates();
            if (hasLatestTrajectory_) {
                plot_->setFrames(latestState_, latestTrajectory_);
                rebuildTrajectoryHistoryTable();
                rebuildSampleTable();
            }
            freshnessClock_.restart();
            updateReadouts(latestState_, latestTrajectory_, QStringLiteral("ROS 实时状态: %1").arg(topic));
            rebuildRecentStateTable();
            setStatusBadge(QStringLiteral("ROS 实时"), QStringLiteral("#25835f"));
            updateFreshness();
        } catch (const std::exception& ex) {
            rosError_ = QStringLiteral("状态 JSON 解析失败: %1").arg(QString::fromLocal8Bit(ex.what()));
            setStatusBadge(QStringLiteral("解析失败"), QStringLiteral("#b42318"));
            sourceValue_->setText(rosError_);
        }
    }, Qt::QueuedConnection);
}

void TrajectoryMonitorWidget::onTrajectoryPayload(const QString& topic, const QString& payload)
{
    QMetaObject::invokeMethod(this, [this, topic, payload]() {
        try {
            const auto frame = json::parse(payload.toStdString()).get<contracts::TrajectoryPredictionFrame>();
            latestTrajectory_ = frame;
            hasLatestTrajectory_ = true;
            if (!hasLatestState_) {
                latestState_ = stateFromTrajectory(frame);
                hasLatestState_ = true;
            }
            plot_->setFrames(latestState_, latestTrajectory_);
            recentTrajectories_.push_front(latestTrajectory_);
            trimRecentTrajectories();
            rebuildTrajectoryHistoryTable();
            rebuildSampleTable();
            freshnessClock_.restart();
            updateReadouts(latestState_, latestTrajectory_, QStringLiteral("ROS 实时弹道: %1").arg(topic));
            rebuildRecentStateTable();
            setStatusBadge(QStringLiteral("ROS 实时"), QStringLiteral("#25835f"));
            updateFreshness();
        } catch (const std::exception& ex) {
            rosError_ = QStringLiteral("弹道 JSON 解析失败: %1").arg(QString::fromLocal8Bit(ex.what()));
            setStatusBadge(QStringLiteral("解析失败"), QStringLiteral("#b42318"));
            sourceValue_->setText(rosError_);
        }
    }, Qt::QueuedConnection);
}

void TrajectoryMonitorWidget::onSimulationTick()
{
    const auto config = readConfig();
    simTime_s_ += 1.0 / std::max(0.1, config.rate_hz);

    auto state = makeStateFrame(config);
    auto trajectory = makeTrajectoryFrame(config, state);
    ingestFrame(state, trajectory, QStringLiteral("本地 DTO 模拟源"));
}

void TrajectoryMonitorWidget::updateFreshness()
{
    if (!freshnessClock_.isValid()) {
        freshnessValue_->setText(QStringLiteral("无样本"));
        if (!simulationCheck_->isChecked()) {
            setStatusBadge(rosError_.isEmpty() ? QStringLiteral("等待 ROS") : QStringLiteral("ROS 未就绪"),
                           rosError_.isEmpty() ? QStringLiteral("#7a8794") : QStringLiteral("#b42318"));
        }
        return;
    }

    const qint64 ageMs = freshnessClock_.elapsed();
    freshnessValue_->setText(QStringLiteral("%1 ms").arg(ageMs));

    const auto config = readConfig();
    const double staleLimitMs = std::max(1500.0, 2500.0 / std::max(0.1, config.rate_hz));
    if (!simulationCheck_->isChecked()) {
        setStatusBadge(ageMs > staleLimitMs ? QStringLiteral("stale / 已暂停") : QStringLiteral("paused / 新鲜"), QStringLiteral("#7a8794"));
    } else if (ageMs <= staleLimitMs) {
        setStatusBadge(QStringLiteral("fresh / 运行中"), QStringLiteral("#25835f"));
    } else {
        setStatusBadge(QStringLiteral("stale / 超时"), QStringLiteral("#bd6b28"));
    }
}

void TrajectoryMonitorWidget::updateReadouts(
    const contracts::StateFrame& state,
    const contracts::TrajectoryPredictionFrame& trajectory,
    const QString& source_label)
{
    sourceValue_->setText(source_label);
    frameIdValue_->setText(QString::fromStdString(trajectory.frame_id));
    sampleCountValue_->setText(QStringLiteral("%1 / horizon %2 / step %3")
                                   .arg(static_cast<qulonglong>(trajectory.samples.size()))
                                   .arg(secondsText(trajectory.horizon_s))
                                   .arg(secondsText(trajectory.step_s)));
    timeValue_->setText(secondsText(state.time));
    distanceValue_->setText(metersText(state.pos_x));
    altitudeValue_->setText(metersText(state.h));
    machValue_->setText(numberText(state.ma, 3));
    dynamicPressureValue_->setText(QStringLiteral("%1 Pa").arg(state.q, 0, 'f', 0));
    alphaValue_->setText(QStringLiteral("%1 deg").arg(state.alpha, 0, 'f', 2));
}

void TrajectoryMonitorWidget::rebuildRecentStateTable()
{
    recentTable_->setRowCount(static_cast<int>(recentStates_.size()));
    for (int row = 0; row < static_cast<int>(recentStates_.size()); ++row) {
        const auto& state = recentStates_[static_cast<size_t>(row)];
        recentTable_->setItem(row, 0, readOnlyItem(QString::number(state.time_id)));
        recentTable_->setItem(row, 1, readOnlyItem(numberText(state.time, 2)));
        recentTable_->setItem(row, 2, readOnlyItem(numberText(state.pos_x, 1)));
        recentTable_->setItem(row, 3, readOnlyItem(numberText(state.h, 1)));
        recentTable_->setItem(row, 4, readOnlyItem(numberText(state.ma, 3)));
        recentTable_->setItem(row, 5, readOnlyItem(QString::number(state.q, 'f', 0)));
        recentTable_->setItem(row, 6, readOnlyItem(QString::fromStdString(latestTrajectory_.status)));
    }
    recentTable_->resizeColumnsToContents();
}

void TrajectoryMonitorWidget::rebuildTrajectoryHistoryTable()
{
    if (trajectoryHistoryTable_ == nullptr) {
        return;
    }
    trajectoryHistoryTable_->setRowCount(static_cast<int>(recentTrajectories_.size()));
    for (int row = 0; row < static_cast<int>(recentTrajectories_.size()); ++row) {
        const auto& trajectory = recentTrajectories_[static_cast<size_t>(row)];
        const bool hasSamples = !trajectory.samples.empty();
        const double t0 = hasSamples ? trajectory.samples.front().time_s : 0.0;
        const double t1 = hasSamples ? trajectory.samples.back().time_s : 0.0;
        const double h0 = hasSamples ? sampleAltitude(trajectory.samples.front()) : 0.0;
        const double h1 = hasSamples ? sampleAltitude(trajectory.samples.back()) : 0.0;
        const double continuity = row == 0 && hasLatestState_
            ? startContinuityErrorM(latestState_, trajectory)
            : 0.0;
        trajectoryHistoryTable_->setItem(row, 0, readOnlyItem(QString::fromStdString(trajectory.frame_id)));
        trajectoryHistoryTable_->setItem(row, 1, readOnlyItem(QString::fromStdString(trajectory.request_frame_id)));
        trajectoryHistoryTable_->setItem(row, 2, readOnlyItem(QString::number(trajectory.samples.size())));
        trajectoryHistoryTable_->setItem(row, 3, readOnlyItem(numberText(t0, 2)));
        trajectoryHistoryTable_->setItem(row, 4, readOnlyItem(numberText(t1, 2)));
        trajectoryHistoryTable_->setItem(row, 5, readOnlyItem(numberText(trajectoryRangeM(trajectory) / 1000.0, 3)));
        trajectoryHistoryTable_->setItem(row, 6, readOnlyItem(numberText(h0, 1)));
        trajectoryHistoryTable_->setItem(row, 7, readOnlyItem(numberText(h1, 1)));
        trajectoryHistoryTable_->setItem(row, 8, readOnlyItem(row == 0 ? numberText(continuity, 2) : QStringLiteral("--")));
        trajectoryHistoryTable_->setItem(row, 9, readOnlyItem(QString::fromStdString(trajectory.status)));
    }
    trajectoryHistoryTable_->resizeColumnsToContents();
}

void TrajectoryMonitorWidget::rebuildSampleTable()
{
    if (sampleTable_ == nullptr) {
        return;
    }
    const auto& trajectory = latestTrajectory_;
    const int rowCount = static_cast<int>(std::min<size_t>(trajectory.samples.size(), 120));
    sampleTable_->setRowCount(rowCount);
    for (int row = 0; row < rowCount; ++row) {
        const size_t sampleIndex = displaySampleIndex(trajectory.samples.size(), row, rowCount);
        const auto& sample = trajectory.samples[sampleIndex];
        const double vx = sample.velocity_mps.empty() ? 0.0 : sample.velocity_mps[0];
        sampleTable_->setItem(row, 0, readOnlyItem(QString::number(static_cast<qulonglong>(sampleIndex))));
        sampleTable_->setItem(row, 1, readOnlyItem(numberText(sample.time_s, 2)));
        sampleTable_->setItem(row, 2, readOnlyItem(numberText(sampleX(sample), 1)));
        sampleTable_->setItem(row, 3, readOnlyItem(numberText(sampleAltitude(sample), 1)));
        sampleTable_->setItem(row, 4, readOnlyItem(numberText(vx, 2)));
        sampleTable_->setItem(row, 5, readOnlyItem(numberText(sample.mach, 3)));
        sampleTable_->setItem(row, 6, readOnlyItem(QString::number(sample.dynamic_pressure_pa, 'f', 0)));
        sampleTable_->setItem(row, 7, readOnlyItem(numberText(sample.normal_load_g, 3)));
        sampleTable_->setItem(row, 8, readOnlyItem(numberText(sampleAlphaDeg(sample), 3)));
    }
    sampleTable_->resizeColumnsToContents();
}

void TrajectoryMonitorWidget::trimRecentStates()
{
    const int maxRows = std::max(1, maxSamplesSpin_ ? maxSamplesSpin_->value() : 80);
    while (static_cast<int>(recentStates_.size()) > maxRows) {
        recentStates_.pop_back();
    }
}

void TrajectoryMonitorWidget::trimRecentTrajectories()
{
    const int maxRows = std::max(1, maxSamplesSpin_ ? maxSamplesSpin_->value() : 80);
    while (static_cast<int>(recentTrajectories_.size()) > maxRows) {
        recentTrajectories_.pop_back();
    }
}

TrajectoryDemoConfig TrajectoryMonitorWidget::readConfig() const
{
    TrajectoryDemoConfig config;
    config.state_topic = topicEdit_->text().trimmed();
    config.trajectory_topic = trajectoryTopicEdit_->text().trimmed();
    config.model_id = modelIdEdit_->text().trimmed();
    config.rate_hz = rateSpin_->value();
    config.horizon_s = horizonSpin_->value();
    config.step_s = stepSpin_->value();
    config.max_samples = maxSamplesSpin_->value();
    return config;
}

contracts::StateFrame TrajectoryMonitorWidget::makeStateFrame(const TrajectoryDemoConfig& config)
{
    Q_UNUSED(config);

    contracts::StateFrame state;
    state.task_id = 20260607;
    state.taskpoint_id = 1;
    state.time_id = ++frameCounter_;
    state.time = simTime_s_;

    const double wave = std::sin(simTime_s_ * 0.22);
    state.pos_x = 1800.0 + 430.0 * simTime_s_;
    state.h = std::max(600.0, 11800.0 + 1250.0 * wave - 16.0 * simTime_s_);
    state.pos_y = 0.0;
    state.pos_z = state.h;
    state.ma = 2.25 + 0.08 * std::sin(simTime_s_ * 0.31);
    state.q = 23500.0 + 3600.0 * std::cos(simTime_s_ * 0.19);
    state.alpha = 4.2 + 1.1 * std::sin(simTime_s_ * 0.17);
    state.beta = 0.18 * std::cos(simTime_s_ * 0.23);
    state.rudder1 = 0.4 * std::sin(simTime_s_ * 0.5);
    state.rudder2 = -state.rudder1;
    state.stamp_ns = currentTimestampNs();
    return state;
}

contracts::TrajectoryPredictionFrame TrajectoryMonitorWidget::makeTrajectoryFrame(
    const TrajectoryDemoConfig& config,
    const contracts::StateFrame& state) const
{
    contracts::TrajectoryPredictionFrame trajectory;
    trajectory.frame_id = QStringLiteral("traj-%1").arg(state.time_id, 5, 10, QChar('0')).toStdString();
    trajectory.run_id = "trajectory-monitor-ui-demo";
    trajectory.object_id = "demo-body";
    trajectory.request_frame_id = config.state_topic.toStdString();
    trajectory.stamp_ns = currentTimestampNs();
    trajectory.source_stamp_ns = state.stamp_ns;
    trajectory.horizon_s = config.horizon_s;
    trajectory.step_s = config.step_s;
    trajectory.status = "simulated";
    trajectory.reason = "local DTO sample source; no private runtime or trajectory core is linked";
    trajectory.model_snapshot.model_id = config.model_id.toStdString();
    trajectory.model_snapshot.model_type = "trajectory_dto_demo";
    trajectory.model_snapshot.version = "local-sim";
    trajectory.model_snapshot.artifact_uri = "local://TrajectoryMonitorUi/mock-source";

    const int requestedCount = static_cast<int>(std::floor(config.horizon_s / std::max(0.1, config.step_s))) + 1;
    const int sampleCount = std::max(2, std::min(config.max_samples, requestedCount));
    trajectory.samples.reserve(static_cast<size_t>(sampleCount));

    const double groundSpeed = 430.0 + 28.0 * std::sin(simTime_s_ * 0.11);
    const double verticalSpeed = 52.0 * std::cos(simTime_s_ * 0.15) - 7.0;
    for (int i = 0; i < sampleCount; ++i) {
        const double dt = i * config.step_s;
        const double x = state.pos_x + groundSpeed * dt;
        const double h = std::max(
            250.0,
            state.h + verticalSpeed * dt - 0.95 * dt * dt + 150.0 * std::sin((simTime_s_ + dt) * 0.2));

        contracts::TrajectorySampleDTO sample;
        sample.time_s = state.time + dt;
        sample.position_ned_m = { x, 0.0, -h };
        sample.velocity_mps = { groundSpeed, 0.0, -verticalSpeed };
        sample.velocity_frame = "Ned";
        sample.attitude_rad = { state.alpha * kPi / 180.0, state.beta * kPi / 180.0, 0.0 };
        sample.body_rates_radps = { 0.0, 0.0, 0.0 };
        sample.mach = state.ma + 0.018 * std::sin(dt * 0.35);
        sample.angle_of_attack_rad = state.alpha * kPi / 180.0;
        sample.sideslip_rad = state.beta * kPi / 180.0;
        sample.dynamic_pressure_pa = std::max(1000.0, state.q - 90.0 * dt);
        sample.normal_load_g = 1.0 + 0.08 * std::sin((simTime_s_ + dt) * 0.25);
        trajectory.samples.push_back(std::move(sample));
    }

    return trajectory;
}

QLabel* TrajectoryMonitorWidget::makeValueLabel(const QString& text) const
{
    auto* label = new QLabel(text);
    label->setMinimumHeight(28);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setStyleSheet(QStringLiteral("background: #f8fafc; border: 1px solid #d9e2ea; border-radius: 4px; padding: 4px 7px; font-weight: 500;"));
    return label;
}

void TrajectoryMonitorWidget::setStatusBadge(const QString& text, const QString& color)
{
    statusBadge_->setText(text);
    statusBadge_->setStyleSheet(QStringLiteral(
                                    "background: %1; color: white; border-radius: 14px; padding: 5px 12px; font-weight: 600;")
                                    .arg(color));
}
