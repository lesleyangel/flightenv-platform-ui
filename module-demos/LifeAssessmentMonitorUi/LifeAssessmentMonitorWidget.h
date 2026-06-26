#pragma once

#include "../common/VtkModelFieldWidget.h"

#include <EnvContracts/dto/FieldBundleDTO.hpp>
#include <EnvContracts/dto/LifeAssessmentFrame.hpp>
#include <EnvContracts/dto/RuntimeSnapshotDTO.hpp>

#include <QtCore/QDateTime>
#include <QtCore/QString>
#include <QtCore/QVector>
#include <QtWidgets/QWidget>

#include <deque>
#include <memory>
#include <optional>

namespace flightenv::ui::demo {
class RosJsonSubscriber;
}

class QLabel;
class QComboBox;
class QLineEdit;
class QDoubleSpinBox;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTextEdit;
class QTimer;

struct LifeAssessmentConfig
{
    QString modelType = QStringLiteral("life_assessment");
    QString modelId = QStringLiteral("life-baseline-linear-demo");
    QString assetId = QStringLiteral("wing-panel-A01");
    QString inputTopic = QStringLiteral("/flightenv/damage_assessment");
    QString outputTopic = QStringLiteral("/flightenv/life_assessment");
    QString runtimeSnapshotTopic = QStringLiteral("/flightenv/runtime_snapshot");
    QString lifeFieldTopic = QStringLiteral("/flightenv/life_field");
    QString assetRoot = QStringLiteral("F:/code/FlightEnvMultiRepo/_deps/example");
    QString lifeRule = QStringLiteral("first_exceed_threshold");
    double threshold = 1.0;
};

struct LifeAssessmentFrame
{
    int frameIndex = 0;
    QDateTime timestamp;
    QString sourceTopic;
    QString assetId;
    double currentDamage = 0.0;
    double damageRate = 0.0;
    double normalizedLoad = 0.0;
};

struct LifeAssessmentEstimate
{
    double currentDamage = 0.0;
    double damageRate = 0.0;
    double rulSeconds = 0.0;
    QDateTime firstExceedTime;
    QString status;
    QString sourceFrame;
};

struct LifeHistoryPoint
{
    QString frameId;
    QString sourceFrame;
    QString status;
    double damage = 0.0;
    double rulSeconds = 0.0;
    double firstExceedSeconds = -1.0;
};

class RulTrendWidget final : public QWidget
{
public:
    explicit RulTrendWidget(QWidget* parent = nullptr);

    void setSamples(const QVector<double>& samples);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QVector<double> samples_;
};

class LifeAssessmentMonitorWidget final : public QWidget
{
public:
    explicit LifeAssessmentMonitorWidget(QWidget* parent = nullptr);
    ~LifeAssessmentMonitorWidget() override;

private:
    void buildUi();
    void applyConfigFromUi();
    void resetDemo();
    void toggleRunning();
    void advanceDemoFrame();
    void reconnectRos();
    void onLifePayload(const QString& topic, const QString& payload);
    void onRuntimeSnapshotPayload(const QString& topic, const QString& payload);
    void onLifeFieldPayload(const QString& topic, const QString& payload);
    void renderLatestLifeField();
    LifeAssessmentEstimate estimateFromFrame(const LifeAssessmentFrame& frame) const;
    void updateMetrics(const LifeAssessmentFrame& frame, const LifeAssessmentEstimate& estimate);
    void updateMetricsFromContract(const contracts::LifeAssessmentFrame& frame, const QString& topic);
    void updateMetricRow(int row, const QString& key, const QString& value, const QString& note);
    void appendHistory(const LifeHistoryPoint& point);
    void updateHistoryTable();
    QString formatSeconds(double seconds) const;
    QString formatTimestamp(const QDateTime& timestamp) const;
    QString buildSourceFrameText(const LifeAssessmentFrame& frame) const;
    QString buildSourceFrameText(const contracts::LifeAssessmentFrame& frame) const;

    LifeAssessmentConfig config_;
    QTimer* demoTimer_ = nullptr;
    int frameIndex_ = 0;
    double currentDamage_ = 0.18;
    QVector<double> rulSamples_;
    std::deque<LifeHistoryPoint> history_;
    std::unique_ptr<flightenv::ui::demo::RosJsonSubscriber> ros_;
    QString rosError_;
    std::optional<contracts::RuntimeSnapshotDTO> runtimeSnapshot_;
    std::optional<contracts::FieldBundleDTO> latestLifeField_;
    contracts::TimestampNs lastRealStampNs_ = 0;
    double lastRealDamage_ = 0.0;
    bool hasLastRealDamage_ = false;

    QLineEdit* modelTypeEdit_ = nullptr;
    QLineEdit* modelIdEdit_ = nullptr;
    QLineEdit* assetIdEdit_ = nullptr;
    QLineEdit* inputTopicEdit_ = nullptr;
    QLineEdit* outputTopicEdit_ = nullptr;
    QLineEdit* runtimeSnapshotTopicEdit_ = nullptr;
    QLineEdit* lifeFieldTopicEdit_ = nullptr;
    QLineEdit* assetRootEdit_ = nullptr;
    QLineEdit* lifeRuleEdit_ = nullptr;
    QDoubleSpinBox* thresholdSpin_ = nullptr;
    QComboBox* subjectCombo_ = nullptr;
    QSpinBox* componentSpin_ = nullptr;

    QLabel* rulValueLabel_ = nullptr;
    QLabel* firstExceedLabel_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QLabel* damageValueLabel_ = nullptr;
    QLabel* rateValueLabel_ = nullptr;
    QProgressBar* rulGauge_ = nullptr;
    RulTrendWidget* trendWidget_ = nullptr;
    flightenv::ui::demo::VtkModelFieldWidget* lifeField_ = nullptr;
    QTableWidget* metricsTable_ = nullptr;
    QTableWidget* historyTable_ = nullptr;
    QTextEdit* sourceFrameText_ = nullptr;
    QPushButton* runButton_ = nullptr;
};
