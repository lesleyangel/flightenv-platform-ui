#include "LifeAssessmentMonitorWidget.h"
#include "../common/RosJsonSubscriber.h"

#include <QtCore/QTimer>
#include <QtCore/QStringList>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtWidgets/QAbstractItemView>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMessageBox>
#include <QtCore/QMetaObject>
#include <QtWidgets/QProgressBar>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSizePolicy>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QTableWidget>
#include <QtWidgets/QTableWidgetItem>
#include <QtWidgets/QTextEdit>
#include <QtWidgets/QVBoxLayout>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace
{
constexpr int kMetricRows = 6;
constexpr int kMaxTrendSamples = 72;
constexpr double kMinDamageRate = 1.0e-9;
using json = nlohmann::json;

contracts::SubjectType subjectFromCombo(const QComboBox* combo)
{
    if (combo == nullptr) {
        return contracts::SubjectType::T;
    }
    return contracts::subject_type_from_string(combo->currentData().toString().toStdString());
}

QString fixedNumber(double value, int precision)
{
    if (!std::isfinite(value)) {
        return QStringLiteral("--");
    }
    return QString::number(value, 'f', precision);
}

QLabel* makeKpiCaption(const QString& text)
{
    auto* label = new QLabel(text);
    label->setStyleSheet(QStringLiteral("color:#5f6b7a;font-size:12px;"));
    return label;
}

QLabel* makeKpiValue(const QString& text)
{
    auto* label = new QLabel(text);
    label->setMinimumHeight(42);
    label->setStyleSheet(QStringLiteral("font-size:28px;font-weight:650;color:#1b2430;"));
    return label;
}

void styleGauge(QProgressBar* gauge, const QString& chunkColor)
{
    gauge->setStyleSheet(QStringLiteral(
        "QProgressBar{border:1px solid #c8d0da;border-radius:6px;"
        "background:#eef2f6;text-align:center;height:22px;color:#1f2933;}"
        "QProgressBar::chunk{border-radius:5px;background:%1;}").arg(chunkColor));
}

QDateTime dateTimeFromNs(contracts::TimestampNs stampNs)
{
    if (stampNs <= 0) {
        return QDateTime::currentDateTime();
    }
    return QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(stampNs / 1000000));
}

}

RulTrendWidget::RulTrendWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(180);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void RulTrendWidget::setSamples(const QVector<double>& samples)
{
    samples_ = samples;
    update();
}

void RulTrendWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor(248, 250, 252));

    const QRect plot = rect().adjusted(48, 18, -18, -34);
    painter.setPen(QPen(QColor(205, 213, 224), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(plot, 4, 4);

    painter.setPen(QColor(98, 111, 130));
    painter.drawText(QRect(plot.left(), plot.bottom() + 8, plot.width(), 22),
                     Qt::AlignCenter,
                     QStringLiteral("RUL 趋势（秒）"));

    if (samples_.isEmpty()) {
        painter.setPen(QColor(122, 135, 154));
        painter.drawText(plot, Qt::AlignCenter, QStringLiteral("等待演示帧"));
        return;
    }

    double maxValue = 1.0;
    for (double sample : samples_) {
        if (std::isfinite(sample)) {
            maxValue = std::max(maxValue, sample);
        }
    }

    painter.setPen(QPen(QColor(226, 232, 240), 1, Qt::DashLine));
    for (int i = 1; i < 4; ++i) {
        const int y = plot.top() + (plot.height() * i / 4);
        painter.drawLine(plot.left(), y, plot.right(), y);
    }

    painter.setPen(QColor(98, 111, 130));
    painter.drawText(QRect(0, plot.top() - 4, 44, 18),
                     Qt::AlignRight | Qt::AlignVCenter,
                     QString::number(maxValue, 'f', 0));
    painter.drawText(QRect(0, plot.bottom() - 12, 44, 18),
                     Qt::AlignRight | Qt::AlignVCenter,
                     QStringLiteral("0"));

    QPainterPath path;
    for (int i = 0; i < samples_.size(); ++i) {
        const double safeSample = std::isfinite(samples_[i]) ? samples_[i] : 0.0;
        const double xRatio = samples_.size() == 1 ? 0.0 : static_cast<double>(i) / (samples_.size() - 1);
        const double yRatio = std::clamp(safeSample / maxValue, 0.0, 1.0);
        const QPointF point(plot.left() + xRatio * plot.width(),
                            plot.bottom() - yRatio * plot.height());
        if (i == 0) {
            path.moveTo(point);
        } else {
            path.lineTo(point);
        }
    }

    painter.setPen(QPen(QColor(34, 132, 204), 2.2));
    painter.drawPath(path);

    painter.setBrush(QColor(34, 132, 204));
    painter.setPen(Qt::NoPen);
    const double lastValue = std::isfinite(samples_.back()) ? samples_.back() : 0.0;
    const QPointF lastPoint(plot.right(),
                            plot.bottom() - std::clamp(lastValue / maxValue, 0.0, 1.0) * plot.height());
    painter.drawEllipse(lastPoint, 4.0, 4.0);
}

LifeAssessmentMonitorWidget::LifeAssessmentMonitorWidget(QWidget* parent)
    : QWidget(parent)
{
    buildUi();

    demoTimer_ = new QTimer(this);
    demoTimer_->setInterval(1000);
    connect(demoTimer_, &QTimer::timeout, this, [this]() {
        advanceDemoFrame();
    });

    resetDemo();
    reconnectRos();
}

LifeAssessmentMonitorWidget::~LifeAssessmentMonitorWidget() = default;

void LifeAssessmentMonitorWidget::buildUi()
{
    setWindowTitle(QStringLiteral("寿命评估模块示意 UI"));
    setStyleSheet(QStringLiteral(
        "QWidget{font-family:'Microsoft YaHei UI','Segoe UI',sans-serif;font-size:13px;color:#1f2933;}"
        "QGroupBox{font-weight:600;border:1px solid #d7dde6;border-radius:6px;margin-top:12px;padding:12px 10px 10px 10px;}"
        "QGroupBox::title{subcontrol-origin:margin;left:10px;padding:0 4px;color:#243447;}"
        "QLineEdit,QDoubleSpinBox{padding:5px;border:1px solid #cbd5e1;border-radius:4px;background:white;}"
        "QPushButton{padding:7px 14px;border:1px solid #aeb8c6;border-radius:4px;background:#f8fafc;}"
        "QPushButton:hover{background:#eef6ff;border-color:#7aa8d8;}"
        "QTableWidget{gridline-color:#d8e0ea;border:1px solid #d8e0ea;border-radius:4px;background:white;}"
        "QHeaderView::section{background:#edf2f7;color:#324255;font-weight:600;border:0;padding:6px;}"));

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(18, 16, 18, 16);
    rootLayout->setSpacing(12);

    auto* title = new QLabel(QStringLiteral("寿命评估模块示意 UI"));
    title->setStyleSheet(QStringLiteral("font-size:24px;font-weight:700;color:#172033;"));
    auto* subtitle = new QLabel(QStringLiteral("本地 demo 使用损伤累计与损伤速率线性外推 RUL 和首超破坏时间，不接入真实模型。"));
    subtitle->setStyleSheet(QStringLiteral("color:#65758b;"));
    rootLayout->addWidget(title);
    rootLayout->addWidget(subtitle);

    auto* mainLayout = new QGridLayout();
    mainLayout->setColumnStretch(0, 3);
    mainLayout->setColumnStretch(1, 2);
    mainLayout->setHorizontalSpacing(14);
    rootLayout->addLayout(mainLayout, 1);

    auto* configGroup = new QGroupBox(QStringLiteral("配置区"));
    auto* configLayout = new QFormLayout(configGroup);
    configLayout->setLabelAlignment(Qt::AlignRight);
    configLayout->setFormAlignment(Qt::AlignTop);
    modelTypeEdit_ = new QLineEdit(config_.modelType);
    modelTypeEdit_->setReadOnly(true);
    modelIdEdit_ = new QLineEdit(config_.modelId);
    assetIdEdit_ = new QLineEdit(config_.assetId);
    inputTopicEdit_ = new QLineEdit(config_.inputTopic);
    outputTopicEdit_ = new QLineEdit(config_.outputTopic);
    runtimeSnapshotTopicEdit_ = new QLineEdit(config_.runtimeSnapshotTopic);
    lifeFieldTopicEdit_ = new QLineEdit(config_.lifeFieldTopic);
    assetRootEdit_ = new QLineEdit(config_.assetRoot);
    lifeRuleEdit_ = new QLineEdit(config_.lifeRule);
    thresholdSpin_ = new QDoubleSpinBox();
    thresholdSpin_->setRange(0.01, 100.0);
    thresholdSpin_->setDecimals(3);
    thresholdSpin_->setSingleStep(0.05);
    thresholdSpin_->setValue(config_.threshold);

    configLayout->addRow(QStringLiteral("model_type"), modelTypeEdit_);
    configLayout->addRow(QStringLiteral("model_id"), modelIdEdit_);
    configLayout->addRow(QStringLiteral("asset_id"), assetIdEdit_);
    configLayout->addRow(QStringLiteral("input_topic"), inputTopicEdit_);
    configLayout->addRow(QStringLiteral("output_topic"), outputTopicEdit_);
    configLayout->addRow(QStringLiteral("runtime_snapshot"), runtimeSnapshotTopicEdit_);
    configLayout->addRow(QStringLiteral("life_field"), lifeFieldTopicEdit_);
    configLayout->addRow(QStringLiteral("asset_root"), assetRootEdit_);
    configLayout->addRow(QStringLiteral("life_rule"), lifeRuleEdit_);
    configLayout->addRow(QStringLiteral("threshold"), thresholdSpin_);

    auto* controlLayout = new QHBoxLayout();
    auto* applyButton = new QPushButton(QStringLiteral("应用配置"));
    runButton_ = new QPushButton(QStringLiteral("开始演示"));
    auto* resetButton = new QPushButton(QStringLiteral("复位"));
    controlLayout->addWidget(applyButton);
    controlLayout->addWidget(runButton_);
    controlLayout->addWidget(resetButton);
    controlLayout->addStretch();
    configLayout->addRow(QString(), controlLayout);

    connect(applyButton, &QPushButton::clicked, this, [this]() {
        applyConfigFromUi();
        resetDemo();
        reconnectRos();
    });
    connect(runButton_, &QPushButton::clicked, this, [this]() {
        toggleRunning();
    });
    connect(resetButton, &QPushButton::clicked, this, [this]() {
        resetDemo();
    });
    connect(outputTopicEdit_, &QLineEdit::editingFinished, this, [this]() {
        applyConfigFromUi();
        reconnectRos();
    });
    connect(runtimeSnapshotTopicEdit_, &QLineEdit::editingFinished, this, [this]() {
        applyConfigFromUi();
        reconnectRos();
    });
    connect(lifeFieldTopicEdit_, &QLineEdit::editingFinished, this, [this]() {
        applyConfigFromUi();
        reconnectRos();
    });
    connect(assetRootEdit_, &QLineEdit::editingFinished, this, [this]() {
        applyConfigFromUi();
        if (lifeField_) {
            lifeField_->setAssetRoot(config_.assetRoot);
        }
    });
    connect(subjectCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        renderLatestLifeField();
    });
    connect(componentSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
        renderLatestLifeField();
    });

    mainLayout->addWidget(configGroup, 0, 0);

    auto* kpiGroup = new QGroupBox(QStringLiteral("RUL 与首超估计"));
    auto* kpiLayout = new QGridLayout(kpiGroup);
    kpiLayout->setColumnStretch(0, 1);
    kpiLayout->setColumnStretch(1, 1);
    kpiLayout->setColumnStretch(2, 1);

    rulValueLabel_ = makeKpiValue(QStringLiteral("--"));
    firstExceedLabel_ = makeKpiValue(QStringLiteral("--"));
    firstExceedLabel_->setStyleSheet(QStringLiteral("font-size:20px;font-weight:650;color:#1b2430;"));
    statusLabel_ = makeKpiValue(QStringLiteral("--"));
    damageValueLabel_ = makeKpiValue(QStringLiteral("--"));
    rateValueLabel_ = makeKpiValue(QStringLiteral("--"));

    kpiLayout->addWidget(makeKpiCaption(QStringLiteral("剩余寿命 RUL")), 0, 0);
    kpiLayout->addWidget(makeKpiCaption(QStringLiteral("首超破坏时间估计")), 0, 1);
    kpiLayout->addWidget(makeKpiCaption(QStringLiteral("状态")), 0, 2);
    kpiLayout->addWidget(rulValueLabel_, 1, 0);
    kpiLayout->addWidget(firstExceedLabel_, 1, 1);
    kpiLayout->addWidget(statusLabel_, 1, 2);
    kpiLayout->addWidget(makeKpiCaption(QStringLiteral("current_damage")), 2, 0);
    kpiLayout->addWidget(makeKpiCaption(QStringLiteral("damage_rate")), 2, 1);
    kpiLayout->addWidget(new QLabel(QStringLiteral("demo 算法：remaining / damage_rate")), 2, 2);
    kpiLayout->addWidget(damageValueLabel_, 3, 0);
    kpiLayout->addWidget(rateValueLabel_, 3, 1);

    rulGauge_ = new QProgressBar();
    rulGauge_->setRange(0, 1000);
    rulGauge_->setTextVisible(true);
    rulGauge_->setFormat(QStringLiteral("剩余寿命 %p%"));
    styleGauge(rulGauge_, QStringLiteral("#2f9e44"));
    kpiLayout->addWidget(rulGauge_, 4, 0, 1, 3);

    mainLayout->addWidget(kpiGroup, 1, 0);

    auto* trendGroup = new QGroupBox(QStringLiteral("RUL 趋势"));
    auto* trendLayout = new QVBoxLayout(trendGroup);
    trendWidget_ = new RulTrendWidget();
    trendLayout->addWidget(trendWidget_);
    mainLayout->addWidget(trendGroup, 2, 0);

    auto* cloudGroup = new QGroupBox(QStringLiteral("飞船模型 VTK 剩余寿命场"));
    auto* cloudLayout = new QVBoxLayout(cloudGroup);
    auto* fieldRow = new QHBoxLayout();
    fieldRow->addWidget(new QLabel(QStringLiteral("subject")));
    subjectCombo_ = new QComboBox(cloudGroup);
    subjectCombo_->addItem(QStringLiteral("T / 温度寿命"), QStringLiteral("T"));
    subjectCombo_->addItem(QStringLiteral("S / 强度寿命"), QStringLiteral("S"));
    subjectCombo_->addItem(QStringLiteral("P / 压力寿命"), QStringLiteral("P"));
    subjectCombo_->addItem(QStringLiteral("K / 热流寿命"), QStringLiteral("K"));
    fieldRow->addWidget(subjectCombo_, 1);
    fieldRow->addWidget(new QLabel(QStringLiteral("component")));
    componentSpin_ = new QSpinBox(cloudGroup);
    componentSpin_->setRange(0, 8);
    componentSpin_->setValue(0);
    fieldRow->addWidget(componentSpin_);
    cloudLayout->addLayout(fieldRow);
    lifeField_ = new flightenv::ui::demo::VtkModelFieldWidget(cloudGroup);
    cloudLayout->addWidget(lifeField_, 1);
    mainLayout->addWidget(cloudGroup, 3, 0);

    auto* metricsGroup = new QGroupBox(QStringLiteral("评估帧字段"));
    auto* metricsLayout = new QVBoxLayout(metricsGroup);
    metricsTable_ = new QTableWidget(kMetricRows, 3);
    metricsTable_->setHorizontalHeaderLabels({QStringLiteral("字段"), QStringLiteral("值"), QStringLiteral("说明")});
    metricsTable_->verticalHeader()->setVisible(false);
    metricsTable_->horizontalHeader()->setStretchLastSection(true);
    metricsTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    metricsTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    metricsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    metricsTable_->setSelectionMode(QAbstractItemView::NoSelection);
    metricsTable_->setFocusPolicy(Qt::NoFocus);
    metricsTable_->setAlternatingRowColors(true);
    metricsLayout->addWidget(metricsTable_);
    mainLayout->addWidget(metricsGroup, 0, 1, 2, 1);

    auto* historyGroup = new QGroupBox(QStringLiteral("历史寿命估计"));
    auto* historyLayout = new QVBoxLayout(historyGroup);
    historyTable_ = new QTableWidget(0, 6);
    historyTable_->setHorizontalHeaderLabels({
        QStringLiteral("frame_id"),
        QStringLiteral("source"),
        QStringLiteral("damage"),
        QStringLiteral("RUL"),
        QStringLiteral("first_exceed"),
        QStringLiteral("status")
    });
    historyTable_->verticalHeader()->setVisible(false);
    historyTable_->horizontalHeader()->setStretchLastSection(true);
    historyTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    historyTable_->setSelectionMode(QAbstractItemView::NoSelection);
    historyLayout->addWidget(historyTable_);
    mainLayout->addWidget(historyGroup, 3, 1);

    auto* sourceGroup = new QGroupBox(QStringLiteral("source frame"));
    auto* sourceLayout = new QVBoxLayout(sourceGroup);
    sourceFrameText_ = new QTextEdit();
    sourceFrameText_->setReadOnly(true);
    sourceFrameText_->setMinimumHeight(170);
    sourceFrameText_->setStyleSheet(QStringLiteral("QTextEdit{font-family:Consolas,'Microsoft YaHei UI';font-size:12px;}"));
    sourceLayout->addWidget(sourceFrameText_);
    mainLayout->addWidget(sourceGroup, 2, 1);
}

