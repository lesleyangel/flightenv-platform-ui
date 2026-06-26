#pragma once

#include <EnvContracts/dto/StateFrame.hpp>
#include <EnvContracts/dto/StateEstimateFrame.hpp>
#include <EnvContracts/dto/TrajectoryPredictionFrame.hpp>

#include <QElapsedTimer>
#include <QPointF>
#include <QString>
#include <QWidget>

#include <deque>
#include <memory>

namespace flightenv::ui::demo {
class RosJsonSubscriber;
}

class QCheckBox;
class QDoubleSpinBox;
class QFormLayout;
class QGroupBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QTableWidget;
class QTimer;

struct TrajectoryDemoConfig {
    QString state_topic;
    QString trajectory_topic;
    QString model_id;
    double rate_hz = 2.0;
    double horizon_s = 24.0;
    double step_s = 1.0;
    int max_samples = 80;
};

class TrajectoryPlotWidget final : public QWidget {
public:
    explicit TrajectoryPlotWidget(QWidget* parent = nullptr);

    void setFrames(
        const contracts::StateFrame& state,
        const contracts::TrajectoryPredictionFrame& trajectory);
    void clearFrames();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    static QPointF statePoint(const contracts::StateFrame& state);
    static QPointF samplePoint(const contracts::TrajectorySampleDTO& sample);

    contracts::StateFrame state_;
    contracts::TrajectoryPredictionFrame trajectory_;
    bool has_frames_ = false;
};

class TrajectoryMonitorWidget final : public QWidget {
public:
    explicit TrajectoryMonitorWidget(QWidget* parent = nullptr);
    ~TrajectoryMonitorWidget() override;

    void ingestFrame(
        const contracts::StateFrame& state,
        const contracts::TrajectoryPredictionFrame& trajectory,
        const QString& source_label);

private:
    void buildUi();
    void connectUi();
    void startSimulation();
    void stopSimulation();
    void reconnectRos();
    void onStatePayload(const QString& topic, const QString& payload);
    void onTrajectoryPayload(const QString& topic, const QString& payload);
    void onSimulationTick();
    void updateFreshness();
    void updateReadouts(
        const contracts::StateFrame& state,
        const contracts::TrajectoryPredictionFrame& trajectory,
        const QString& source_label);
    void rebuildRecentStateTable();
    void rebuildTrajectoryHistoryTable();
    void rebuildSampleTable();
    void trimRecentStates();
    void trimRecentTrajectories();

    TrajectoryDemoConfig readConfig() const;
    contracts::StateFrame makeStateFrame(const TrajectoryDemoConfig& config);
    contracts::TrajectoryPredictionFrame makeTrajectoryFrame(
        const TrajectoryDemoConfig& config,
        const contracts::StateFrame& state) const;

    QLabel* makeValueLabel(const QString& text = QString()) const;
    void setStatusBadge(const QString& text, const QString& color);

    QLineEdit* topicEdit_ = nullptr;
    QLineEdit* trajectoryTopicEdit_ = nullptr;
    QLineEdit* modelIdEdit_ = nullptr;
    QDoubleSpinBox* rateSpin_ = nullptr;
    QDoubleSpinBox* horizonSpin_ = nullptr;
    QDoubleSpinBox* stepSpin_ = nullptr;
    QSpinBox* maxSamplesSpin_ = nullptr;
    QCheckBox* simulationCheck_ = nullptr;

    QLabel* sourceValue_ = nullptr;
    QLabel* statusBadge_ = nullptr;
    QLabel* freshnessValue_ = nullptr;
    QLabel* frameIdValue_ = nullptr;
    QLabel* sampleCountValue_ = nullptr;
    QLabel* timeValue_ = nullptr;
    QLabel* distanceValue_ = nullptr;
    QLabel* altitudeValue_ = nullptr;
    QLabel* machValue_ = nullptr;
    QLabel* dynamicPressureValue_ = nullptr;
    QLabel* alphaValue_ = nullptr;

    TrajectoryPlotWidget* plot_ = nullptr;
    QTableWidget* recentTable_ = nullptr;
    QTableWidget* trajectoryHistoryTable_ = nullptr;
    QTableWidget* sampleTable_ = nullptr;
    QTimer* simulationTimer_ = nullptr;
    QTimer* freshnessTimer_ = nullptr;

    QElapsedTimer freshnessClock_;
    std::deque<contracts::StateFrame> recentStates_;
    std::deque<contracts::TrajectoryPredictionFrame> recentTrajectories_;
    contracts::StateFrame latestState_;
    contracts::TrajectoryPredictionFrame latestTrajectory_;
    std::unique_ptr<flightenv::ui::demo::RosJsonSubscriber> ros_;
    QString rosError_;
    bool hasLatestState_ = false;
    bool hasLatestTrajectory_ = false;
    int frameCounter_ = 0;
    double simTime_s_ = 0.0;
};
