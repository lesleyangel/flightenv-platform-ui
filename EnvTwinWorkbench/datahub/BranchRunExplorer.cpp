#include "BranchRunExplorer.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

namespace twin {
namespace {

// 共享 delete 读取：C++ Host 每帧 MoveFileExW 原子替换 run_timeline_index.json 等文件，
// UI 用普通句柄会阻塞替换并使 Host 崩溃。共享 delete 后 Host 替换可成功。
QByteArray readRunFileBytes(const QString& path) {
#ifdef _WIN32
    HANDLE handle = CreateFileW(
        reinterpret_cast<const wchar_t*>(path.utf16()),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return {};
    }
    QByteArray out;
    char buffer[65536];
    DWORD readBytes = 0;
    while (ReadFile(handle, buffer, sizeof(buffer), &readBytes, nullptr) && readBytes > 0) {
        out.append(buffer, static_cast<int>(readBytes));
    }
    CloseHandle(handle);
    return out;
#else
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
#endif
}

void addIssue(QVector<PdkReadIssue>& issues,
              QString severity,
              QString code,
              QString message,
              QString path = {}) {
    issues.push_back(PdkReadIssue{
        std::move(severity),
        std::move(code),
        std::move(message),
        QDir::toNativeSeparators(std::move(path))});
}

QJsonObject readJsonObject(const QString& path,
                           QVector<PdkReadIssue>& issues,
                           bool required,
                           const QString& missingCode) {
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        addIssue(issues,
                 required ? QStringLiteral("error") : QStringLiteral("warning"),
                 missingCode,
                 QStringLiteral("JSON 文件不存在"),
                 path);
        return {};
    }

    QByteArray payload = readRunFileBytes(path);
    if (payload.isEmpty()) {
        addIssue(issues,
                 required ? QStringLiteral("error") : QStringLiteral("warning"),
                 QStringLiteral("file_open_failed"),
                 QStringLiteral("无法读取（文件可能正被 Host 替换或为空）"),
                 path);
        return {};
    }

    QJsonParseError error{};
    QJsonDocument doc = QJsonDocument::fromJson(payload, &error);
    if (error.error != QJsonParseError::NoError) {
        // Runtime evidence 里历史数据可能出现非标准数值。展示层按 null 容错。
        payload.replace("-Infinity", "null");
        payload.replace("Infinity", "null");
        payload.replace("NaN", "null");
        error = {};
        doc = QJsonDocument::fromJson(payload, &error);
    }
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        addIssue(issues,
                 required ? QStringLiteral("error") : QStringLiteral("warning"),
                 QStringLiteral("json_parse_failed"),
                 error.errorString(),
                 path);
        return {};
    }
    return doc.object();
}

QString normalizePath(QString path) {
    path = path.trimmed();
    if (path.startsWith(QStringLiteral("//?/")) || path.startsWith(QStringLiteral("\\\\?\\"))) {
        path = path.mid(4);
    }
    return QDir::toNativeSeparators(QDir::fromNativeSeparators(path));
}

QString fileInDir(const QString& dir, const QString& fileName) {
    return QDir(dir).filePath(fileName);
}

QString stringValue(const QJsonObject& obj, const QString& key, const QString& fallback = {}) {
    const QJsonValue value = obj.value(key);
    if (value.isString()) {
        return value.toString();
    }
    return fallback;
}

int intValue(const QJsonObject& obj, const QString& key, int fallback = -1) {
    const QJsonValue value = obj.value(key);
    if (value.isDouble()) {
        return value.toInt(fallback);
    }
    if (value.isString()) {
        bool ok = false;
        const int parsed = value.toString().toInt(&ok);
        return ok ? parsed : fallback;
    }
    return fallback;
}

qint64 int64Value(const QJsonObject& obj, const QString& key, qint64 fallback = 0) {
    const QJsonValue value = obj.value(key);
    if (value.isDouble()) {
        return static_cast<qint64>(value.toDouble(fallback));
    }
    if (value.isString()) {
        bool ok = false;
        const qint64 parsed = value.toString().toLongLong(&ok);
        return ok ? parsed : fallback;
    }
    return fallback;
}

double doubleValue(const QJsonObject& obj, const QString& key, double fallback = 0.0) {
    const QJsonValue value = obj.value(key);
    if (value.isDouble()) {
        return value.toDouble(fallback);
    }
    if (value.isString()) {
        bool ok = false;
        const double parsed = value.toString().toDouble(&ok);
        return ok ? parsed : fallback;
    }
    return fallback;
}

double nanValue() {
    return std::numeric_limits<double>::quiet_NaN();
}

double timePointRunTime(const QJsonObject& point, double fallback = nanValue()) {
    return doubleValue(point, QStringLiteral("run_time_s"), fallback);
}