void LifeAssessmentMonitorWidget::applyConfigFromUi()
{
    config_.modelId = modelIdEdit_->text().trimmed();
    config_.assetId = assetIdEdit_->text().trimmed();
    config_.inputTopic = inputTopicEdit_->text().trimmed();
    config_.outputTopic = outputTopicEdit_->text().trimmed();
    config_.runtimeSnapshotTopic = runtimeSnapshotTopicEdit_->text().trimmed();
    config_.lifeFieldTopic = lifeFieldTopicEdit_->text().trimmed();
    config_.assetRoot = assetRootEdit_->text().trimmed();
    config_.lifeRule = lifeRuleEdit_->text().trimmed();
    config_.threshold = thresholdSpin_->value();
}

void LifeAssessmentMonitorWidget::resetDemo()
{
    if (demoTimer_->isActive()) {
        demoTimer_->stop();
    }
    if (runButton_ != nullptr) {
        runButton_->setText(QStringLiteral("开始演示"));
    }

    frameIndex_ = 0;
    currentDamage_ = std::clamp(config_.threshold * 0.18, 0.0, config_.threshold * 0.95);
    rulSamples_.clear();
    history_.clear();

    const LifeAssessmentFrame frame{
        frameIndex_,
        QDateTime::currentDateTime(),
        config_.inputTopic,
        config_.assetId,
        currentDamage_,
        config_.threshold * 0.0035,
        0.55
    };
    updateMetrics(frame, estimateFromFrame(frame));
}

