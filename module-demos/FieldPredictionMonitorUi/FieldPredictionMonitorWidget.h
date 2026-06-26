#pragma once

#include "../common/VtkModelFieldWidget.h"

#include <EnvContracts/dto/FieldPredictionFrame.hpp>
#include <EnvContracts/dto/PredictionResultDTO.hpp>
#include <EnvContracts/dto/RuntimeSnapshotDTO.hpp>

#include <QElapsedTimer>
#include <QString>
#include <QVector>
#include <QWidget>

#include <deque>
#include <memory>
#include <optional>
#include <vector>

namespace flightenv::ui::demo {
class RosJsonSubscriber;
}

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QTableWidget;
class QTimer;

struct FieldPredictionDemoConfig {
    QString model_id = QStringLiteral("field-baseline-grid-demo");
    QString asset_id = QStringLiteral("thermal-panel-A01");
    QString input_topic = QStringLiteral("/flightenv/trajectory_prediction");
    QString output_topic = QStringLiteral("/flightenv/field_prediction");
    QString runtime_snapshot_topic = QStringLiteral("/flightenv/runtime_snapshot");
    QString prediction_result_topic = QStringLiteral("/flightenv/prediction_result");
    QString asset_root = QStringLiteral("F:/code/FlightEnvMultiRepo/_deps/example");
    QString algorithm = QStringLiteral("altitude_time_decay_baseline");
    int grid_size = 28;
    double update_rate_hz = 1.0;
};

class FieldPredictionMonitorWidget final : public QWidget {
public:
    explicit FieldPredictionMonitorWidget(QWidget* parent = nullptr);
    ~FieldPredictionMonitorWidget() override;

private:
    void buildUi();
    void connectUi();
    void startDemo();
    void stopDemo();
    void reconnectRos();
    void onFieldPayload(const QString& topic, const QString& payload);
    void onRuntimeSnapshotPayload(const QString& topic, const QString& payload);
    void onPredictionResultPayload(const QString& topic, const QString& payload);
    void onDemoTick();
    void updateFreshness();
    void updateFrame(const contracts::FieldPredictionFrame& frame, const QVector<double>& grid);
    void updateSummaryTable(const contracts::FieldPredictionFrame& frame);
    void updateHistoryTable();
    FieldPredictionDemoConfig readConfig() const;
    contracts::FieldPredictionFrame makeFrame(const FieldPredictionDemoConfig& config, QVector<double>* grid);
    QLabel* makeValueLabel(const QString& text = QString()) const;
    void setStatusBadge(const QString& text, const QString& color);

    QLineEdit* modelIdEdit_ = nullptr;
    QLineEdit* assetIdEdit_ = nullptr;
    QLineEdit* inputTopicEdit_ = nullptr;
    QLineEdit* outputTopicEdit_ = nullptr;
    QLineEdit* runtimeSnapshotTopicEdit_ = nullptr;
    QLineEdit* predictionResultTopicEdit_ = nullptr;
    QLineEdit* assetRootEdit_ = nullptr;
    QLineEdit* algorithmEdit_ = nullptr;
    QComboBox* subjectCombo_ = nullptr;
    QSpinBox* componentSpin_ = nullptr;
    QSpinBox* gridSizeSpin_ = nullptr;
    QDoubleSpinBox* updateRateSpin_ = nullptr;
    QCheckBox* simulationCheck_ = nullptr;

    QLabel* statusBadge_ = nullptr;
    QLabel* frameIdValue_ = nullptr;
    QLabel* sourceFrameValue_ = nullptr;
    QLabel* maxValue_ = nullptr;
    QLabel* meanValue_ = nullptr;
    QLabel* freshnessValue_ = nullptr;
    QLabel* gridValue_ = nullptr;

    flightenv::ui::demo::VtkModelFieldWidget* vtkField_ = nullptr;
    QTableWidget* summaryTable_ = nullptr;
    QTableWidget* historyTable_ = nullptr;
    QTimer* demoTimer_ = nullptr;
    QTimer* freshnessTimer_ = nullptr;
    std::unique_ptr<flightenv::ui::demo::RosJsonSubscriber> ros_;
    QElapsedTimer freshnessClock_;
    QString rosError_;
    std::deque<contracts::FieldPredictionFrame> history_;
    std::optional<contracts::RuntimeSnapshotDTO> runtimeSnapshot_;
    std::optional<contracts::PredictionResultDTO> latestPredictionResult_;
    int frameCounter_ = 0;
    double simTime_s_ = 0.0;
};
