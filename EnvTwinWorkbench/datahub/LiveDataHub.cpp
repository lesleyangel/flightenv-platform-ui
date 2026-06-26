#include "LiveDataHub.h"

#include "BranchRunExplorer.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHash>
#include <QSet>
#include <QTimer>

#include <algorithm>
#include <cmath>

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

// 用 FILE_SHARE_DELETE 读取 run 目录文件：C++ Host 每帧用 MoveFileExW 原子替换
// run_timeline_index.json 等文件；UI 若用普通 QFile 持有句柄(不共享 delete)，会让 Host
// 的替换失败并崩溃整条主链。共享 delete 后 Host 替换可成功，UI 读到的是替换前的旧内容。
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

QJsonObject readJsonObject(const QString& path) {
    QByteArray payload = readRunFileBytes(path);
    if (payload.isEmpty()) {
        return {};
    }
    QJsonParseError err{};
    QJsonDocument doc = QJsonDocument::fromJson(payload, &err);
    if (err.error != QJsonParseError::NoError) {
        // Runtime Host 的历史 evidence 中可能出现 Infinity/NaN。
        // UI 只做展示，遇到非标准 JSON 数值时按空值读取，避免整份 evidence 失效。
        payload.replace("-Infinity", "null");
        payload.replace("Infinity", "null");
        payload.replace("NaN", "null");
        err = {};
        doc = QJsonDocument::fromJson(payload, &err);
    }
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return {};
    }
    return doc.object();
}

QString normalizeEvidencePath(QString path) {
    path = path.trimmed();
    if (path.startsWith(QStringLiteral("file://"))) {
        path = path.mid(QStringLiteral("file://").size());
    }
    if (path.startsWith(QStringLiteral("//?/")) || path.startsWith(QStringLiteral("\\\\?\\"))) {
        path = path.mid(4);
    }
    return QDir::toNativeSeparators(QDir::fromNativeSeparators(path));
}

QString filePathFromJson(const QJsonValue& value) {
    return normalizeEvidencePath(value.toString());
}

qint64 fileMtimeMs(const QString& path) {
    const QFileInfo info(path);
    return info.exists() ? info.lastModified().toMSecsSinceEpoch() : -1;
}

qint64 branchSnapshotMtime(const QString& mainlineRunDir) {
    const QDir dir(mainlineRunDir);
    qint64 mtime = -1;
    const QStringList names = {
        QStringLiteral("branch_registry.json"),
        QStringLiteral("run_timeline_index.json"),
        QStringLiteral("runtime_cursor.json"),
        QStringLiteral("mainline_progress.json"),
        QStringLiteral("mainline_summary.json"),
        QStringLiteral("runtime_host_evidence.json"),
        QStringLiteral("series_manifest.json")
    };
    for (const QString& name : names) {
        mtime = std::max(mtime, fileMtimeMs(dir.filePath(name)));
    }
    return mtime;
}

QString firstExistingFile(const QStringList& candidates) {
    for (const QString& candidate : candidates) {
        const QString clean = QDir::fromNativeSeparators(candidate);
        if (QFileInfo::exists(clean) && QFileInfo(clean).isFile()) {
            return QFileInfo(clean).absoluteFilePath();
        }
    }
    return {};
}

QString resolveRuntimeSnapshotForEvidence(const QString& mainlineDirPath, const QString& runDirPath) {
    const QDir mainlineDir(mainlineDirPath);
    const QDir runDir(runDirPath);
    const QString direct = firstExistingFile({
        runDir.filePath(QStringLiteral("runtime_snapshot.json")),
        mainlineDir.filePath(QStringLiteral("runtime_snapshot.json"))
    });
    if (!direct.isEmpty()) {
        return direct;
    }

    // Render only snapshots explicitly attached to the selected evidence.
    // The UI must render only the runtime_snapshot explicitly produced by the
    // selected evidence package, otherwise it reports a missing mesh snapshot.
    return {};
}

bool hasRuntimeRunEvidence(const QString& runDir) {
    const QDir dir(runDir);
    return QFileInfo::exists(dir.filePath(QStringLiteral("sensor_stream.json"))) ||
           QFileInfo::exists(dir.filePath(QStringLiteral("runtime_node_snapshot.json")));
}

qint64 runtimeRunEvidenceMtime(const QString& runDir) {
    const QDir dir(runDir);
    qint64 mtime = -1;
    const QStringList evidenceFiles = {
        QStringLiteral("sensor_stream.json"),
        QStringLiteral("runtime_node_snapshot.json"),
        QStringLiteral("scheduler_timeline.json"),
        QStringLiteral("runtime_loop_summary.json"),
        QStringLiteral("data_plane_manifest.json")
    };
    for (const QString& name : evidenceFiles) {
        const QFileInfo info(dir.filePath(name));
        if (info.exists()) {
            mtime = std::max(mtime, info.lastModified().toMSecsSinceEpoch());
        }
    }
    return mtime;
}

QString runDirFromPointerFile(const QString& evidenceRoot) {
    const QDir dir(evidenceRoot);
    const QStringList pointerNames = {
        QStringLiteral("live_run.json"),
        QStringLiteral("current_run.json"),
        QStringLiteral("latest_online_run.json"),
        QStringLiteral("live_pointer.json")
    };
    const QStringList fieldNames = {
        QStringLiteral("run_dir"),
        QStringLiteral("online_run_dir"),
        QStringLiteral("current_run_dir"),
        QStringLiteral("latest_run_dir")
    };
    for (const QString& name : pointerNames) {
        const QJsonObject pointer = readJsonObject(dir.filePath(name));
        if (pointer.isEmpty()) {
            continue;
        }
        for (const QString& field : fieldNames) {
            const QString runDir = filePathFromJson(pointer.value(field));
            if (!runDir.isEmpty() && hasRuntimeRunEvidence(runDir)) {
                return runDir;
            }
        }
    }
    return {};
}

QString onlineRunRootFromWorkspaceLikeRoot(const QString& evidenceRoot) {
    const QDir root(evidenceRoot);
    const QString direct = root.filePath(QStringLiteral("reentry.online_filtering.v1"));
    if (QFileInfo::exists(direct)) {
        return direct;
    }
    const QString workspaceDefault = root.filePath(
        QStringLiteral("_local_artifacts/platform-pdk/runtime-host-runs/reentry.online_filtering.v1"));
    if (QFileInfo::exists(workspaceDefault)) {
        return workspaceDefault;
    }
    return {};
}