double publicTimeValue(const QJsonObject& obj,
                       const QJsonObject& point,
                       double fallback = nanValue()) {
    const double publicTime = doubleValue(obj, QStringLiteral("public_time_s"), nanValue());
    if (std::isfinite(publicTime)) {
        return publicTime;
    }
    const double publicOutputTime = doubleValue(obj, QStringLiteral("public_output_time_s"), nanValue());
    if (std::isfinite(publicOutputTime)) {
        return publicOutputTime;
    }
    const double pointTime = timePointRunTime(point, nanValue());
    if (std::isfinite(pointTime)) {
        return pointTime;
    }
    const double runTime = doubleValue(obj, QStringLiteral("run_time_s"), nanValue());
    if (std::isfinite(runTime)) {
        return runTime;
    }
    return doubleValue(obj, QStringLiteral("sample_time_s"), fallback);
}

bool samePublicTime(double left, double right) {
    return std::isfinite(left) && std::isfinite(right) && std::abs(left - right) <= 1.0e-6;
}

QJsonObject timeSummaryFromItem(const QJsonObject& item) {
    QJsonObject out = item.value(QStringLiteral("time_summary")).toObject();
    const auto copyIfPresent = [&out, &item](const QString& key) {
        if (item.contains(key) && !out.contains(key)) {
            out.insert(key, item.value(key));
        }
    };
    copyIfPresent(QStringLiteral("base_dt_s"));
    copyIfPresent(QStringLiteral("output_period_s"));
    copyIfPresent(QStringLiteral("effective_delta_t_s"));
    copyIfPresent(QStringLiteral("public_output_time_s"));
    copyIfPresent(QStringLiteral("effective_delta_t_s_by_node"));
    copyIfPresent(QStringLiteral("held_output_count"));
    copyIfPresent(QStringLiteral("held_outputs"));
    return out;
}

QStringList stringListValue(const QJsonValue& value) {
    QStringList out;
    const QJsonArray array = value.toArray();
    out.reserve(array.size());
    for (const QJsonValue& item : array) {
        if (item.isString()) {
            out.push_back(item.toString());
        } else if (item.isDouble()) {
            out.push_back(QString::number(item.toDouble(), 'g', 16));
        }
    }
    return out;
}

BranchKind parseBranchKind(const QString& raw) {
    if (raw == QStringLiteral("online_mainline") || raw == QStringLiteral("online")) {
        return BranchKind::OnlineMainline;
    }
    if (raw == QStringLiteral("realtime_prediction") || raw == QStringLiteral("online_prediction")) {
        return BranchKind::RealtimePrediction;
    }
    if (raw == QStringLiteral("future_prediction") || raw == QStringLiteral("prediction")) {
        return BranchKind::FuturePrediction;
    }
    return BranchKind::Unknown;
}

QString inferFieldName(const QJsonObject& entry) {
    const QString explicitName = stringValue(entry, QStringLiteral("field_name"));
    if (!explicitName.isEmpty()) {
        return explicitName;
    }
    const QString contract = stringValue(entry, QStringLiteral("contract_id"));
    if (!contract.isEmpty()) {
        return contract;
    }
    return stringValue(entry, QStringLiteral("port_id"));
}

QString inferFieldRole(const QJsonObject& entry, const QString& fieldName) {
    Q_UNUSED(fieldName);
    const QString explicitRole = stringValue(entry, QStringLiteral("field_role"));
    if (!explicitRole.isEmpty()) {
        return explicitRole;
    }
    const QString displayRole = stringValue(entry, QStringLiteral("display_role"));
    if (!displayRole.isEmpty()) {
        return displayRole;
    }
    return QStringLiteral("field_artifact");
}

QString inferQoiName(const QJsonObject& entry) {
    const QString port = stringValue(entry, QStringLiteral("port_id"));
    if (!port.isEmpty()) {
        const int dot = port.lastIndexOf(QLatin1Char('.'));
        return dot >= 0 ? port.mid(dot + 1) : port;
    }
    const QString node = stringValue(entry, QStringLiteral("node_id"));
    if (!node.isEmpty()) {
        const int dot = node.lastIndexOf(QLatin1Char('.'));
        return dot >= 0 ? node.mid(dot + 1) : node;
    }
    return stringValue(entry, QStringLiteral("contract_id"));
}

QString branchDisplayName(const QString& branchId, BranchKind kind, int triggerFrame) {
    if (kind == BranchKind::OnlineMainline) {
        return QStringLiteral("在线主时间轴");
    }
    if (kind == BranchKind::RealtimePrediction) {
        // 不是分叉：是主时间轴每帧后验的场重建一路。
        return QStringLiteral("└ 在线后验场轨（非分支）");
    }
    if (kind == BranchKind::FuturePrediction) {
        return triggerFrame >= 0
                   ? QStringLiteral("未来预测分支 · 帧 %1").arg(triggerFrame)
                   : QStringLiteral("未来预测分支 · %1").arg(branchId);
    }
    return branchId;
}

QString pointId(const QString& branchId,
                TimelinePointKind kind,
                int frameIndex,
                int loopIterationIndex) {
    const QString role = kind == TimelinePointKind::OnlineFusionFrame
                             ? QStringLiteral("fusion")
                             : (kind == TimelinePointKind::RealtimePredictionFrame ? QStringLiteral("realtime")
                             : (kind == TimelinePointKind::PredictionStep ? QStringLiteral("prediction")
                                                                          : QStringLiteral("runtime")));
    const int index = frameIndex >= 0 ? frameIndex : loopIterationIndex;
    return QStringLiteral("%1/%2/%3").arg(branchId, role).arg(index, 6, 10, QLatin1Char('0'));
}