void LifeAssessmentMonitorWidget::toggleRunning()
{
    if (demoTimer_->isActive()) {
        demoTimer_->stop();
        runButton_->setText(QStringLiteral("继续演示"));
        return;
    }

    applyConfigFromUi();
    demoTimer_->start();
    runButton_->setText(QStringLiteral("暂停演示"));
}

void LifeAssessmentMonitorWidget::advanceDemoFrame()
{
    applyConfigFromUi();
    ++frameIndex_;

    const double loadWave = 0.5 + 0.5 * std::sin(frameIndex_ * 0.37);
    const double baseRate = std::max(config_.threshold, 0.01) * 0.0024;
    const double damageRate = baseRate * (0.75 + loadWave * 0.85);
    currentDamage_ = std::min(currentDamage_ + damageRate, config_.threshold * 1.05);

    const LifeAssessmentFrame frame{
        frameIndex_,
        QDateTime::currentDateTime(),
        config_.inputTopic,
        config_.assetId,
        currentDamage_,
        damageRate,
        loadWave
    };

    updateMetrics(frame, estimateFromFrame(frame));
}

void LifeAssessmentMonitorWidget::reconnectRos()
{
    ros_ = std::make_unique<flightenv::ui::demo::RosJsonSubscriber>("life_assessment_monitor_ui");
    if (!ros_->ok()) {
        rosError_ = QString::fromStdString(ros_->error());
        sourceFrameText_->setPlainText(rosError_);
        statusLabel_->setText(QStringLiteral("ROS 未就绪"));
        statusLabel_->setStyleSheet(QStringLiteral("font-size:28px;font-weight:650;color:#b42318;"));
        return;
    }

    rosError_.clear();
    if (lifeField_) {
        lifeField_->setAssetRoot(config_.assetRoot);
    }
    ros_->subscribe(config_.outputTopic.toStdString(), [this](const std::string& topic, const std::string& payload) {
        onLifePayload(QString::fromStdString(topic), QString::fromStdString(payload));
    });
    ros_->subscribe(config_.runtimeSnapshotTopic.toStdString(), [this](const std::string& topic, const std::string& payload) {
        onRuntimeSnapshotPayload(QString::fromStdString(topic), QString::fromStdString(payload));
    });
    ros_->subscribe(config_.lifeFieldTopic.toStdString(), [this](const std::string& topic, const std::string& payload) {
        onLifeFieldPayload(QString::fromStdString(topic), QString::fromStdString(payload));
    });
    sourceFrameText_->setPlainText(QStringLiteral("订阅: %1\n寿命场: %2\n等待寿命评估节点实时帧...")
                                       .arg(config_.outputTopic, config_.lifeFieldTopic));
    statusLabel_->setText(QStringLiteral("等待 ROS"));
    statusLabel_->setStyleSheet(QStringLiteral("font-size:28px;font-weight:650;color:#7a8794;"));
}