QString latestRuntimeRunDirUnder(const QString& evidenceRoot) {
    if (hasRuntimeRunEvidence(evidenceRoot)) {
        return evidenceRoot;
    }

    const QString pointerRun = runDirFromPointerFile(evidenceRoot);
    if (!pointerRun.isEmpty()) {
        return pointerRun;
    }

    QString root = onlineRunRootFromWorkspaceLikeRoot(evidenceRoot);
    if (root.isEmpty()) {
        root = evidenceRoot;
    }

    QDir dir(root);
    if (!dir.exists()) {
        return {};
    }

    QString bestDir;
    qint64 bestMtime = -1;
    const QFileInfoList children =
        dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);
    for (const QFileInfo& child : children) {
        const QString candidate = child.absoluteFilePath();
        if (!hasRuntimeRunEvidence(candidate)) {
            continue;
        }
        const qint64 mtime = runtimeRunEvidenceMtime(candidate);
        if (mtime > bestMtime) {
            bestMtime = mtime;
            bestDir = candidate;
        }
    }
    return bestDir;
}

QJsonObject particleFilterSummary(const QJsonObject& node) {
    QJsonObject out;
    const QJsonObject runtimePacket = node.value(QStringLiteral("runtime_packet")).toObject();
    const QJsonObject payload = runtimePacket.value(QStringLiteral("payload")).toObject();
    const QJsonObject outputs = payload.value(QStringLiteral("outputs")).toObject();
    const QJsonObject posterior = outputs.value(QStringLiteral("state.posterior")).toObject();
    const QJsonObject uncertainty = posterior.value(QStringLiteral("uncertainty")).toObject();
    const QJsonObject components = posterior.value(QStringLiteral("components")).toObject();
    const QJsonObject ballistic = components.value(QStringLiteral("ballistic")).toObject();

    out.insert(QStringLiteral("effective_sample_size"),
               uncertainty.value(QStringLiteral("effective_sample_size")).toDouble());
    out.insert(QStringLiteral("normalized_effective_sample_size"),
               uncertainty.value(QStringLiteral("normalized_effective_sample_size")).toDouble());
    out.insert(QStringLiteral("particle_count"),
               uncertainty.value(QStringLiteral("particle_count")).toInt());
    out.insert(QStringLiteral("observation_residual_norm"),
               uncertainty.value(QStringLiteral("max_abs_residual")).toDouble());
    out.insert(QStringLiteral("resampled"),
               uncertainty.value(QStringLiteral("resampled")).toBool());
    out.insert(QStringLiteral("posterior_altitude_m"),
               ballistic.value(QStringLiteral("h")).toDouble());
    out.insert(QStringLiteral("posterior_mach"),
               ballistic.value(QStringLiteral("ma")).toDouble());
    out.insert(QStringLiteral("posterior_time_s"),
               ballistic.value(QStringLiteral("time_s")).toDouble());
    out.insert(QStringLiteral("status"),
               posterior.value(QStringLiteral("filter_status")).toString());
    return out;
}

QJsonObject schedulerSummary(const QString& runDir) {
    const QJsonObject scheduler = readJsonObject(QDir(runDir).filePath(QStringLiteral("scheduler_timeline.json")));
    QJsonObject out;
    out.insert(QStringLiteral("path"), QDir(runDir).filePath(QStringLiteral("scheduler_timeline.json")));
    const QJsonArray events = scheduler.value(QStringLiteral("events")).toArray();
    int tickCount = 0;
    int startCount = 0;
    int finishCount = 0;
    int missedDeadlines = 0;
    int maxParallelism = 0;
    int maxReadyNodes = 0;
    QSet<QString> workers;
    for (const QJsonValue& value : events) {
        const QJsonObject event = value.toObject();
        const QString kind = event.value(QStringLiteral("event")).toString();
        if (kind == QStringLiteral("tick")) {
            ++tickCount;
            maxParallelism = std::max(maxParallelism, event.value(QStringLiteral("max_parallelism")).toInt());
            maxReadyNodes = std::max(maxReadyNodes, static_cast<int>(
                event.value(QStringLiteral("ready_node_ids")).toArray().size()));
        } else if (kind == QStringLiteral("start")) {
            ++startCount;
            const QString worker = event.value(QStringLiteral("worker_thread")).toString();
            if (!worker.isEmpty()) {
                workers.insert(worker);
            }
        } else if (kind == QStringLiteral("finish")) {
            ++finishCount;
            if (event.value(QStringLiteral("deadline_status")).toString() == QStringLiteral("missed")) {
                ++missedDeadlines;
            }
        }
    }
    out.insert(QStringLiteral("event_count"), events.size());
    out.insert(QStringLiteral("tick_count"), tickCount);
    out.insert(QStringLiteral("start_count"), startCount);
    out.insert(QStringLiteral("finish_count"), finishCount);
    out.insert(QStringLiteral("missed_deadline_count"), missedDeadlines);
    out.insert(QStringLiteral("worker_count"), workers.size());
    out.insert(QStringLiteral("max_parallelism"), maxParallelism);
    out.insert(QStringLiteral("max_ready_nodes"), maxReadyNodes);
    return out;
}

QJsonObject runtimeLoopSummary(const QString& runDir) {
    const QJsonObject loop = readJsonObject(QDir(runDir).filePath(QStringLiteral("runtime_loop_summary.json")));
    const QJsonObject summary = loop.value(QStringLiteral("summary")).toObject();
    QJsonObject out = summary;
    out.insert(QStringLiteral("path"), QDir(runDir).filePath(QStringLiteral("runtime_loop_summary.json")));
    out.insert(QStringLiteral("iteration_count"), summary.value(QStringLiteral("iteration_count")).toInt());
    out.insert(QStringLiteral("stop_reason"), summary.value(QStringLiteral("stop_reason")).toString());
    out.insert(QStringLiteral("stopped"), summary.value(QStringLiteral("stopped")).toBool());
    return out;
}