QHash<QString, BranchDescriptor> branchMap(const QVector<BranchDescriptor>& branches) {
    QHash<QString, BranchDescriptor> out;
    for (const BranchDescriptor& branch : branches) {
        out.insert(branch.branchId, branch);
    }
    return out;
}

QString runIdForBranch(const QHash<QString, BranchDescriptor>& branches, const QString& branchId) {
    const auto it = branches.find(branchId);
    return it == branches.end() ? QString() : it->runId;
}

TimelinePoint readOnlineFramePoint(const QJsonObject& item) {
    const QJsonObject frame = item.value(QStringLiteral("frame")).toObject();
    TimelinePoint out;
    out.kind = TimelinePointKind::OnlineFusionFrame;
    out.branchId = stringValue(item, QStringLiteral("branch_id"), QStringLiteral("main.online"));
    out.frameIndex = intValue(item, QStringLiteral("frame_index"), intValue(frame, QStringLiteral("frame_index")));
    out.mainlineFrameIndex = intValue(item, QStringLiteral("mainline_frame_index"), out.frameIndex);
    out.loopIterationIndex = intValue(item, QStringLiteral("loop_iteration_index"), intValue(frame, QStringLiteral("loop_iteration_index"), out.frameIndex));
    out.stepIndex = intValue(item, QStringLiteral("step_index"), out.loopIterationIndex);
    out.sampleTimeS = doubleValue(item, QStringLiteral("sample_time_s"), doubleValue(frame, QStringLiteral("sample_time_s")));
    out.runTimeS = doubleValue(frame, QStringLiteral("runtime_time_s"), doubleValue(item, QStringLiteral("runtime_time_s")));
    out.sourceTimeS = out.sampleTimeS;
    out.publicTimeS = publicTimeValue(item, QJsonObject(), out.sampleTimeS);
    out.effectiveDeltaTS = doubleValue(item, QStringLiteral("effective_delta_t_s"), nanValue());
    out.outputPeriodS = doubleValue(item, QStringLiteral("output_period_s"), nanValue());
    out.sensorCount = intValue(item, QStringLiteral("sensor_count"), intValue(frame, QStringLiteral("sensor_count"), 0));
    out.status = stringValue(frame, QStringLiteral("status"), stringValue(item, QStringLiteral("status")));
    out.sourceRunDir = normalizePath(stringValue(item, QStringLiteral("source_run_dir")));
    out.sourceRuntimeOutputs = normalizePath(stringValue(item, QStringLiteral("source_runtime_outputs")));
    out.selectedState = item.value(QStringLiteral("selected_state")).toObject();
    if (out.selectedState.isEmpty()) {
        out.selectedState = frame.value(QStringLiteral("selected_state")).toObject();
    }
    out.filterSummary = item.value(QStringLiteral("filter")).toObject();
    out.altitudeM = doubleValue(out.selectedState, QStringLiteral("h"));
    out.rawJson = item;
    out.pointId = pointId(out.branchId, out.kind, out.frameIndex, out.loopIterationIndex);
    return out;
}

TimelinePoint readBranchStepPoint(const QJsonObject& item,
                                  const QHash<QString, BranchDescriptor>& branches) {
    const QString branchId = stringValue(item, QStringLiteral("branch_id"));
    const auto branch = branches.find(branchId);
    const BranchKind branchKind = branch == branches.end() ? BranchKind::Unknown : branch->kind;
    TimelinePoint out;
    if (branchKind == BranchKind::FuturePrediction) {
        out.kind = TimelinePointKind::PredictionStep;
    } else if (branchKind == BranchKind::RealtimePrediction) {
        out.kind = TimelinePointKind::RealtimePredictionFrame;
    } else {
        out.kind = TimelinePointKind::OnlineRuntimeStep;
    }
    out.branchId = branchId;
    out.runId = runIdForBranch(branches, branchId);
    out.sourceRunDir = normalizePath(stringValue(item, QStringLiteral("source_run_dir")));
    out.sourceRuntimeOutputs = normalizePath(stringValue(item, QStringLiteral("source_runtime_outputs")));
    out.frameIndex = intValue(item, QStringLiteral("frame_index"));
    out.mainlineFrameIndex = intValue(item, QStringLiteral("mainline_frame_index"), out.frameIndex);
    out.loopIterationIndex = intValue(item, QStringLiteral("loop_iteration_index"), intValue(item, QStringLiteral("step_index")));
    out.stepIndex = intValue(item, QStringLiteral("step_index"), out.loopIterationIndex);
    out.timePoint = item.value(QStringLiteral("time_point")).toObject();
    out.runTimeS = doubleValue(item, QStringLiteral("run_time_s"),
                               doubleValue(out.timePoint, QStringLiteral("run_time_s")));
    out.sourceTimeS = doubleValue(item, QStringLiteral("source_time_s"),
                                  doubleValue(out.timePoint, QStringLiteral("source_time_s")));
    out.sampleTimeS = doubleValue(item, QStringLiteral("sample_time_s"),
                                  doubleValue(item, QStringLiteral("state_time_s"), out.sourceTimeS));
    out.publicTimeS = publicTimeValue(item, out.timePoint, out.runTimeS);
    out.effectiveDeltaTS = doubleValue(item, QStringLiteral("effective_delta_t_s"), nanValue());
    out.outputPeriodS = doubleValue(item, QStringLiteral("output_period_s"), nanValue());
    out.timeSummary = timeSummaryFromItem(item);
    out.selectedState = item.value(QStringLiteral("state_label")).toObject();
    if (out.selectedState.isEmpty()) {
        out.selectedState = item.value(QStringLiteral("selected_state")).toObject();
    }
    out.altitudeM = doubleValue(item, QStringLiteral("altitude_m"),
                                doubleValue(out.selectedState, QStringLiteral("altitude_m"),
                                            doubleValue(out.selectedState, QStringLiteral("h"))));
    out.remainingLifeS = doubleValue(item, QStringLiteral("remaining_life_s"));
    out.stopped = item.value(QStringLiteral("stop")).toBool(false);
    out.stopReason = stringValue(item, QStringLiteral("stop_reason"));
    out.status = intValue(item, QStringLiteral("failed_nodes"), 0) == 0 ? QStringLiteral("ok") : QStringLiteral("failed");
    out.rawJson = item;
    out.pointId = pointId(out.branchId, out.kind, out.frameIndex, out.loopIterationIndex);
    return out;
}