void LifeAssessmentMonitorWidget::onLifePayload(const QString& topic, const QString& payload)
{
    QMetaObject::invokeMethod(this, [this, topic, payload]() {
        try {
            const auto frame = json::parse(payload.toStdString()).get<contracts::LifeAssessmentFrame>();
            if (demoTimer_->isActive()) {
                demoTimer_->stop();
                runButton_->setText(QStringLiteral("继续本地演示"));
            }
            updateMetricsFromContract(frame, topic);
        } catch (const std::exception& ex) {
            rosError_ = QStringLiteral("寿命 JSON 解析失败: %1").arg(QString::fromLocal8Bit(ex.what()));
            sourceFrameText_->setPlainText(rosError_);
            statusLabel_->setText(QStringLiteral("解析失败"));
            statusLabel_->setStyleSheet(QStringLiteral("font-size:28px;font-weight:650;color:#b42318;"));
        }
    }, Qt::QueuedConnection);
}

void LifeAssessmentMonitorWidget::onRuntimeSnapshotPayload(const QString& topic, const QString& payload)
{
    QMetaObject::invokeMethod(this, [this, topic, payload]() {
        try {
            runtimeSnapshot_ = json::parse(payload.toStdString()).get<contracts::RuntimeSnapshotDTO>();
            if (lifeField_) {
                lifeField_->setAssetRoot(config_.assetRoot);
                lifeField_->setRuntimeSnapshot(*runtimeSnapshot_);
            }
            sourceFrameText_->setPlainText(QStringLiteral("%1\nruntime snapshot: fields=%2 meshes=%3")
                                               .arg(topic)
                                               .arg(runtimeSnapshot_->field_layouts.size())
                                               .arg(runtimeSnapshot_->meshes.size()));
            renderLatestLifeField();
        } catch (const std::exception& ex) {
            rosError_ = QStringLiteral("RuntimeSnapshotDTO JSON 解析失败: %1").arg(QString::fromLocal8Bit(ex.what()));
            sourceFrameText_->setPlainText(rosError_);
            statusLabel_->setText(QStringLiteral("布局解析失败"));
            statusLabel_->setStyleSheet(QStringLiteral("font-size:28px;font-weight:650;color:#b42318;"));
        }
    }, Qt::QueuedConnection);
}

void LifeAssessmentMonitorWidget::onLifeFieldPayload(const QString& topic, const QString& payload)
{
    QMetaObject::invokeMethod(this, [this, topic, payload]() {
        try {
            latestLifeField_ = json::parse(payload.toStdString()).get<contracts::FieldBundleDTO>();
            sourceFrameText_->setPlainText(QStringLiteral("%1\n已收到真实剩余寿命场").arg(topic));
            renderLatestLifeField();
        } catch (const std::exception& ex) {
            rosError_ = QStringLiteral("Life FieldBundleDTO JSON 解析失败: %1").arg(QString::fromLocal8Bit(ex.what()));
            sourceFrameText_->setPlainText(rosError_);
            statusLabel_->setText(QStringLiteral("寿命场解析失败"));
            statusLabel_->setStyleSheet(QStringLiteral("font-size:28px;font-weight:650;color:#b42318;"));
        }
    }, Qt::QueuedConnection);
}