QJsonArray stringListToJson(const QStringList& values) {
    QJsonArray array;
    for (const QString& value : values) {
        array.push_back(value);
    }
    return array;
}

QJsonArray issuesToJson(const QVector<PdkReadIssue>& issues) {
    QJsonArray array;
    for (const PdkReadIssue& issue : issues) {
        QJsonObject obj;
        obj.insert(QStringLiteral("severity"), issue.severity);
        obj.insert(QStringLiteral("code"), issue.code);
        obj.insert(QStringLiteral("message"), issue.message);
        obj.insert(QStringLiteral("path"), issue.path);
        array.push_back(obj);
    }
    return array;
}

void insertFinite(QJsonObject& obj, const QString& key, double value) {
    if (std::isfinite(value)) {
        obj.insert(key, value);
    }
}

QHash<QString, QString> runtimeSnapshotsByBranch(const RunPackage& package) {
    QHash<QString, QString> out;
    for (const BranchDescriptor& branch : package.branches) {
        out.insert(branch.branchId,
                   resolveRuntimeSnapshotForEvidence(package.mainlineRunDir, branch.runDir));
    }
    return out;
}

QJsonObject branchToJson(const BranchDescriptor& branch,
                         const QString& runtimeSnapshotPath) {
    QJsonObject obj = branch.rawJson;
    // ① 在线主线 + 实时场重建不是分叉：强制用 UI 语义标签覆盖 registry 的“实时预测分支”叫法；
    //    真分支（future_prediction）保留 registry 标签（含触发帧信息）。
    const bool forceUiLabel =
        branch.kind == BranchKind::OnlineMainline || branch.kind == BranchKind::RealtimePrediction;
    const QString displayName = forceUiLabel
        ? branch.displayName()
        : branch.rawJson.value(QStringLiteral("display_name")).toString(branch.displayName());
    const QString kindLabel = forceUiLabel
        ? branchKindText(branch.kind)
        : branch.rawJson.value(QStringLiteral("kind_label")).toString(branchKindText(branch.kind));
    obj.insert(QStringLiteral("display_name"), displayName);
    obj.insert(QStringLiteral("kind_label"), kindLabel);
    obj.insert(QStringLiteral("branch_id"), branch.branchId);
    obj.insert(QStringLiteral("branch_kind"), branch.rawKind);
    obj.insert(QStringLiteral("parent_branch_id"), branch.parentBranchId);
    obj.insert(QStringLiteral("workflow_id"), branch.workflowId);
    obj.insert(QStringLiteral("run_id"), branch.runId);
    obj.insert(QStringLiteral("run_dir"), branch.runDir);
    obj.insert(QStringLiteral("status"), branch.status);
    obj.insert(QStringLiteral("priority"), branch.priority);
    obj.insert(QStringLiteral("trigger_frame_index"), branch.triggerFrameIndex);
    obj.insert(QStringLiteral("trigger_time_s"), branch.triggerTimeS);
    obj.insert(QStringLiteral("seed_runtime_outputs_ref"), branch.seedRuntimeOutputsRef);
    obj.insert(QStringLiteral("runtime_snapshot_path"), runtimeSnapshotPath);
    obj.insert(QStringLiteral("summary"), branch.summary);
    return obj;
}

QJsonObject timelinePointToJson(const TimelinePoint& point) {
    QJsonObject obj = point.rawJson;
    obj.insert(QStringLiteral("point_id"), point.pointId);
    obj.insert(QStringLiteral("point_kind"), timelinePointKindText(point.kind));
    obj.insert(QStringLiteral("point_kind_id"),
               point.isFusionFrame() ? QStringLiteral("online_fusion_frame")
                                     : (point.isRealtimePredictionFrame() ? QStringLiteral("realtime_prediction_frame")
                                     : (point.isPredictionStep() ? QStringLiteral("prediction_step")
                                                                 : QStringLiteral("online_runtime_step"))));
    obj.insert(QStringLiteral("display_name"), point.displayName());
    obj.insert(QStringLiteral("branch_id"), point.branchId);
    obj.insert(QStringLiteral("run_id"), point.runId);
    obj.insert(QStringLiteral("source_run_dir"), point.sourceRunDir);
    obj.insert(QStringLiteral("source_runtime_outputs"), point.sourceRuntimeOutputs);
    obj.insert(QStringLiteral("frame_index"), point.frameIndex);
    obj.insert(QStringLiteral("mainline_frame_index"), point.mainlineFrameIndex);
    obj.insert(QStringLiteral("step_index"), point.stepIndex);
    obj.insert(QStringLiteral("loop_iteration_index"), point.loopIterationIndex);
    obj.insert(QStringLiteral("tick_index"), point.tickIndex);
    obj.insert(QStringLiteral("sample_time_s"), point.sampleTimeS);
    obj.insert(QStringLiteral("run_time_s"), point.runTimeS);
    obj.insert(QStringLiteral("source_time_s"), point.sourceTimeS);
    insertFinite(obj, QStringLiteral("public_time_s"), point.publicTimeS);
    insertFinite(obj, QStringLiteral("effective_delta_t_s"), point.effectiveDeltaTS);
    insertFinite(obj, QStringLiteral("output_period_s"), point.outputPeriodS);
    obj.insert(QStringLiteral("altitude_m"), point.altitudeM);
    obj.insert(QStringLiteral("remaining_life_s"), point.remainingLifeS);
    obj.insert(QStringLiteral("sensor_count"), point.sensorCount);
    obj.insert(QStringLiteral("status"), point.status);
    obj.insert(QStringLiteral("stop_reason"), point.stopReason);
    obj.insert(QStringLiteral("stopped"), point.stopped);
    obj.insert(QStringLiteral("time_point"), point.timePoint);
    obj.insert(QStringLiteral("time_summary"), point.timeSummary);
    obj.insert(QStringLiteral("selected_state"), point.selectedState);
    obj.insert(QStringLiteral("filter"), point.filterSummary);
    return obj;
}

