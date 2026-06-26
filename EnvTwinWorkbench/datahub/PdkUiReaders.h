#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace twin {

struct PdkReadIssue {
    QString severity;
    QString code;
    QString message;
    QString path;
};

bool hasPdkReadErrors(const QVector<PdkReadIssue>& issues);

struct PdkPortView {
    QString portId;
    QString frameContract;
    QString contractId;
    QString valueKind;
    bool required = false;
    // 结构体化(TypedDTO)：端口强类型 I/O 契约（atomic.json 的 typed_io_contract）。
    QString typedStatus;
    QString typedDtoName;
    QString typedTypeName;
    QString typedSchemaId;
    QString bufferLayoutId;
    bool zeroCopyEligible = false;
    bool jsonIoForbidden = false;
};

struct PdkOperatorDisplayView {
    QString rendererId;
    QString displayTitle;
    QString fallbackRenderer;
    QStringList primaryOutputs;
    QStringList views;
    QStringList series;
    QStringList artifactPorts;
    QStringList probePorts;
    QStringList displayRoles;
    QString meshRefRole;
    QString valueRefPort;
    QString expectedValueKind;
};

struct PdkOperatorView {
    QString path;
    QString operatorId;
    QString operatorKind;
    QString operatorFamily;
    QString executionKind;
    QString adapterId;
    QStringList phases;
    QStringList resourceRefs;
    QVector<PdkPortView> inputs;
    QVector<PdkPortView> outputs;
    PdkOperatorDisplayView display;
    QJsonObject rawJson;
};

struct PdkWorkflowNodeView {
    QString phaseId;
    QString stageId;
    QString nodeId;
    QString operatorRef;
};

struct PdkWorkflowView {
    QString path;
    QString workflowId;
    QString objectId;
    QString phase;
    QVector<PdkWorkflowNodeView> nodes;
    QJsonObject rawJson;
};

struct PdkAssetGroupView {
    QString groupId;
    QStringList resourceIds;
};

struct PdkObjectPackageView {
    QString rootPath;
    QString objectId;
    QJsonObject twinObjectJson;
    QJsonObject resourcesJson;
    QJsonObject runtimeProfileJson;
    QVector<PdkAssetGroupView> assetGroups;
    QVector<PdkOperatorView> operators;
    QVector<PdkWorkflowView> workflows;
    QVector<PdkReadIssue> issues;

    bool ok() const { return !hasPdkReadErrors(issues); }
};

struct PdkPlanNodeView {
    QString nodeId;
    QString operatorId;
    QString phaseId;
    QString stageId;
    QString executionKind;
    QString adapterId;
    QStringList dependsOn;
    PdkOperatorDisplayView display;
    QJsonObject timePolicy;
    QJsonObject schedulerPolicy;
};

struct PdkActivationNodeView {
    QString compiledNodeId;
    QString phaseId;
    QString stageId;
    QString nodeId;
    QString operatorRef;
    QString feature;
    QString status;
    QString reason;
    bool required = false;
    bool enabledByPolicy = true;
    bool enabledByProfile = true;
};

struct PdkCompiledWorkflowView {
    QString compiledDir;
    QString workflowId;
    QString objectId;
    QString phase;
    QString runId;
    QJsonObject executionPlanJson;
    QJsonObject timePlanJson;
    QJsonObject schedulerPlanJson;
    QJsonObject dataPlanePlanJson;
    QJsonObject operatorSnapshotJson;
    QJsonObject activationSnapshotJson;
    QVector<PdkPlanNodeView> nodes;
    QVector<PdkActivationNodeView> activationNodes;
    QVector<PdkReadIssue> issues;

    bool ok() const { return !hasPdkReadErrors(issues); }
};

struct PdkDataPlaneEntryView {
    QString branchId;
    QString sourceRunDir;
    QString nodeId;
    QString operatorId;
    QString direction;
    QString portId;
    QString contractId;
    QString representation;
    QString ref;
    QString artifactUri;
    QString layoutRef;
    QString checksum;
    QString evidenceRef;
    QString fieldName;
    QString componentId;
    QString meshRef;
    QString unit;
    QStringList shape;
    QJsonObject timePoint;
    QJsonObject statistics;
    int mainlineFrameIndex = -1;
    int stepIndex = -1;
    int loopIterationIndex = -1;
    qint64 inlineByteSize = 0;
    qint64 nodeCount = 0;
};

struct PdkDataPlaneView {
    QString path;
    QString runId;
    QString workflowId;
    QString objectId;
    QVector<PdkDataPlaneEntryView> entries;
    QVector<PdkReadIssue> issues;

    int artifactRefCount() const;
    int inlineJsonCount() const;
    bool ok() const { return !hasPdkReadErrors(issues); }
};

struct PdkHealthTrendView {
    QString path;
    QString runId;
    int triggerFrameIndex = -1;
    int iterationCount = 0;
    QString stopReason;
    bool damageNonDecreasing = false;
    bool ablationNonDecreasing = false;
    bool rulNonIncreasing = false;
    QJsonObject firstStep;
    QJsonObject lastStep;
    QVector<QJsonObject> trend;
    QVector<PdkReadIssue> issues;

    bool ok() const { return !hasPdkReadErrors(issues); }
};

struct PdkFieldArtifactView {
    QString path;
    QString fieldName;
    QString contractId;
    QString layoutRef;
    QString unit;
    QString componentId;
    QString meshRef;
    QStringList shape;
    qint64 nodeCount = 0;
    QVector<double> values;
    QJsonObject statistics;
    QVector<PdkReadIssue> issues;

    bool ok() const { return !hasPdkReadErrors(issues); }
};

struct PdkRuntimeEvidenceView {
    QString runDir;
    QString runId;
    QString workflowId;
    QString objectId;
    QJsonObject runtimeNodeSnapshotJson;
    QJsonObject sensorStreamJson;
    QJsonObject runtimeOutputsJson;
    PdkDataPlaneView dataPlane;
    PdkHealthTrendView healthTrend;
    int sensorFrameCount = 0;
    int sensorCountMin = 0;
    int sensorCountMax = 0;
    QVector<PdkReadIssue> issues;

    bool ok() const { return !hasPdkReadErrors(issues); }
};

class PdkObjectPackageReader {
public:
    PdkObjectPackageView read(const QString& objectPackageRoot) const;
};

class PdkCompiledWorkflowReader {
public:
    PdkCompiledWorkflowView read(const QString& compiledWorkflowDir) const;
};

class PdkDataPlaneReader {
public:
    PdkDataPlaneView read(const QString& manifestOrRunDir) const;
};

class PdkHealthTrendReader {
public:
    PdkHealthTrendView read(const QString& healthTrendPathOrRunDir) const;
};

class PdkFieldArtifactReader {
public:
    PdkFieldArtifactView read(const QString& artifactPath) const;
};

class PdkRuntimeEvidenceReader {
public:
    PdkRuntimeEvidenceView read(const QString& runDir) const;
};

} // namespace twin
