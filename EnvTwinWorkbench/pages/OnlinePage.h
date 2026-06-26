#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>
#include <QWidget>

#include "../datahub/PdkUiReaders.h"

#include "EnvContracts/dto/PredictionResultDTO.hpp"
#include "EnvContracts/dto/RuntimeSnapshotDTO.hpp"
#include "EnvContracts/dto/StateFrame.hpp"

class QLabel;
class QComboBox;
class QProgressBar;
class QPushButton;
class QSlider;
class QTableWidget;

namespace flightenv::ui::display {
class ScalarTrendWidget;
}

namespace twin {

class BranchFieldPanel;
class BranchSeriesPanel;
class BranchStatePanel;
class BranchTimelineWidget;
class BranchTreeWidget;

class OnlinePage final : public QWidget {
    Q_OBJECT
public:
    explicit OnlinePage(QString objectPackageRoot, QWidget* parent = nullptr);

public slots:
    void onRuntimeSnapshot(const contracts::RuntimeSnapshotDTO& snapshot);
    void onPrediction(const contracts::PredictionResultDTO& prediction);
    void onState(const contracts::StateFrame& state);
    void onTimeline(const QJsonObject& timeline);
    void onRunProgress(const QJsonObject& progress);
    void onRunStatus(const QString& status, const QString& message);
    void onRunLog(const QString& line);

signals:
    void prepareMainlineRequested(const QString& workflowId, const QString& profileId);
    void startMainlineRequested(const QString& workflowId, const QString& profileId);

private:
    void setProgressValue(QProgressBar* bar, double percent);
    void applyBranchSnapshot(const QJsonObject& timeline, bool followLive);
    void selectBranch(const QString& branchId);
    void selectTimelinePoint(const QString& branchId, int loopIterationIndex);
    // 时间轴 scrubber：滑块在当前分支的帧序列上拖动，驱动状态/场/曲线显示。
    void onScrubberMoved(int sliderValue);
    void syncScrubber();
    void updateScrubberLabel(int loopIterationIndex);
    void updateOnlineSummary(const QJsonObject& timeline);
    void updateFilterSummary(const QJsonObject& timeline);
    void updateRuntimeSummary(const QJsonObject& timeline);

    QPushButton* prepareRunButton_ = nullptr;
    QPushButton* startRunButton_ = nullptr;
    QLabel* runStageVal_ = nullptr;
    QLabel* runMessageVal_ = nullptr;
    QLabel* runLogVal_ = nullptr;
    QLabel* clockProgressVal_ = nullptr;
    QLabel* initializationStatusVal_ = nullptr;
    QProgressBar* totalProgressBar_ = nullptr;
    QProgressBar* onlineProgressBar_ = nullptr;
    QProgressBar* predictionProgressBar_ = nullptr;
    QTableWidget* initTable_ = nullptr;
    QComboBox* workflowCombo_ = nullptr;
    QComboBox* profileCombo_ = nullptr;
    PdkObjectPackageView objectPackage_;

    QLabel* dataSourceVal_ = nullptr;
    QLabel* latestStateVal_ = nullptr;
    QLabel* freshnessVal_ = nullptr;
    QLabel* onlineFramesVal_ = nullptr;

    QLabel* essVal_ = nullptr;
    QLabel* particleVal_ = nullptr;
    QLabel* residualVal_ = nullptr;
    QLabel* resampleVal_ = nullptr;
    QLabel* runtimeVal_ = nullptr;
    QLabel* schedulerVal_ = nullptr;
    QLabel* predictionVal_ = nullptr;
    flightenv::ui::display::ScalarTrendWidget* essTrend_ = nullptr;
    flightenv::ui::display::ScalarTrendWidget* residualTrend_ = nullptr;

    QSlider* timeScrubber_ = nullptr;
    QLabel* scrubberLabel_ = nullptr;
    QVector<int> scrubberLoops_;

    BranchTreeWidget* branchTree_ = nullptr;
    BranchTimelineWidget* branchTimeline_ = nullptr;
    BranchStatePanel* branchState_ = nullptr;
    BranchFieldPanel* branchField_ = nullptr;
    BranchSeriesPanel* branchSeries_ = nullptr;

    QJsonObject latestTimeline_;
    QString currentBranchId_;
    int currentLoopIterationIndex_ = -1;
    qint64 lastStateNs_ = 0;
    int livePredictionCount_ = 0;
};

} // namespace twin