QJsonObject fieldOptionToJson(const FieldArtifactOption& option,
                              const QString& runtimeSnapshotPath) {
    QJsonObject obj = option.rawJson;
    obj.insert(QStringLiteral("option_id"), option.optionId);
    obj.insert(QStringLiteral("display_name"), option.displayName());
    obj.insert(QStringLiteral("branch_id"), option.branchId);
    obj.insert(QStringLiteral("run_id"), option.runId);
    obj.insert(QStringLiteral("source_run_dir"), option.sourceRunDir);
    obj.insert(QStringLiteral("mainline_frame_index"), option.mainlineFrameIndex);
    obj.insert(QStringLiteral("step_index"), option.stepIndex);
    obj.insert(QStringLiteral("loop_iteration_index"), option.loopIterationIndex);
    obj.insert(QStringLiteral("node_id"), option.nodeId);
    obj.insert(QStringLiteral("operator_id"), option.operatorId);
    obj.insert(QStringLiteral("port_id"), option.portId);
    obj.insert(QStringLiteral("contract_id"), option.contractId);
    obj.insert(QStringLiteral("field_name"), option.fieldName);
    obj.insert(QStringLiteral("field_role"), option.fieldRole);
    obj.insert(QStringLiteral("component_id"), option.componentId);
    obj.insert(QStringLiteral("mesh_ref"), option.meshRef);
    obj.insert(QStringLiteral("layout_ref"), option.layoutRef);
    obj.insert(QStringLiteral("unit"), option.unit);
    obj.insert(QStringLiteral("representation"), option.representation);
    obj.insert(QStringLiteral("ref"), option.ref);
    obj.insert(QStringLiteral("artifact_uri"), option.artifactUri);
    obj.insert(QStringLiteral("artifact_path"), option.artifactPath);
    obj.insert(QStringLiteral("runtime_snapshot_path"), runtimeSnapshotPath);
    obj.insert(QStringLiteral("checksum"), option.checksum);
    obj.insert(QStringLiteral("evidence_ref"), option.evidenceRef);
    obj.insert(QStringLiteral("shape"), stringListToJson(option.shape));
    obj.insert(QStringLiteral("node_count"), static_cast<double>(option.nodeCount));
    insertFinite(obj, QStringLiteral("public_time_s"), option.publicTimeS);
    obj.insert(QStringLiteral("time_point"), option.timePoint);
    obj.insert(QStringLiteral("statistics"), option.statistics);
    return obj;
}

QJsonObject qoiOptionToJson(const QoiOption& option) {
    QJsonObject obj = option.rawJson;
    obj.insert(QStringLiteral("option_id"), option.optionId);
    obj.insert(QStringLiteral("display_name"), option.displayName());
    obj.insert(QStringLiteral("branch_id"), option.branchId);
    obj.insert(QStringLiteral("run_id"), option.runId);
    obj.insert(QStringLiteral("source_run_dir"), option.sourceRunDir);
    obj.insert(QStringLiteral("mainline_frame_index"), option.mainlineFrameIndex);
    obj.insert(QStringLiteral("step_index"), option.stepIndex);
    obj.insert(QStringLiteral("loop_iteration_index"), option.loopIterationIndex);
    obj.insert(QStringLiteral("node_id"), option.nodeId);
    obj.insert(QStringLiteral("operator_id"), option.operatorId);
    obj.insert(QStringLiteral("port_id"), option.portId);
    obj.insert(QStringLiteral("contract_id"), option.contractId);
    obj.insert(QStringLiteral("qoi_name"), option.qoiName);
    obj.insert(QStringLiteral("representation"), option.representation);
    obj.insert(QStringLiteral("ref"), option.ref);
    obj.insert(QStringLiteral("evidence_ref"), option.evidenceRef);
    obj.insert(QStringLiteral("checksum"), option.checksum);
    obj.insert(QStringLiteral("inline_byte_size"), static_cast<double>(option.inlineByteSize));
    insertFinite(obj, QStringLiteral("public_time_s"), option.publicTimeS);
    obj.insert(QStringLiteral("time_point"), option.timePoint);
    obj.insert(QStringLiteral("statistics"), option.statistics);
    return obj;
}

QHash<QString, int> fieldCountByBranch(const QVector<FieldArtifactOption>& fields) {
    QHash<QString, int> counts;
    for (const FieldArtifactOption& field : fields) {
        counts.insert(field.branchId, counts.value(field.branchId) + 1);
    }
    return counts;
}

QHash<QString, int> lastFieldStepByBranch(const QVector<FieldArtifactOption>& fields) {
    QHash<QString, int> lastSteps;
    for (const FieldArtifactOption& field : fields) {
        lastSteps.insert(field.branchId,
                         std::max(lastSteps.value(field.branchId, -1), field.loopIterationIndex));
    }
    return lastSteps;
}

QJsonArray predictionRunsFromPackage(const RunPackage& package,
                                     const QHash<QString, QString>& runtimeSnapshots) {
    const QHash<QString, int> fieldCounts = fieldCountByBranch(package.fieldOptions);
    const QHash<QString, int> lastFieldSteps = lastFieldStepByBranch(package.fieldOptions);
    QJsonArray runs;
    for (const BranchDescriptor& branch : package.branches) {
        if (!branch.isPrediction()) {
            continue;
        }
        const QJsonObject summary = branch.summary;
        const int lastFieldStep = lastFieldSteps.value(branch.branchId, -1);
        QJsonObject run;
        run.insert(QStringLiteral("branch_id"), branch.branchId);
        run.insert(QStringLiteral("branch_kind"), branch.rawKind);
        run.insert(QStringLiteral("trigger_frame_index"), branch.triggerFrameIndex);
        run.insert(QStringLiteral("trigger_time_s"), branch.triggerTimeS);
        run.insert(QStringLiteral("run_id"), branch.runId);
        run.insert(QStringLiteral("run_dir"), branch.runDir);
        run.insert(QStringLiteral("status"), branch.status);
        run.insert(QStringLiteral("runtime_snapshot_path"), runtimeSnapshots.value(branch.branchId));
        run.insert(QStringLiteral("iteration_count"),
                   summary.value(QStringLiteral("iteration_count")).toInt(lastFieldStep + 1));
        run.insert(QStringLiteral("stop_reason"), summary.value(QStringLiteral("stop_reason")).toString());
        run.insert(QStringLiteral("remaining_life_s"),
                   summary.value(QStringLiteral("remaining_life_s")).toDouble());
        run.insert(QStringLiteral("field_artifact_count"),
                   summary.value(QStringLiteral("field_artifact_count")).toInt(
                       fieldCounts.value(branch.branchId)));
        run.insert(QStringLiteral("last_field_step_index"), lastFieldStep);
        run.insert(QStringLiteral("summary"), summary);
        runs.push_back(run);
    }
    return runs;
}