FieldArtifactOption readFieldOption(const QJsonObject& entry,
                                    const QHash<QString, BranchDescriptor>& branches) {
    FieldArtifactOption out;
    out.branchId = stringValue(entry, QStringLiteral("branch_id"));
    out.runId = runIdForBranch(branches, out.branchId);
    out.sourceRunDir = normalizePath(stringValue(entry, QStringLiteral("source_run_dir")));
    out.mainlineFrameIndex = intValue(entry, QStringLiteral("mainline_frame_index"));
    out.stepIndex = intValue(entry, QStringLiteral("step_index"));
    out.loopIterationIndex = intValue(entry, QStringLiteral("loop_iteration_index"), out.stepIndex);
    out.nodeId = stringValue(entry, QStringLiteral("node_id"));
    out.operatorId = stringValue(entry, QStringLiteral("operator_id"));
    out.portId = stringValue(entry, QStringLiteral("port_id"));
    out.contractId = stringValue(entry, QStringLiteral("contract_id"));
    out.fieldName = inferFieldName(entry);
    out.fieldRole = inferFieldRole(entry, out.fieldName);
    out.componentId = stringValue(entry, QStringLiteral("component_id"));
    out.meshRef = stringValue(entry, QStringLiteral("mesh_ref"));
    out.layoutRef = stringValue(entry, QStringLiteral("layout_ref"));
    out.unit = stringValue(entry, QStringLiteral("unit"));
    out.representation = stringValue(entry, QStringLiteral("representation"));
    out.ref = stringValue(entry, QStringLiteral("ref"));
    out.artifactUri = stringValue(entry, QStringLiteral("artifact_uri"));
    out.artifactPath = normalizePath(out.artifactUri.isEmpty() ? out.ref : out.artifactUri);
    out.checksum = stringValue(entry, QStringLiteral("checksum"));
    out.evidenceRef = stringValue(entry, QStringLiteral("evidence_ref"));
    out.shape = stringListValue(entry.value(QStringLiteral("shape")));
    out.nodeCount = int64Value(entry, QStringLiteral("node_count"));
    out.timePoint = entry.value(QStringLiteral("time_point")).toObject();
    out.publicTimeS = publicTimeValue(entry, out.timePoint, nanValue());
    out.statistics = entry.value(QStringLiteral("statistics")).toObject();
    out.rawJson = entry;
    out.optionId = QStringLiteral("%1/field/%2/%3/%4")
                       .arg(out.branchId)
                       .arg(out.loopIterationIndex, 6, 10, QLatin1Char('0'))
                       .arg(out.fieldName, out.portId);
    return out;
}

QoiOption readQoiOption(const QJsonObject& entry,
                        const QHash<QString, BranchDescriptor>& branches) {
    QoiOption out;
    out.branchId = stringValue(entry, QStringLiteral("branch_id"));
    out.runId = runIdForBranch(branches, out.branchId);
    out.sourceRunDir = normalizePath(stringValue(entry, QStringLiteral("source_run_dir")));
    out.mainlineFrameIndex = intValue(entry, QStringLiteral("mainline_frame_index"));
    out.stepIndex = intValue(entry, QStringLiteral("step_index"));
    out.loopIterationIndex = intValue(entry, QStringLiteral("loop_iteration_index"), out.stepIndex);
    out.nodeId = stringValue(entry, QStringLiteral("node_id"));
    out.operatorId = stringValue(entry, QStringLiteral("operator_id"));
    out.portId = stringValue(entry, QStringLiteral("port_id"));
    out.contractId = stringValue(entry, QStringLiteral("contract_id"));
    out.qoiName = inferQoiName(entry);
    out.representation = stringValue(entry, QStringLiteral("representation"));
    out.ref = stringValue(entry, QStringLiteral("ref"));
    out.evidenceRef = stringValue(entry, QStringLiteral("evidence_ref"));
    out.checksum = stringValue(entry, QStringLiteral("checksum"));
    out.inlineByteSize = int64Value(entry, QStringLiteral("inline_byte_size"));
    out.timePoint = entry.value(QStringLiteral("time_point")).toObject();
    out.publicTimeS = publicTimeValue(entry, out.timePoint, nanValue());
    out.statistics = entry.value(QStringLiteral("statistics")).toObject();
    out.rawJson = entry;
    out.optionId = QStringLiteral("%1/qoi/%2/%3/%4")
                       .arg(out.branchId)
                       .arg(out.loopIterationIndex, 6, 10, QLatin1Char('0'))
                       .arg(out.qoiName, out.portId);
    return out;
}

} // namespace

