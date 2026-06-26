#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

#include "PdkUiReaders.h"

namespace twin {

enum class BranchKind {
    OnlineMainline,
    RealtimePrediction,
    FuturePrediction,
    Unknown
};

enum class TimelinePointKind {
    OnlineFusionFrame,
    OnlineRuntimeStep,
    RealtimePredictionFrame,
    PredictionStep,
    Unknown
};

QString branchKindText(BranchKind kind);
QString timelinePointKindText(TimelinePointKind kind);

// 一条运行分支。在线主线和每个预测分支都用同一个模型表达，
// UI 后续只按 branch_kind 决定显示为“在线融合”还是“未来预测”。
struct BranchDescriptor {
    QString branchId;
    BranchKind kind = BranchKind::Unknown;
    QString rawKind;
    QString parentBranchId;
    QString workflowId;
    QString runId;
    QString runDir;
    QString status;
    int priority = 0;
    int triggerFrameIndex = -1;
    double triggerTimeS = 0.0;
    QString seedRuntimeOutputsRef;
    QJsonObject refs;
    QJsonObject summary;
    QJsonObject rawJson;

    bool isOnline() const;
    bool isPrediction() const;
    QString displayName() const;
};

// 分支上的一个可选时刻。在线融合帧、在线 runtime step、预测 step 都会进入这里。
// 页面层通过 kind 区分“当前时刻是融合还是预测”。
struct TimelinePoint {
    QString pointId;
    TimelinePointKind kind = TimelinePointKind::Unknown;
    QString branchId;
    QString runId;
    QString sourceRunDir;
    QString sourceRuntimeOutputs;
    int frameIndex = -1;
    int mainlineFrameIndex = -1;
    int stepIndex = -1;
    int loopIterationIndex = -1;
    int tickIndex = -1;
    double sampleTimeS = 0.0;
    double runTimeS = 0.0;
    double sourceTimeS = 0.0;
    double publicTimeS = 0.0;
    double effectiveDeltaTS = 0.0;
    double outputPeriodS = 0.0;
    double altitudeM = 0.0;
    double remainingLifeS = 0.0;
    int sensorCount = 0;
    QString status;
    QString stopReason;
    bool stopped = false;
    QJsonObject timePoint;
    QJsonObject timeSummary;
    QJsonObject selectedState;
    QJsonObject filterSummary;
    QJsonObject rawJson;

    bool isFusionFrame() const;
    bool isRealtimePredictionFrame() const;
    bool isPredictionStep() const;
    QString displayName() const;
};

// 可显示的场 artifact。它只保存 artifact 引用和显示元数据，
// 不在数据模型阶段读取完整 values，避免阻塞 UI。
struct FieldArtifactOption {
    QString optionId;
    QString branchId;
    QString runId;
    QString sourceRunDir;
    int mainlineFrameIndex = -1;
    int stepIndex = -1;
    int loopIterationIndex = -1;
    QString nodeId;
    QString operatorId;
    QString portId;
    QString contractId;
    QString fieldName;
    QString fieldRole;
    QString componentId;
    QString meshRef;
    QString layoutRef;
    QString unit;
    QString representation;
    QString ref;
    QString artifactUri;
    QString artifactPath;
    QString checksum;
    QString evidenceRef;
    QStringList shape;
    qint64 nodeCount = 0;
    double publicTimeS = 0.0;
    QJsonObject timePoint;
    QJsonObject statistics;
    QJsonObject rawJson;

    bool isFullField() const;
    QString displayName() const;
};

// QoI/判据输出。剩余寿命、失效判据等先作为 QoIOption 进入 UI；
// 若后续 RUL 改为场 artifact，它也会同时出现在 FieldArtifactOption。
struct QoiOption {
    QString optionId;
    QString branchId;
    QString runId;
    QString sourceRunDir;
    int mainlineFrameIndex = -1;
    int stepIndex = -1;
    int loopIterationIndex = -1;
    QString nodeId;
    QString operatorId;
    QString portId;
    QString contractId;
    QString qoiName;
    QString representation;
    QString ref;
    QString evidenceRef;
    QString checksum;
    qint64 inlineByteSize = 0;
    double publicTimeS = 0.0;
    QJsonObject timePoint;
    QJsonObject statistics;
    QJsonObject rawJson;

    QString displayName() const;
};

struct RunPackage {
    QString mainlineRunDir;
    QString runId;
    QString objectId;
    QString workflowId;
    QString status;
    QString executionBackend;
    QString generatedAtUtc;
    QString primaryBranchId;
    QString branchRegistryPath;
    QString runTimelineIndexPath;
    QString seriesManifestPath;
    QString mainlineSummaryPath;
    QString runtimeHostEvidencePath;
    QString runtimeCursorPath;
    QJsonObject branchRegistryJson;
    QJsonObject runTimelineIndexJson;
    QJsonObject seriesManifestJson;
    QJsonObject mainlineSummaryJson;
    QJsonObject runtimeHostEvidenceJson;
    QVector<BranchDescriptor> branches;
    QVector<TimelinePoint> timelinePoints;
    QVector<FieldArtifactOption> fieldOptions;
    QVector<QoiOption> qoiOptions;
    QVector<PdkReadIssue> issues;

    bool ok() const;
    int onlineFrameCount() const;
    int predictionBranchCount() const;
    const BranchDescriptor* branchById(const QString& branchId) const;
    QVector<BranchDescriptor> predictionBranches() const;
    QVector<TimelinePoint> pointsForBranch(const QString& branchId) const;
    QVector<FieldArtifactOption> fieldsForPoint(const QString& branchId, int loopIterationIndex) const;
    QVector<QoiOption> qoisForPoint(const QString& branchId, int loopIterationIndex) const;
};

class BranchRunExplorerReader {
public:
    RunPackage read(const QString& mainlineRunDir) const;
};

} // namespace twin