QJsonObject clockFromCursor(const QJsonObject& cursor) {
    QJsonObject clock;
    clock.insert(QStringLiteral("source"), QStringLiteral("runtime_cursor"));
    clock.insert(QStringLiteral("run_time_s"), cursor.value(QStringLiteral("time_s")).toDouble());
    clock.insert(QStringLiteral("source_time_s"), cursor.value(QStringLiteral("time_s")).toDouble());
    clock.insert(QStringLiteral("tick_index"), cursor.value(QStringLiteral("frame_index")).toInt());
    return clock;
}

QJsonObject buildBranchTimelineSnapshot(const QString& mainlineRunDir) {
    const RunPackage package = BranchRunExplorerReader().read(mainlineRunDir);
    if (package.runTimelineIndexJson.isEmpty()) {
        return {};
    }

    const QHash<QString, QString> runtimeSnapshots = runtimeSnapshotsByBranch(package);
    const QJsonObject progress = readJsonObject(
        QDir(package.mainlineRunDir).filePath(QStringLiteral("mainline_progress.json")));
    const QJsonObject cursor = readJsonObject(package.runtimeCursorPath);
    const bool live = cursor.value(QStringLiteral("follow_live")).toBool(false) ||
                      cursor.value(QStringLiteral("status")).toString() == QStringLiteral("running") ||
                      progress.value(QStringLiteral("status")).toString() == QStringLiteral("running") ||
                      package.status == QStringLiteral("running");

    QJsonArray branches;
    for (const BranchDescriptor& branch : package.branches) {
        branches.push_back(branchToJson(branch, runtimeSnapshots.value(branch.branchId)));
    }

    QJsonArray timelinePoints;
    for (const TimelinePoint& point : package.timelinePoints) {
        timelinePoints.push_back(timelinePointToJson(point));
    }

    QJsonArray fieldOptions;
    for (const FieldArtifactOption& option : package.fieldOptions) {
        fieldOptions.push_back(fieldOptionToJson(option, runtimeSnapshots.value(option.branchId)));
    }

    QJsonArray qoiOptions;
    for (const QoiOption& option : package.qoiOptions) {
        qoiOptions.push_back(qoiOptionToJson(option));
    }

    QJsonObject runtime;
    if (const BranchDescriptor* mainBranch = package.branchById(package.primaryBranchId)) {
        runtime.insert(QStringLiteral("iteration_count"),
                       mainBranch->summary.value(QStringLiteral("step_count")).toInt(
                           package.timelinePoints.size()));
        runtime.insert(QStringLiteral("stop_reason"),
                       mainBranch->summary.value(QStringLiteral("stop_reason")).toString());
        runtime.insert(QStringLiteral("stopped"), !live);
    }

    QJsonObject snapshot;
    snapshot.insert(QStringLiteral("schema_version"), QStringLiteral("flightenv.ui.branch_timeline_snapshot.v1"));
    snapshot.insert(QStringLiteral("mode"), QStringLiteral("platform_branch_index"));
    snapshot.insert(QStringLiteral("view_mode"), live ? QStringLiteral("live") : QStringLiteral("replay"));
    snapshot.insert(QStringLiteral("source_kind"), QStringLiteral("run_timeline_index"));
    snapshot.insert(QStringLiteral("live"), live);
    snapshot.insert(QStringLiteral("status"), live ? QStringLiteral("running") : QStringLiteral("ok"));
    snapshot.insert(QStringLiteral("mainline_run_dir"), package.mainlineRunDir);
    snapshot.insert(QStringLiteral("run_dir"), package.mainlineRunDir);
    snapshot.insert(QStringLiteral("run_id"), package.runId);
    snapshot.insert(QStringLiteral("workflow_id"), package.workflowId);
    snapshot.insert(QStringLiteral("object_id"), package.objectId);
    snapshot.insert(QStringLiteral("primary_branch_id"), package.primaryBranchId);
    snapshot.insert(QStringLiteral("branch_registry_path"), package.branchRegistryPath);
    snapshot.insert(QStringLiteral("run_timeline_index_path"), package.runTimelineIndexPath);
    snapshot.insert(QStringLiteral("runtime_cursor_path"), package.runtimeCursorPath);
    snapshot.insert(QStringLiteral("runtime_snapshot_path"),
                    runtimeSnapshots.value(package.primaryBranchId));
    snapshot.insert(QStringLiteral("last_update_mtime_ms"), branchSnapshotMtime(package.mainlineRunDir));
    snapshot.insert(QStringLiteral("branches"), branches);
    snapshot.insert(QStringLiteral("branch_descriptors"), branches);
    snapshot.insert(QStringLiteral("timeline_points"), timelinePoints);
    snapshot.insert(QStringLiteral("online_frames"),
                    package.runTimelineIndexJson.value(QStringLiteral("online_frames")).toArray());
    snapshot.insert(QStringLiteral("branch_steps"),
                    package.runTimelineIndexJson.value(QStringLiteral("branch_steps")).toArray());
    snapshot.insert(QStringLiteral("field_artifact_options"), fieldOptions);
    snapshot.insert(QStringLiteral("qoi_options"), qoiOptions);
    snapshot.insert(QStringLiteral("artifact_refs"),
                    package.runTimelineIndexJson.value(QStringLiteral("artifact_refs")).toArray());
    snapshot.insert(QStringLiteral("qoi_refs"),
                    package.runTimelineIndexJson.value(QStringLiteral("qoi_refs")).toArray());
    snapshot.insert(QStringLiteral("prediction_runs"),
                    predictionRunsFromPackage(package, runtimeSnapshots));
    snapshot.insert(QStringLiteral("prediction_run_count"), package.predictionBranchCount());
    snapshot.insert(QStringLiteral("observed_frame_count"), package.onlineFrameCount());
    snapshot.insert(QStringLiteral("mainline_progress"), progress);
    snapshot.insert(QStringLiteral("active_cursor"), cursor);
    snapshot.insert(QStringLiteral("clock"),
                    progress.value(QStringLiteral("clock")).isObject()
                        ? progress.value(QStringLiteral("clock")).toObject()
                        : clockFromCursor(cursor));
    snapshot.insert(QStringLiteral("runtime"), runtime);
    snapshot.insert(QStringLiteral("series_manifest_refs"),
                    package.runTimelineIndexJson.value(QStringLiteral("series_manifest_refs")).toArray());
    snapshot.insert(QStringLiteral("read_issues"), issuesToJson(package.issues));
    return snapshot;
}