QString branchKindText(BranchKind kind) {
    switch (kind) {
    case BranchKind::OnlineMainline:
        return QStringLiteral("在线主线·融合+预测");
    case BranchKind::RealtimePrediction:
        return QStringLiteral("主线后验场·非分叉");
    case BranchKind::FuturePrediction:
        return QStringLiteral("未来预测·真分支");
    case BranchKind::Unknown:
    default:
        return QStringLiteral("未知");
    }
}

QString timelinePointKindText(TimelinePointKind kind) {
    switch (kind) {
    case TimelinePointKind::OnlineFusionFrame:
        return QStringLiteral("在线融合帧");
    case TimelinePointKind::OnlineRuntimeStep:
        return QStringLiteral("在线算子步");
    case TimelinePointKind::RealtimePredictionFrame:
        return QStringLiteral("在线后验场帧");
    case TimelinePointKind::PredictionStep:
        return QStringLiteral("预测步");
    case TimelinePointKind::Unknown:
    default:
        return QStringLiteral("未知时刻");
    }
}

bool BranchDescriptor::isOnline() const {
    return kind == BranchKind::OnlineMainline;
}

bool BranchDescriptor::isPrediction() const {
    return kind == BranchKind::FuturePrediction;
}

QString BranchDescriptor::displayName() const {
    return branchDisplayName(branchId, kind, triggerFrameIndex);
}

bool TimelinePoint::isFusionFrame() const {
    return kind == TimelinePointKind::OnlineFusionFrame;
}

bool TimelinePoint::isRealtimePredictionFrame() const {
    return kind == TimelinePointKind::RealtimePredictionFrame;
}

bool TimelinePoint::isPredictionStep() const {
    return kind == TimelinePointKind::PredictionStep;
}

QString TimelinePoint::displayName() const {
    if (kind == TimelinePointKind::OnlineFusionFrame) {
        return QStringLiteral("融合帧 %1 · t=%2s").arg(frameIndex).arg(sampleTimeS, 0, 'f', 2);
    }
    if (kind == TimelinePointKind::RealtimePredictionFrame) {
        return QStringLiteral("在线后验场帧 %1").arg(loopIterationIndex);
    }
    if (kind == TimelinePointKind::PredictionStep) {
        return QStringLiteral("预测步 %1 · 源帧 %2").arg(loopIterationIndex).arg(mainlineFrameIndex);
    }
    return QStringLiteral("算子步 %1").arg(loopIterationIndex);
}

bool FieldArtifactOption::isFullField() const {
    return representation == QStringLiteral("artifact_ref") && nodeCount > 0 && !artifactPath.isEmpty();
}

QString FieldArtifactOption::displayName() const {
    const QString base = fieldName.isEmpty() ? portId : fieldName;
    const QString component = componentId.isEmpty() ? QString() : QStringLiteral(" / %1").arg(componentId);
    return QStringLiteral("%1%2 · step %3").arg(base, component).arg(loopIterationIndex);
}

QString QoiOption::displayName() const {
    return QStringLiteral("%1 · step %2").arg(qoiName.isEmpty() ? portId : qoiName).arg(loopIterationIndex);
}

bool RunPackage::ok() const {
    return !hasPdkReadErrors(issues);
}

int RunPackage::onlineFrameCount() const {
    return std::count_if(timelinePoints.begin(), timelinePoints.end(), [](const TimelinePoint& point) {
        return point.kind == TimelinePointKind::OnlineFusionFrame;
    });
}

int RunPackage::predictionBranchCount() const {
    return std::count_if(branches.begin(), branches.end(), [](const BranchDescriptor& branch) {
        return branch.kind == BranchKind::FuturePrediction;
    });
}

const BranchDescriptor* RunPackage::branchById(const QString& branchId) const {
    const auto it = std::find_if(branches.begin(), branches.end(), [&branchId](const BranchDescriptor& branch) {
        return branch.branchId == branchId;
    });
    return it == branches.end() ? nullptr : &(*it);
}

QVector<BranchDescriptor> RunPackage::predictionBranches() const {
    QVector<BranchDescriptor> out;
    for (const BranchDescriptor& branch : branches) {
        if (branch.kind == BranchKind::FuturePrediction) {
            out.push_back(branch);
        }
    }
    return out;
}