void LifeAssessmentMonitorWidget::renderLatestLifeField()
{
    if (lifeField_ == nullptr) {
        return;
    }
    if (!runtimeSnapshot_) {
        lifeField_->clearField(QStringLiteral("等待 RuntimeSnapshotDTO：需要真实飞船模型节点和面片索引后才能绘制剩余寿命场"));
        return;
    }
    if (!latestLifeField_) {
        lifeField_->clearField(QStringLiteral("等待 FieldBundleDTO：LifeAssessmentFrame 只是标量摘要，不能生成真实剩余寿命云图"));
        return;
    }

    const auto stats = lifeField_->renderFieldBundle(*latestLifeField_,
                                                     subjectFromCombo(subjectCombo_),
                                                     componentSpin_->value(),
                                                     QStringLiteral("剩余寿命场"),
                                                     QStringLiteral("s"));
    if (!stats.ok) {
        sourceFrameText_->setPlainText(stats.message);
        statusLabel_->setText(QStringLiteral("寿命场不可用"));
        statusLabel_->setStyleSheet(QStringLiteral("font-size:28px;font-weight:650;color:#b54708;"));
        return;
    }

    rulValueLabel_->setText(formatSeconds(stats.minValue));
    firstExceedLabel_->setText(stats.minValue <= 0.0 ? QStringLiteral("已首超") : QStringLiteral("min_node=%1").arg(stats.minNodeIndex));
    statusLabel_->setText(stats.minValue <= 0.0 ? QStringLiteral("failed") : QStringLiteral("field ok"));
    statusLabel_->setStyleSheet(QStringLiteral("font-size:28px;font-weight:650;color:%1;")
                                    .arg(stats.minValue <= 0.0 ? QStringLiteral("#b42318") : QStringLiteral("#2f7d32")));
}

LifeAssessmentEstimate LifeAssessmentMonitorWidget::estimateFromFrame(const LifeAssessmentFrame& frame) const
{
    LifeAssessmentEstimate estimate;
    estimate.currentDamage = frame.currentDamage;
    estimate.damageRate = frame.damageRate;
    estimate.sourceFrame = buildSourceFrameText(frame);

    const double threshold = std::max(config_.threshold, 0.01);
    const double remaining = threshold - frame.currentDamage;
    if (remaining <= 0.0) {
        estimate.rulSeconds = 0.0;
        estimate.firstExceedTime = frame.timestamp;
        estimate.status = QStringLiteral("已超阈值");
        return estimate;
    }

    if (frame.damageRate <= kMinDamageRate) {
        estimate.rulSeconds = std::numeric_limits<double>::infinity();
        estimate.status = QStringLiteral("等待速率");
        return estimate;
    }

    estimate.rulSeconds = remaining / frame.damageRate;
    estimate.firstExceedTime = frame.timestamp.addMSecs(static_cast<qint64>(estimate.rulSeconds * 1000.0));

    const double remainingRatio = remaining / threshold;
    if (remainingRatio <= 0.15 || estimate.rulSeconds <= 120.0) {
        estimate.status = QStringLiteral("临近阈值");
    } else if (remainingRatio <= 0.35) {
        estimate.status = QStringLiteral("预警");
    } else {
        estimate.status = QStringLiteral("正常");
    }
    return estimate;
}

void LifeAssessmentMonitorWidget::updateMetrics(const LifeAssessmentFrame& frame,
                                                const LifeAssessmentEstimate& estimate)
{
    const double threshold = std::max(config_.threshold, 0.01);
    const double remainingRatio = std::clamp((threshold - estimate.currentDamage) / threshold, 0.0, 1.0);

    rulValueLabel_->setText(formatSeconds(estimate.rulSeconds));
    firstExceedLabel_->setText(formatTimestamp(estimate.firstExceedTime));
    statusLabel_->setText(estimate.status);
    damageValueLabel_->setText(fixedNumber(estimate.currentDamage, 4));
    rateValueLabel_->setText(fixedNumber(estimate.damageRate, 6) + QStringLiteral(" /s"));

    if (estimate.status == QStringLiteral("已超阈值")) {
        statusLabel_->setStyleSheet(QStringLiteral("font-size:28px;font-weight:650;color:#b42318;"));
        styleGauge(rulGauge_, QStringLiteral("#d92d20"));
    } else if (estimate.status == QStringLiteral("临近阈值") || estimate.status == QStringLiteral("预警")) {
        statusLabel_->setStyleSheet(QStringLiteral("font-size:28px;font-weight:650;color:#b54708;"));
        styleGauge(rulGauge_, QStringLiteral("#f59f00"));
    } else {
        statusLabel_->setStyleSheet(QStringLiteral("font-size:28px;font-weight:650;color:#2f7d32;"));
        styleGauge(rulGauge_, QStringLiteral("#2f9e44"));
    }

    rulGauge_->setValue(static_cast<int>(remainingRatio * 1000.0));

    const double trendSample = std::isfinite(estimate.rulSeconds) ? estimate.rulSeconds : 0.0;
    rulSamples_.push_back(trendSample);
    while (rulSamples_.size() > kMaxTrendSamples) {
        rulSamples_.remove(0);
    }
    trendWidget_->setSamples(rulSamples_);

    updateMetricRow(0,
                    QStringLiteral("current_damage"),
                    fixedNumber(estimate.currentDamage, 4),
                    QStringLiteral("累计损伤，阈值为 %1").arg(fixedNumber(config_.threshold, 3)));
    updateMetricRow(1,
                    QStringLiteral("damage_rate"),
                    fixedNumber(estimate.damageRate, 6),
                    QStringLiteral("本地演示帧估计的损伤增长率"));
    updateMetricRow(2,
                    QStringLiteral("rul_s"),
                    std::isfinite(estimate.rulSeconds) ? fixedNumber(estimate.rulSeconds, 1) : QStringLiteral("--"),
                    QStringLiteral("(threshold - current_damage) / damage_rate"));
    updateMetricRow(3,
                    QStringLiteral("first_exceed_time"),
                    formatTimestamp(estimate.firstExceedTime),
                    QStringLiteral("按当前速率推算的首次超阈值时间"));
    updateMetricRow(4,
                    QStringLiteral("status"),
                    estimate.status,
                    QStringLiteral("仅用于 UI 示意，不代表真实结构安全结论"));
    updateMetricRow(5,
                    QStringLiteral("source frame"),
                    QStringLiteral("#%1").arg(frame.frameIndex),
                    QStringLiteral("来自本地 timer 生成的 demo frame"));

    appendHistory({
        QStringLiteral("local-%1").arg(frame.frameIndex),
        frame.sourceTopic,
        estimate.status,
        estimate.currentDamage,
        estimate.rulSeconds,
        estimate.rulSeconds
    });

    sourceFrameText_->setPlainText(estimate.sourceFrame);
}