QJsonObject buildPlatformRunTimeline(const QString& runDir) {
    const QDir dir(runDir);
    const QJsonObject sensorStream = readJsonObject(dir.filePath(QStringLiteral("sensor_stream.json")));
    const QJsonObject nodeSnapshot = readJsonObject(dir.filePath(QStringLiteral("runtime_node_snapshot.json")));
    if (sensorStream.isEmpty() && nodeSnapshot.isEmpty()) {
        return {};
    }

    QHash<int, QJsonObject> filtersByIteration;
    for (const QJsonValue& value : nodeSnapshot.value(QStringLiteral("nodes")).toArray()) {
        const QJsonObject node = value.toObject();
        if (node.value(QStringLiteral("operator_id")).toString() != QStringLiteral("reentry.particle_filter.atomic.v1")) {
            continue;
        }
        filtersByIteration.insert(node.value(QStringLiteral("loop_iteration_index")).toInt(),
                                  particleFilterSummary(node));
    }

    QJsonArray onlineFrames;
    const QJsonArray frames = sensorStream.value(QStringLiteral("frames")).toArray();
    for (const QJsonValue& value : frames) {
        QJsonObject frame = value.toObject();
        const int loopIndex = frame.value(QStringLiteral("loop_iteration_index"))
                                  .toInt(frame.value(QStringLiteral("frame_index")).toInt());
        if (filtersByIteration.contains(loopIndex)) {
            frame.insert(QStringLiteral("filter"), filtersByIteration.value(loopIndex));
        }
        onlineFrames.push_back(frame);
    }

    const QJsonObject sensorSummary = sensorStream.value(QStringLiteral("summary")).toObject();
    const QJsonObject scheduler = schedulerSummary(runDir);
    const QJsonObject runtime = runtimeLoopSummary(runDir);
    const bool isRunning = !runtime.value(QStringLiteral("stopped")).toBool(false);
    QJsonObject timeline;
    timeline.insert(QStringLiteral("schema_version"), QStringLiteral("flightenv.platform.ui_timeline.v1"));
    timeline.insert(QStringLiteral("mode"), QStringLiteral("platform_online_run"));
    timeline.insert(QStringLiteral("view_mode"), isRunning ? QStringLiteral("live") : QStringLiteral("replay"));
    timeline.insert(QStringLiteral("source_kind"), QStringLiteral("runtime_host_online_run"));
    timeline.insert(QStringLiteral("live"), isRunning);
    timeline.insert(QStringLiteral("status"), isRunning ? QStringLiteral("running") : QStringLiteral("ok"));
    timeline.insert(QStringLiteral("run_dir"), runDir);
    timeline.insert(QStringLiteral("last_update_mtime_ms"), runtimeRunEvidenceMtime(runDir));
    timeline.insert(QStringLiteral("run_id"), sensorStream.value(QStringLiteral("run_id")).toString(
        nodeSnapshot.value(QStringLiteral("run_id")).toString()));
    timeline.insert(QStringLiteral("workflow_id"), sensorStream.value(QStringLiteral("workflow_id")).toString(
        nodeSnapshot.value(QStringLiteral("workflow_id")).toString()));
    timeline.insert(QStringLiteral("object_id"), sensorStream.value(QStringLiteral("object_id")).toString(
        nodeSnapshot.value(QStringLiteral("object_id")).toString()));
    timeline.insert(QStringLiteral("observed_frame_count"),
                    sensorSummary.value(QStringLiteral("frame_count")).toInt(onlineFrames.size()));
    timeline.insert(QStringLiteral("sensor_count_min"),
                    sensorSummary.value(QStringLiteral("sensor_count_min")).toInt());
    timeline.insert(QStringLiteral("sensor_count_max"),
                    sensorSummary.value(QStringLiteral("sensor_count_max")).toInt());
    timeline.insert(QStringLiteral("online_frames"), onlineFrames);
    timeline.insert(QStringLiteral("prediction_runs"), QJsonArray{});
    timeline.insert(QStringLiteral("prediction_run_count"), 0);
    timeline.insert(QStringLiteral("scheduler"), scheduler);
    timeline.insert(QStringLiteral("runtime"), runtime);

    QJsonObject branch;
    branch.insert(QStringLiteral("branch_id"), QStringLiteral("main.online"));
    branch.insert(QStringLiteral("branch_kind"), QStringLiteral("online_mainline"));
    branch.insert(QStringLiteral("display_name"), QStringLiteral("在线融合主线"));
    branch.insert(QStringLiteral("kind_label"), QStringLiteral("在线融合"));
    branch.insert(QStringLiteral("run_id"), timeline.value(QStringLiteral("run_id")));
    branch.insert(QStringLiteral("run_dir"), runDir);
    branch.insert(QStringLiteral("workflow_id"), timeline.value(QStringLiteral("workflow_id")));
    branch.insert(QStringLiteral("status"), timeline.value(QStringLiteral("status")));
    QJsonArray branchArray;
    branchArray.push_back(branch);
    timeline.insert(QStringLiteral("branches"), branchArray);
    timeline.insert(QStringLiteral("branch_descriptors"), branchArray);
    timeline.insert(QStringLiteral("timeline_points"), onlineFrames);
    timeline.insert(QStringLiteral("field_artifact_options"), QJsonArray{});
    timeline.insert(QStringLiteral("qoi_options"), QJsonArray{});
    return timeline;
}