QVector<TimelinePoint> RunPackage::pointsForBranch(const QString& branchId) const {
    QVector<TimelinePoint> out;
    for (const TimelinePoint& point : timelinePoints) {
        if (point.branchId == branchId) {
            out.push_back(point);
        }
    }
    return out;
}

QVector<FieldArtifactOption> RunPackage::fieldsForPoint(const QString& branchId, int loopIterationIndex) const {
    double targetPublicTime = nanValue();
    for (const TimelinePoint& point : timelinePoints) {
        if (point.branchId == branchId && point.loopIterationIndex == loopIterationIndex) {
            targetPublicTime = point.publicTimeS;
            break;
        }
    }
    QVector<FieldArtifactOption> timeMatched;
    QVector<FieldArtifactOption> loopMatched;
    for (const FieldArtifactOption& option : fieldOptions) {
        if (option.branchId != branchId) {
            continue;
        }
        if (samePublicTime(option.publicTimeS, targetPublicTime)) {
            timeMatched.push_back(option);
        }
        if (option.loopIterationIndex == loopIterationIndex) {
            loopMatched.push_back(option);
        }
    }
    return timeMatched.isEmpty() ? loopMatched : timeMatched;
}

QVector<QoiOption> RunPackage::qoisForPoint(const QString& branchId, int loopIterationIndex) const {
    double targetPublicTime = nanValue();
    for (const TimelinePoint& point : timelinePoints) {
        if (point.branchId == branchId && point.loopIterationIndex == loopIterationIndex) {
            targetPublicTime = point.publicTimeS;
            break;
        }
    }
    QVector<QoiOption> timeMatched;
    QVector<QoiOption> loopMatched;
    for (const QoiOption& option : qoiOptions) {
        if (option.branchId != branchId) {
            continue;
        }
        if (samePublicTime(option.publicTimeS, targetPublicTime)) {
            timeMatched.push_back(option);
        }
        if (option.loopIterationIndex == loopIterationIndex) {
            loopMatched.push_back(option);
        }
    }
    return timeMatched.isEmpty() ? loopMatched : timeMatched;
}