void LifeAssessmentMonitorWidget::updateMetricsFromContract(const contracts::LifeAssessmentFrame& frame,
                                                            const QString& topic)
{
    const QDateTime timestamp = dateTimeFromNs(frame.stamp_ns);
    const double damage = frame.health_state.damage_index;
    double damageRate = 0.0;
    if (hasLastRealDamage_ && frame.stamp_ns > lastRealStampNs_) {
        const double dt = static_cast<double>(frame.stamp_ns - lastRealStampNs_) * 1.0e-9;
        if (dt > 0.0) {
            damageRate = std::max(0.0, (damage - lastRealDamage_) / dt);
        }
    }
    hasLastRealDamage_ = true;
    lastRealStampNs_ = frame.stamp_ns;
    lastRealDamage_ = damage;

    const QString status = QString::fromStdString(!frame.health_state.status.empty() ? frame.health_state.status : frame.status);
    const double threshold = std::max(config_.threshold, 0.01);
    const double remainingRatio = std::clamp((threshold - damage) / threshold, 0.0, 1.0);
    const QDateTime exceedTime =
        (std::isfinite(frame.first_limit_exceedance_s) && frame.first_limit_exceedance_s >= 0.0)
            ? timestamp.addMSecs(static_cast<qint64>(frame.first_limit_exceedance_s * 1000.0))
            : QDateTime{};

    rulValueLabel_->setText(formatSeconds(frame.rul_s));
    firstExceedLabel_->setText(formatTimestamp(exceedTime));
    statusLabel_->setText(status);
    damageValueLabel_->setText(fixedNumber(damage, 4));
    rateValueLabel_->setText(fixedNumber(damageRate, 6) + QStringLiteral(" /s"));

    if (status == QStringLiteral("failed")) {
        statusLabel_->setStyleSheet(QStringLiteral("font-size:28px;font-weight:650;color:#b42318;"));
        styleGauge(rulGauge_, QStringLiteral("#d92d20"));
    } else if (status != QStringLiteral("ok")) {
        statusLabel_->setStyleSheet(QStringLiteral("font-size:28px;font-weight:650;color:#b54708;"));
        styleGauge(rulGauge_, QStringLiteral("#f59f00"));
    } else {
        statusLabel_->setStyleSheet(QStringLiteral("font-size:28px;font-weight:650;color:#2f7d32;"));
        styleGauge(rulGauge_, QStringLiteral("#2f9e44"));
    }
    rulGauge_->setValue(static_cast<int>(remainingRatio * 1000.0));

    rulSamples_.push_back(std::isfinite(frame.rul_s) && frame.rul_s >= 0.0 ? frame.rul_s : 0.0);
    while (rulSamples_.size() > kMaxTrendSamples) {
        rulSamples_.remove(0);
    }
    trendWidget_->setSamples(rulSamples_);

    updateMetricRow(0,
                    QStringLiteral("damage_index"),
                    fixedNumber(damage, 4),
                    QStringLiteral("来自 health_state.damage_index"));
    updateMetricRow(1,
                    QStringLiteral("damage_rate"),
                    fixedNumber(damageRate, 6),
                    QStringLiteral("由相邻 ROS 寿命帧估算，仅用于显示变化速度"));
    updateMetricRow(2,
                    QStringLiteral("rul_s"),
                    std::isfinite(frame.rul_s) ? fixedNumber(frame.rul_s, 1) : QStringLiteral("--"),
                    QStringLiteral("寿命节点发布的剩余寿命"));
    updateMetricRow(3,
                    QStringLiteral("first_limit_exceedance_s"),
                    std::isfinite(frame.first_limit_exceedance_s) ? fixedNumber(frame.first_limit_exceedance_s, 1) : QStringLiteral("--"),
                    QStringLiteral("寿命节点发布的首超时间"));
    updateMetricRow(4,
                    QStringLiteral("status"),
                    status,
                    QStringLiteral("ROS 实时帧状态"));
    updateMetricRow(5,
                    QStringLiteral("source frame"),
                    QString::fromStdString(frame.frame_id),
                    QStringLiteral("来自 %1，source=%2").arg(topic, QString::fromStdString(frame.source_frame_id)));

    appendHistory({
        QString::fromStdString(frame.frame_id),
        QString::fromStdString(frame.source_frame_id),
        status,
        damage,
        frame.rul_s,
        frame.first_limit_exceedance_s
    });

    sourceFrameText_->setPlainText(buildSourceFrameText(frame));
}