QJsonObject buildPlatformMainlineTimeline(const QString& mainlineSummaryPath) {
    const QJsonObject mainline = readJsonObject(mainlineSummaryPath);
    if (mainline.isEmpty()) {
        return {};
    }
    const QDir mainlineDir(QFileInfo(mainlineSummaryPath).absolutePath());
    const QJsonObject online = mainline.value(QStringLiteral("online")).toObject();
    const QString onlineRunDir = filePathFromJson(online.value(QStringLiteral("run_dir")));
    QJsonObject timeline = buildPlatformRunTimeline(onlineRunDir);
    if (timeline.isEmpty()) {
        return {};
    }

    timeline.insert(QStringLiteral("mode"), QStringLiteral("platform_mainline"));
    timeline.insert(QStringLiteral("view_mode"), QStringLiteral("replay"));
    timeline.insert(QStringLiteral("source_kind"), QStringLiteral("mainline_replay"));
    timeline.insert(QStringLiteral("live"), false);
    timeline.insert(QStringLiteral("status"), QStringLiteral("ok"));
    timeline.insert(QStringLiteral("mainline_summary_path"), mainlineSummaryPath);
    timeline.insert(QStringLiteral("prediction_run_count"),
                    mainline.value(QStringLiteral("prediction")).toObject().value(QStringLiteral("run_count")).toInt());
    timeline.insert(QStringLiteral("runtime_snapshot_path"),
                    resolveRuntimeSnapshotForEvidence(mainlineDir.absolutePath(), onlineRunDir));
    QJsonArray enrichedRuns;
    for (const QJsonValue& value : mainline.value(QStringLiteral("prediction")).toObject().value(QStringLiteral("runs")).toArray()) {
        QJsonObject run = value.toObject();
        const QString runDir = filePathFromJson(run.value(QStringLiteral("run_dir")));
        run.insert(QStringLiteral("runtime_snapshot_path"),
                   resolveRuntimeSnapshotForEvidence(mainlineDir.absolutePath(), runDir));
        enrichedRuns.push_back(run);
    }
    timeline.insert(QStringLiteral("prediction_runs"), enrichedRuns);
    timeline.insert(QStringLiteral("acceptance"),
                    mainline.value(QStringLiteral("acceptance")).toObject());
    const QString progressPath = mainlineDir.filePath(QStringLiteral("mainline_progress.json"));
    const QJsonObject progress = readJsonObject(progressPath);
    if (!progress.isEmpty()) {
        timeline.insert(QStringLiteral("mainline_progress"), progress);
    }
    return timeline;
}

QJsonObject buildPlatformTimelineFromIndex(const QString& indexPath) {
    return buildBranchTimelineSnapshot(QFileInfo(indexPath).absolutePath());
}

} // namespace

LiveDataHub::LiveDataHub(QString evidenceRoot, QObject* parent)
    : QObject(parent),
      evidenceRoot_(std::move(evidenceRoot)) {
    timelinePath_ = QDir(evidenceRoot_).filePath(QStringLiteral("workflow_timeline.json"));
    branchRegistryPath_ = QDir(evidenceRoot_).filePath(QStringLiteral("branch_registry.json"));
    runTimelineIndexPath_ = QDir(evidenceRoot_).filePath(QStringLiteral("run_timeline_index.json"));
    mainlineSummaryPath_ = QDir(evidenceRoot_).filePath(QStringLiteral("mainline_summary.json"));
    mainlineProgressPath_ = QDir(evidenceRoot_).filePath(QStringLiteral("mainline_progress.json"));
    runtimeCursorPath_ = QDir(evidenceRoot_).filePath(QStringLiteral("runtime_cursor.json"));
}

void LiveDataHub::start() {
    pollTimer_ = new QTimer(this);
    pollTimer_->setInterval(500);
    connect(pollTimer_, &QTimer::timeout, this, &LiveDataHub::pollTimeline);
    pollTimer_->start();
    pollTimeline();
}

bool LiveDataHub::latestRuntimeSnapshot(contracts::RuntimeSnapshotDTO& out) const {
    (void)out;
    return false;
}

void LiveDataHub::setEvidenceRoot(const QString& evidenceRoot) {
    const QString cleanRoot = QDir::fromNativeSeparators(evidenceRoot);
    if (QDir::cleanPath(cleanRoot) == QDir::cleanPath(evidenceRoot_)) {
        return;
    }
    evidenceRoot_ = cleanRoot;
    timelinePath_ = QDir(evidenceRoot_).filePath(QStringLiteral("workflow_timeline.json"));
    branchRegistryPath_ = QDir(evidenceRoot_).filePath(QStringLiteral("branch_registry.json"));
    runTimelineIndexPath_ = QDir(evidenceRoot_).filePath(QStringLiteral("run_timeline_index.json"));
    mainlineSummaryPath_ = QDir(evidenceRoot_).filePath(QStringLiteral("mainline_summary.json"));
    mainlineProgressPath_ = QDir(evidenceRoot_).filePath(QStringLiteral("mainline_progress.json"));
    runtimeCursorPath_ = QDir(evidenceRoot_).filePath(QStringLiteral("runtime_cursor.json"));
    lastTimelineMtime_ = -1;
    lastRunTimelineIndexMtime_ = -1;
    lastMainlineMtime_ = -1;
    lastMainlineProgressMtime_ = -1;
    lastRunDirMtime_ = -1;
    lastRunDir_.clear();
    pollTimeline();
}

void LiveDataHub::pollTimeline() {
    if (evidenceRoot_.isEmpty()) {
        return;
    }
    if (pollRunTimelineIndex()) {
        return;
    }
    if (pollWorkflowTimeline()) {
        return;
    }
    if (pollPlatformMainline()) {
        return;
    }
    if (pollPlatformMainlineProgress()) {
        return;
    }
    if (pollLatestPlatformRunDir()) {
        return;
    }
    pollPlatformRunDir(evidenceRoot_);
}