RunPackage BranchRunExplorerReader::read(const QString& mainlineRunDir) const {
    RunPackage package;
    const QFileInfo mainlineInfo(QDir::fromNativeSeparators(mainlineRunDir));
    package.mainlineRunDir = QDir::toNativeSeparators(mainlineInfo.absoluteFilePath());

    if (!mainlineInfo.exists() || !mainlineInfo.isDir()) {
        addIssue(package.issues,
                 QStringLiteral("error"),
                 QStringLiteral("mainline_run_missing"),
                 QStringLiteral("主线 run 目录不存在"),
                 mainlineRunDir);
        return package;
    }

    package.branchRegistryPath = fileInDir(package.mainlineRunDir, QStringLiteral("branch_registry.json"));
    package.runTimelineIndexPath = fileInDir(package.mainlineRunDir, QStringLiteral("run_timeline_index.json"));
    package.seriesManifestPath = fileInDir(package.mainlineRunDir, QStringLiteral("series_manifest.json"));
    package.mainlineSummaryPath = fileInDir(package.mainlineRunDir, QStringLiteral("mainline_summary.json"));
    package.runtimeHostEvidencePath = fileInDir(package.mainlineRunDir, QStringLiteral("runtime_host_evidence.json"));
    package.runtimeCursorPath = fileInDir(package.mainlineRunDir, QStringLiteral("runtime_cursor.json"));

    package.branchRegistryJson = readJsonObject(
        package.branchRegistryPath, package.issues, true, QStringLiteral("branch_registry_missing"));
    package.runTimelineIndexJson = readJsonObject(
        package.runTimelineIndexPath, package.issues, true, QStringLiteral("run_timeline_index_missing"));
    package.seriesManifestJson = readJsonObject(
        package.seriesManifestPath, package.issues, false, QStringLiteral("series_manifest_missing"));
    package.mainlineSummaryJson = readJsonObject(
        package.mainlineSummaryPath, package.issues, false, QStringLiteral("mainline_summary_missing"));
    package.runtimeHostEvidenceJson = readJsonObject(
        package.runtimeHostEvidencePath, package.issues, false, QStringLiteral("runtime_host_evidence_missing"));

    package.runId = stringValue(package.branchRegistryJson, QStringLiteral("run_id"),
                                stringValue(package.runTimelineIndexJson, QStringLiteral("run_id"), mainlineInfo.fileName()));
    package.objectId = stringValue(package.branchRegistryJson, QStringLiteral("object_id"),
                                   stringValue(package.runTimelineIndexJson, QStringLiteral("object_id")));
    package.workflowId = stringValue(package.branchRegistryJson, QStringLiteral("workflow_id"),
                                     stringValue(package.runTimelineIndexJson, QStringLiteral("workflow_id")));
    package.generatedAtUtc = stringValue(package.runTimelineIndexJson, QStringLiteral("generated_at_utc"),
                                         stringValue(package.branchRegistryJson, QStringLiteral("generated_at_utc")));
    package.primaryBranchId = stringValue(package.branchRegistryJson, QStringLiteral("primary_branch_id"),
                                          QStringLiteral("main.online"));
    package.status = stringValue(package.runtimeHostEvidenceJson, QStringLiteral("status"),
                                 stringValue(package.mainlineSummaryJson, QStringLiteral("status")));
    package.executionBackend = package.runtimeHostEvidenceJson.value(QStringLiteral("host"))
                                   .toObject()
                                   .value(QStringLiteral("execution_backend"))
                                   .toString();

    const QJsonArray branchArray = package.branchRegistryJson.value(QStringLiteral("branches")).toArray();
    package.branches.reserve(branchArray.size());
    for (const QJsonValue& value : branchArray) {
        const QJsonObject obj = value.toObject();
        BranchDescriptor branch;
        branch.branchId = stringValue(obj, QStringLiteral("branch_id"));
        branch.rawKind = stringValue(obj, QStringLiteral("branch_kind"));
        branch.kind = parseBranchKind(branch.rawKind);
        branch.parentBranchId = stringValue(obj, QStringLiteral("parent_branch_id"));
        branch.workflowId = stringValue(obj, QStringLiteral("workflow_id"));
        branch.runId = stringValue(obj, QStringLiteral("run_id"));
        branch.runDir = normalizePath(stringValue(obj, QStringLiteral("run_dir")));
        branch.status = stringValue(obj, QStringLiteral("status"));
        branch.priority = intValue(obj, QStringLiteral("priority"), 0);
        branch.triggerFrameIndex = intValue(obj, QStringLiteral("trigger_frame_index"));
        branch.triggerTimeS = doubleValue(obj, QStringLiteral("trigger_time_s"));
        branch.seedRuntimeOutputsRef = normalizePath(stringValue(obj, QStringLiteral("seed_runtime_outputs_ref")));
        branch.refs = obj.value(QStringLiteral("refs")).toObject();
        branch.summary = obj.value(QStringLiteral("summary")).toObject();
        branch.rawJson = obj;
        if (!branch.branchId.isEmpty()) {
            package.branches.push_back(std::move(branch));
        }
    }

    if (package.branches.isEmpty()) {
        addIssue(package.issues,
                 QStringLiteral("warning"),
                 QStringLiteral("branch_registry_empty"),
                 QStringLiteral("branch_registry.json 中没有可用分支"),
                 package.branchRegistryPath);
    }

    const QHash<QString, BranchDescriptor> branchesById = branchMap(package.branches);

    const QJsonArray onlineFrames = package.runTimelineIndexJson.value(QStringLiteral("online_frames")).toArray();
    package.timelinePoints.reserve(onlineFrames.size() +
                                   package.runTimelineIndexJson.value(QStringLiteral("branch_steps")).toArray().size());
    for (const QJsonValue& value : onlineFrames) {
        TimelinePoint point = readOnlineFramePoint(value.toObject());
        point.runId = runIdForBranch(branchesById, point.branchId);
        package.timelinePoints.push_back(std::move(point));
    }

    const QJsonArray branchSteps = package.runTimelineIndexJson.value(QStringLiteral("branch_steps")).toArray();
    for (const QJsonValue& value : branchSteps) {
        package.timelinePoints.push_back(readBranchStepPoint(value.toObject(), branchesById));
    }

    const QJsonArray artifacts = package.runTimelineIndexJson.value(QStringLiteral("artifact_refs")).toArray();
    package.fieldOptions.reserve(artifacts.size());
    for (const QJsonValue& value : artifacts) {
        const QJsonObject entry = value.toObject();
        const FieldArtifactOption option = readFieldOption(entry, branchesById);
        if (option.isFullField()) {
            package.fieldOptions.push_back(option);
        }
    }

    const QJsonArray qois = package.runTimelineIndexJson.value(QStringLiteral("qoi_refs")).toArray();
    package.qoiOptions.reserve(qois.size());
    for (const QJsonValue& value : qois) {
        package.qoiOptions.push_back(readQoiOption(value.toObject(), branchesById));
    }

    QHash<QString, QString> onlineParentForRealtime;
    for (const BranchDescriptor& b : package.branches) {
        if (b.kind != BranchKind::RealtimePrediction || b.parentBranchId.isEmpty()) {
            continue;
        }
        const auto pit = branchesById.find(b.parentBranchId);
        if (pit != branchesById.end() && pit->kind == BranchKind::OnlineMainline) {
            onlineParentForRealtime.insert(b.branchId, b.parentBranchId);
        }
    }

    // ① 后验场轨的 branch_steps 可能只承载场输出索引，轨迹状态(h/ma/time)在 online_frames 里。
    //    按 frame/loop 索引把在线帧状态回填到后验场轨，否则 UI 显示 t=0/h=0。
    QHash<int, int> onlineFrameByLoop;  // loopIterationIndex -> timelinePoints 下标
    for (int i = 0; i < package.timelinePoints.size(); ++i) {
        const TimelinePoint& p = package.timelinePoints[i];
        if (p.kind == TimelinePointKind::OnlineFusionFrame) {
            onlineFrameByLoop.insert(p.loopIterationIndex >= 0 ? p.loopIterationIndex : p.frameIndex, i);
        }
    }
    for (TimelinePoint& p : package.timelinePoints) {
        if (p.kind != TimelinePointKind::RealtimePredictionFrame) {
            continue;
        }
        const int key = p.loopIterationIndex >= 0 ? p.loopIterationIndex : p.frameIndex;
        const auto it = onlineFrameByLoop.find(key);
        if (it == onlineFrameByLoop.end()) {
            continue;
        }
        const TimelinePoint& of = package.timelinePoints[it.value()];
        if (p.selectedState.isEmpty()) {
            p.selectedState = of.selectedState;
        }
        if (p.altitudeM == 0.0) {
            p.altitudeM = of.altitudeM;
        }
        if (p.sampleTimeS == 0.0) {
            p.sampleTimeS = of.sampleTimeS;
        }
        if (p.sourceTimeS == 0.0) {
            p.sourceTimeS = of.sourceTimeS;
        }
        if (p.sensorCount == 0) {
            p.sensorCount = of.sensorCount;
        }
    }

    // ② 后验场轨应该和在线主线等长。若某些 run 只有 artifact_refs 而缺少 branch_steps，
    //    用 online_frames 合成轻量 timeline point，确保 UI 能按主时间轴选择每个后验场时刻。
    if (!onlineParentForRealtime.isEmpty()) {
        QHash<QString, QSet<int>> realtimeLoops;
        for (const TimelinePoint& p : package.timelinePoints) {
            if (p.kind != TimelinePointKind::RealtimePredictionFrame) {
                continue;
            }
            realtimeLoops[p.branchId].insert(p.loopIterationIndex >= 0 ? p.loopIterationIndex : p.frameIndex);
        }
        QVector<TimelinePoint> synthesized;
        for (auto it = onlineParentForRealtime.constBegin(); it != onlineParentForRealtime.constEnd(); ++it) {
            const QString realtimeBranchId = it.key();
            const QString onlineBranchId = it.value();
            QSet<int>& loops = realtimeLoops[realtimeBranchId];
            for (const TimelinePoint& p : package.timelinePoints) {
                if (p.kind != TimelinePointKind::OnlineFusionFrame || p.branchId != onlineBranchId) {
                    continue;
                }
                const int loop = p.loopIterationIndex >= 0 ? p.loopIterationIndex : p.frameIndex;
                if (loops.contains(loop)) {
                    continue;
                }
                TimelinePoint copy = p;
                copy.kind = TimelinePointKind::RealtimePredictionFrame;
                copy.branchId = realtimeBranchId;
                copy.runId = runIdForBranch(branchesById, realtimeBranchId);
                copy.pointId = pointId(copy.branchId, copy.kind, copy.frameIndex, copy.loopIterationIndex);
                copy.rawJson.insert(QStringLiteral("branch_id"), realtimeBranchId);
                copy.rawJson.insert(QStringLiteral("point_kind_id"), QStringLiteral("realtime_prediction_frame"));
                copy.rawJson.insert(QStringLiteral("synthesized_from_online_frame"), true);
                synthesized.push_back(std::move(copy));
                loops.insert(loop);
            }
        }
        package.timelinePoints.reserve(package.timelinePoints.size() + synthesized.size());
        for (TimelinePoint& p : synthesized) {
            package.timelinePoints.push_back(std::move(p));
        }
    }

    // ③ 在线场重建归属在 realtime_prediction 子分支(role=online_field_reconstruction)。
    //    把这些场镜像一份到父在线分支，使「选中在线滤波分支」也能看到当前实时场。
    if (!onlineParentForRealtime.isEmpty()) {
        QVector<FieldArtifactOption> mirrored;
        for (const FieldArtifactOption& opt : package.fieldOptions) {
            const auto pit = onlineParentForRealtime.find(opt.branchId);
            if (pit == onlineParentForRealtime.end()) {
                continue;
            }
            FieldArtifactOption copy = opt;
            copy.branchId = pit.value();
            copy.optionId = opt.optionId + QStringLiteral("@online");
            mirrored.push_back(std::move(copy));
        }
        package.fieldOptions.reserve(package.fieldOptions.size() + mirrored.size());
        for (FieldArtifactOption& m : mirrored) {
            package.fieldOptions.push_back(std::move(m));
        }

        QVector<QoiOption> mirroredQois;
        for (const QoiOption& opt : package.qoiOptions) {
            const auto pit = onlineParentForRealtime.find(opt.branchId);
            if (pit == onlineParentForRealtime.end()) {
                continue;
            }
            QoiOption copy = opt;
            copy.branchId = pit.value();
            copy.optionId = opt.optionId + QStringLiteral("@online");
            mirroredQois.push_back(std::move(copy));
        }
        package.qoiOptions.reserve(package.qoiOptions.size() + mirroredQois.size());
        for (QoiOption& q : mirroredQois) {
            package.qoiOptions.push_back(std::move(q));
        }
    }

    std::sort(package.timelinePoints.begin(), package.timelinePoints.end(), [](const TimelinePoint& a, const TimelinePoint& b) {
        if (a.branchId != b.branchId) {
            return a.branchId < b.branchId;
        }
        if (a.kind != b.kind) {
            return static_cast<int>(a.kind) < static_cast<int>(b.kind);
        }
        return a.loopIterationIndex < b.loopIterationIndex;
    });

    return package;
}

} // namespace twin