void LifeAssessmentMonitorWidget::appendHistory(const LifeHistoryPoint& point)
{
    history_.push_front(point);
    while (history_.size() > 80) {
        history_.pop_back();
    }
    updateHistoryTable();
}

void LifeAssessmentMonitorWidget::updateHistoryTable()
{
    if (historyTable_ == nullptr) {
        return;
    }
    historyTable_->setRowCount(static_cast<int>(history_.size()));
    for (int row = 0; row < static_cast<int>(history_.size()); ++row) {
        const auto& point = history_[static_cast<size_t>(row)];
        const QStringList values{
            point.frameId,
            point.sourceFrame,
            fixedNumber(point.damage, 5),
            formatSeconds(point.rulSeconds),
            point.firstExceedSeconds >= 0.0 ? formatSeconds(point.firstExceedSeconds) : QStringLiteral("--"),
            point.status
        };
        for (int column = 0; column < values.size(); ++column) {
            auto* cell = new QTableWidgetItem(values[column]);
            cell->setFlags(cell->flags() & ~Qt::ItemIsEditable);
            cell->setTextAlignment(Qt::AlignCenter);
            historyTable_->setItem(row, column, cell);
        }
    }
    historyTable_->resizeColumnsToContents();
}

void LifeAssessmentMonitorWidget::updateMetricRow(int row,
                                                  const QString& key,
                                                  const QString& value,
                                                  const QString& note)
{
    const QStringList values{key, value, note};
    for (int column = 0; column < values.size(); ++column) {
        auto* item = metricsTable_->item(row, column);
        if (item == nullptr) {
            item = new QTableWidgetItem();
            metricsTable_->setItem(row, column, item);
        }
        item->setText(values[column]);
    }
}

QString LifeAssessmentMonitorWidget::formatSeconds(double seconds) const
{
    if (!std::isfinite(seconds)) {
        return QStringLiteral("--");
    }
    if (seconds <= 0.0) {
        return QStringLiteral("0 s");
    }
    if (seconds < 60.0) {
        return QStringLiteral("%1 s").arg(QString::number(seconds, 'f', 1));
    }
    if (seconds < 3600.0) {
        return QStringLiteral("%1 min").arg(QString::number(seconds / 60.0, 'f', 1));
    }
    return QStringLiteral("%1 h").arg(QString::number(seconds / 3600.0, 'f', 2));
}

QString LifeAssessmentMonitorWidget::formatTimestamp(const QDateTime& timestamp) const
{
    if (!timestamp.isValid()) {
        return QStringLiteral("未估计");
    }
    return timestamp.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

QString LifeAssessmentMonitorWidget::buildSourceFrameText(const LifeAssessmentFrame& frame) const
{
    QStringList lines;
    lines << QStringLiteral("{")
          << QStringLiteral("  \"source\": \"local_linear_extrapolation_demo\",")
          << QStringLiteral("  \"frame_index\": %1,").arg(frame.frameIndex)
          << QStringLiteral("  \"timestamp\": \"%1\",").arg(frame.timestamp.toString(Qt::ISODateWithMs))
          << QStringLiteral("  \"asset_id\": \"%1\",").arg(frame.assetId)
          << QStringLiteral("  \"input_topic\": \"%1\",").arg(frame.sourceTopic)
          << QStringLiteral("  \"output_topic\": \"%1\",").arg(config_.outputTopic)
          << QStringLiteral("  \"current_damage\": %1,").arg(fixedNumber(frame.currentDamage, 6))
          << QStringLiteral("  \"damage_rate\": %1,").arg(fixedNumber(frame.damageRate, 8))
          << QStringLiteral("  \"normalized_load\": %1,").arg(fixedNumber(frame.normalizedLoad, 4))
          << QStringLiteral("  \"life_rule\": \"%1\",").arg(config_.lifeRule)
          << QStringLiteral("  \"threshold\": %1").arg(fixedNumber(config_.threshold, 6))
          << QStringLiteral("}");
    return lines.join(QStringLiteral("\n"));
}

QString LifeAssessmentMonitorWidget::buildSourceFrameText(const contracts::LifeAssessmentFrame& frame) const
{
    json value = frame;
    return QString::fromStdString(value.dump(2));
}