bool LiveDataHub::pollRunTimelineIndex() {
    QFileInfo info(runTimelineIndexPath_);
    if (!info.exists() || !info.isFile()) {
        return false;
    }
    const qint64 mtime = branchSnapshotMtime(evidenceRoot_);
    if (mtime == lastRunTimelineIndexMtime_) {
        return true;
    }
    lastRunTimelineIndexMtime_ = mtime;

    const QJsonObject timeline = buildPlatformTimelineFromIndex(runTimelineIndexPath_);
    if (!timeline.isEmpty()) {
        emit timelineUpdated(timeline);
    }
    return true;
}

bool LiveDataHub::pollWorkflowTimeline() {
    QFileInfo info(timelinePath_);
    if (!info.exists() || !info.isFile()) {
        return false;
    }
    const qint64 mtime = info.lastModified().toMSecsSinceEpoch();
    if (mtime == lastTimelineMtime_) {
        return true;
    }
    lastTimelineMtime_ = mtime;

    const QJsonObject timeline = readJsonObject(timelinePath_);
    if (!timeline.isEmpty()) {
        emit timelineUpdated(timeline);
    }
    return true;
}

bool LiveDataHub::pollPlatformMainline() {
    QFileInfo info(mainlineSummaryPath_);
    if (!info.exists() || !info.isFile()) {
        return false;
    }
    const qint64 mtime = info.lastModified().toMSecsSinceEpoch();
    if (mtime == lastMainlineMtime_) {
        return true;
    }
    lastMainlineMtime_ = mtime;

    const QJsonObject timeline = buildPlatformMainlineTimeline(mainlineSummaryPath_);
    if (!timeline.isEmpty()) {
        emit timelineUpdated(timeline);
    }
    return true;
}

bool LiveDataHub::pollPlatformMainlineProgress() {
    QFileInfo info(mainlineProgressPath_);
    if (!info.exists() || !info.isFile()) {
        return false;
    }
    const qint64 mtime = info.lastModified().toMSecsSinceEpoch();
    if (mtime == lastMainlineProgressMtime_) {
        return true;
    }
    lastMainlineProgressMtime_ = mtime;

    const QJsonObject progress = readJsonObject(mainlineProgressPath_);
    if (progress.isEmpty()) {
        return true;
    }

    const QJsonObject online = progress.value(QStringLiteral("online")).toObject();
    const QString onlineRunDir = filePathFromJson(online.value(QStringLiteral("run_dir")));
    QJsonObject timeline = buildPlatformRunTimeline(onlineRunDir);
    if (timeline.isEmpty()) {
        timeline.insert(QStringLiteral("schema_version"), QStringLiteral("flightenv.platform.ui_timeline.v1"));
        timeline.insert(QStringLiteral("online_frames"), QJsonArray{});
        timeline.insert(QStringLiteral("prediction_runs"), QJsonArray{});
    }

    const QJsonObject prediction = progress.value(QStringLiteral("prediction")).toObject();
    const int completedFrames = online.value(QStringLiteral("completed_frames")).toInt(-1);
    if (completedFrames >= 0) {
        const QJsonArray allFrames = timeline.value(QStringLiteral("online_frames")).toArray();
        if (completedFrames < allFrames.size()) {
            QJsonArray visibleFrames;
            for (int i = 0; i < completedFrames; ++i) {
                visibleFrames.push_back(allFrames.at(i));
            }
            timeline.insert(QStringLiteral("online_frames"), visibleFrames);
        }
    }
    timeline.insert(QStringLiteral("mode"), QStringLiteral("platform_mainline"));
    timeline.insert(QStringLiteral("view_mode"),
                    progress.value(QStringLiteral("status")).toString() == QStringLiteral("running")
                        ? QStringLiteral("live")
                        : QStringLiteral("replay"));
    timeline.insert(QStringLiteral("source_kind"), QStringLiteral("mainline_progress"));
    timeline.insert(QStringLiteral("live"),
                    progress.value(QStringLiteral("status")).toString() == QStringLiteral("running"));
    timeline.insert(QStringLiteral("status"), progress.value(QStringLiteral("status")).toString(QStringLiteral("running")));
    timeline.insert(QStringLiteral("run_id"), progress.value(QStringLiteral("run_id_prefix")).toString());
    timeline.insert(QStringLiteral("object_id"), progress.value(QStringLiteral("object_id")).toString());
    timeline.insert(QStringLiteral("mainline_progress_path"), mainlineProgressPath_);
    timeline.insert(QStringLiteral("mainline_progress"), progress);
    timeline.insert(QStringLiteral("clock"), progress.value(QStringLiteral("clock")).toObject());
    timeline.insert(QStringLiteral("observed_frame_count"),
                    online.value(QStringLiteral("completed_frames")).toInt());
    timeline.insert(QStringLiteral("prediction_run_count"),
                    prediction.value(QStringLiteral("completed_runs")).toInt());
    timeline.insert(QStringLiteral("prediction_runs"),
                    prediction.value(QStringLiteral("runs")).toArray());
    emit timelineUpdated(timeline);
    return true;
}

bool LiveDataHub::pollLatestPlatformRunDir() {
    const QString runDir = latestRuntimeRunDirUnder(evidenceRoot_);
    if (runDir.isEmpty() || QDir::cleanPath(runDir) == QDir::cleanPath(evidenceRoot_)) {
        return false;
    }
    return pollPlatformRunDir(runDir);
}

bool LiveDataHub::pollPlatformRunDir(const QString& runDir) {
    const QString sensorPath = QDir(runDir).filePath(QStringLiteral("sensor_stream.json"));
    const QString snapshotPath = QDir(runDir).filePath(QStringLiteral("runtime_node_snapshot.json"));
    QFileInfo sensorInfo(sensorPath);
    QFileInfo snapshotInfo(snapshotPath);
    if (!sensorInfo.exists() && !snapshotInfo.exists()) {
        return false;
    }
    const qint64 mtime = runtimeRunEvidenceMtime(runDir);
    if (QDir::cleanPath(runDir) != QDir::cleanPath(lastRunDir_)) {
        lastRunDir_ = runDir;
        lastRunDirMtime_ = -1;
    }
    if (mtime == lastRunDirMtime_) {
        return true;
    }
    lastRunDirMtime_ = mtime;

    const QJsonObject timeline = buildPlatformRunTimeline(runDir);
    if (!timeline.isEmpty()) {
        emit timelineUpdated(timeline);
    }
    return true;
}

} // namespace twin
