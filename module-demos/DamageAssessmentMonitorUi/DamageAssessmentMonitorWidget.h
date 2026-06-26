#pragma once

#include "../common/VtkModelFieldWidget.h"

#include <EnvContracts/dto/DamageAssessmentFrame.hpp>
#include <EnvContracts/dto/FieldBundleDTO.hpp>
#include <EnvContracts/dto/RuntimeSnapshotDTO.hpp>

#include <QElapsedTimer>
#include <QString>
#include <QVector>
#include <QWidget>

#include <deque>
#include <memory>
#include <optional>

namespace flightenv::ui::demo {
class RosJsonSubscriber;
}

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QSpinBox;
class QTableWidget;
class QTimer;

struct DamageAssessmentDemoConfig {
    QString model_id = QStringLiteral("damage-linear-miner-demo");
    QString asset_id = QStringLiteral("thermal-panel-A01");
    QString input_topic = QStringLiteral("/flightenv/field_prediction");
    QString output_topic = QStringLiteral("/flightenv/damage_assessment");
    QString runtime_snapshot_topic = QStringLiteral("/flightenv/runtime_snapshot");
    QString damage_field_topic = QStringLiteral("/flightenv/damage_field");
    QString asset_root = QStringLiteral("F:/code/FlightEnvMultiRepo/_deps/example");
    QString damage_rule = QStringLiteral("linear_miner_baseline");
    double threshold = 1.0;
    double update_rate_hz = 1.0;
};

class DamageCurveWidget final : public QWidget {
public:
    explicit DamageCurveWidget(QWidget* parent = nullptr);

    void setSamples(QVector<double> samples, double threshold);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QVector<double> samples_;
    double threshold_ = 1.0;
};

class DamageAssessmentMonitorWidget final : public QWidget {
public:
    explicit DamageAssessmentMonitorWidget(QWidget* parent = nullptr);
    ~DamageAssessmentMonitorWidget() override;

private:
    void buildUi();
    void connectUi();
    void startDemo();
    void stopDemo();
    void reconnectRos();
    void onDamagePayload(const QString& topic, const QString& payload);
    void onRuntimeSnapshotPayload(const QString& topic, const QString& payload);
    void onDamageFieldPayload(const QString& topic, const QString& payload);
    void renderLatestDamageField();
    void resetDemo();
    void onDemoTick();
    void updateFreshness();
    void updateFrame(const contracts::DamageAssessmentFrame& frame);
    void updateIncrementTable(const contracts::DamageAssessmentFrame& frame);
    void updateCriterionTable(const contracts::DamageAssessmentFrame& frame);
    void updateHistoryTable();
    DamageAssessmentDemoConfig readConfig() const;
    contracts::DamageAssessmentFrame makeFrame(const DamageAssessmentDemoConfig& config);
    QLabel* makeValueLabel(const QString& text = QString()) const;
    void setStatusBadge(const QString& text, const QString& color);

    QLineEdit* modelIdEdit_ = nullptr;
    QLineEdit* assetIdEdit_ = nullptr;
    QLineEdit* inputTopicEdit_ = nullptr;
    QLineEdit* outputTopicEdit_ = nullptr;
    QLineEdit* runtimeSnapshotTopicEdit_ = nullptr;
    QLineEdit* damageFieldTopicEdit_ = nullptr;
    QLineEdit* assetRootEdit_ = nullptr;
    QLineEdit* damageRuleEdit_ = nullptr;
    QComboBox* subjectCombo_ = nullptr;
    QSpinBox* componentSpin_ = nullptr;
    QDoubleSpinBox* thresholdSpin_ = nullptr;
    QDoubleSpinBox* updateRateSpin_ = nullptr;
    QCheckBox* simulationCheck_ = nullptr;

    QLabel* statusBadge_ = nullptr;
    QLabel* frameIdValue_ = nullptr;
    QLabel* sourceFrameValue_ = nullptr;
    QLabel* incrementValue_ = nullptr;
    QLabel* cumulativeValue_ = nullptr;
    QLabel* modeValue_ = nullptr;
    QLabel* freshnessValue_ = nullptr;
    QProgressBar* damageBar_ = nullptr;
    DamageCurveWidget* curve_ = nullptr;
    flightenv::ui::demo::VtkModelFieldWidget* damageField_ = nullptr;
    QTableWidget* incrementTable_ = nullptr;
    QTableWidget* criterionTable_ = nullptr;
    QTableWidget* historyTable_ = nullptr;

    QTimer* demoTimer_ = nullptr;
    QTimer* freshnessTimer_ = nullptr;
    std::unique_ptr<flightenv::ui::demo::RosJsonSubscriber> ros_;
    QElapsedTimer freshnessClock_;
    QString rosError_;
    std::optional<contracts::RuntimeSnapshotDTO> runtimeSnapshot_;
    std::optional<contracts::FieldBundleDTO> latestDamageField_;
    QVector<double> cumulativeSamples_;
    std::deque<contracts::DamageAssessmentFrame> history_;
    int frameCounter_ = 0;
    double cumulativeDamage_ = 0.0;
};
