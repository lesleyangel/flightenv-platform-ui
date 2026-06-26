#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>

#include "EnvContracts/dto/PredictionResultDTO.hpp"
#include "EnvContracts/dto/RuntimeSnapshotDTO.hpp"
#include "EnvContracts/dto/StateFrame.hpp"

class QTimer;

namespace twin {

// 数据中枢：周期轮询平台 evidence，并向页面输出统一的 branch/timeline snapshot。
// LiveDataHub 负责识别 live/replay evidence、解析分支、时间线、场 artifact 和 QoI；
// 页面只消费 snapshot，不再自行拼 data_plane_manifest/runtime_snapshot/run_dir 路径。
// UI 不直接挂 ROS2/controller 节点；ROS2 只是 Runtime Host 可选 adapter backend。
class LiveDataHub : public QObject {
    Q_OBJECT
public:
    explicit LiveDataHub(QString evidenceRoot, QObject* parent = nullptr);

    // 启动 evidence 轮询；构造后由窗口显式调用。
    void start();

    // 兼容旧页面槽位；平台原生路径下 runtime snapshot 来自 evidence。
    bool latestRuntimeSnapshot(contracts::RuntimeSnapshotDTO& out) const;

    QString evidenceRoot() const { return evidenceRoot_; }
    void setEvidenceRoot(const QString& evidenceRoot);

signals:
    void predictionReceived(const contracts::PredictionResultDTO& prediction);
    void stateReceived(const contracts::StateFrame& state);
    void runtimeSnapshotReceived(const contracts::RuntimeSnapshotDTO& snapshot);
    void logReceived(const QString& line);
    // 平台 UI branch/timeline snapshot。
    // 兼容旧字段 online_frames/prediction_runs，同时包含 branches/timeline_points/
    // field_artifact_options/qoi_options，view_mode 标记 live 或 replay。
    void timelineUpdated(const QJsonObject& timeline);

private:
    void pollTimeline();
    bool pollRunTimelineIndex();
    bool pollWorkflowTimeline();
    bool pollPlatformMainline();
    bool pollPlatformMainlineProgress();
    bool pollLatestPlatformRunDir();
    bool pollPlatformRunDir(const QString& runDir);

    QString evidenceRoot_;
    QString timelinePath_;
    QString branchRegistryPath_;
    QString runTimelineIndexPath_;
    QString mainlineSummaryPath_;
    QString mainlineProgressPath_;
    QString runtimeCursorPath_;
    QTimer* pollTimer_ = nullptr;
    qint64 lastTimelineMtime_ = -1;
    qint64 lastRunTimelineIndexMtime_ = -1;
    qint64 lastMainlineMtime_ = -1;
    qint64 lastMainlineProgressMtime_ = -1;
    qint64 lastRunDirMtime_ = -1;
    QString lastRunDir_;
};

} // namespace twin
