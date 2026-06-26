#include "PdkUiReaders.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>

#include <algorithm>
#include <utility>

namespace twin {
namespace {

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
    QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        addIssue(issues,
                 required ? QStringLiteral("error") : QStringLiteral("warning"),
                 missingCode,
                 QStringLiteral("JSON 文件不存在"),
                 path);
        return {};
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        addIssue(issues,
                 required ? QStringLiteral("error") : QStringLiteral("warning"),
                 QStringLiteral("file_open_failed"),
                 file.errorString(),
                 path);
        return {};
    }

    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
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

QStringList readStringList(const QJsonValue& value) {
    QStringList out;
    for (const QJsonValue& item : value.toArray()) {
        if (item.isString()) {
            out.push_back(item.toString());
        }
    }
    return out;
}

QVector<PdkPortView> readPorts(const QJsonArray& ports) {
    QVector<PdkPortView> out;
    out.reserve(ports.size());
    for (const QJsonValue& value : ports) {
        const QJsonObject obj = value.toObject();
        PdkPortView port;
        port.portId = obj.value(QStringLiteral("port_id")).toString();
        port.frameContract = obj.value(QStringLiteral("frame_contract")).toString();
        port.contractId = obj.value(QStringLiteral("contract_id")).toString();
        port.valueKind = obj.value(QStringLiteral("value_kind")).toString();
        port.required = obj.value(QStringLiteral("required")).toBool(false);
        const QJsonObject typed = obj.value(QStringLiteral("typed_io_contract")).toObject();
        port.typedStatus = typed.value(QStringLiteral("status")).toString();
        port.typedDtoName = typed.value(QStringLiteral("dto_name")).toString();
        port.typedTypeName = typed.value(QStringLiteral("type_name")).toString();
        port.typedSchemaId = typed.value(QStringLiteral("schema_id")).toString();
        port.bufferLayoutId = typed.value(QStringLiteral("buffer_layout_id")).toString();
        port.zeroCopyEligible = typed.value(QStringLiteral("zero_copy_eligible")).toBool(false);
        port.jsonIoForbidden = typed.value(QStringLiteral("json_operator_io_forbidden")).toBool(false);
        out.push_back(std::move(port));
    }
    return out;
}

PdkOperatorDisplayView readDisplay(const QJsonObject& obj) {
    const QJsonObject display = obj.value(QStringLiteral("display_descriptor")).toObject();
    PdkOperatorDisplayView out;
    out.rendererId = display.value(QStringLiteral("renderer_id")).toString();
    out.displayTitle = display.value(QStringLiteral("display_title")).toString();
    out.fallbackRenderer = display.value(QStringLiteral("fallback_renderer")).toString();
    out.primaryOutputs = readStringList(display.value(QStringLiteral("primary_outputs")));
    out.views = readStringList(display.value(QStringLiteral("views")));
    out.series = readStringList(display.value(QStringLiteral("series")));
    out.artifactPorts = readStringList(display.value(QStringLiteral("artifact_ports")));
    out.probePorts = readStringList(display.value(QStringLiteral("probe_ports")));
    out.displayRoles = readStringList(display.value(QStringLiteral("display_roles")));
    out.meshRefRole = display.value(QStringLiteral("mesh_ref_role")).toString();
    out.valueRefPort = display.value(QStringLiteral("value_ref_port")).toString();
    out.expectedValueKind = display.value(QStringLiteral("expected_value_kind")).toString();
    return out;
}

QSet<QString> outputPortSet(const PdkOperatorView& op) {
    QSet<QString> out;
    for (const PdkPortView& port : op.outputs) {
        out.insert(port.portId);
    }
    return out;
}

void validateDisplayPorts(const PdkOperatorView& op, QVector<PdkReadIssue>& issues) {
    if (op.display.rendererId.isEmpty()) {
        addIssue(issues,
                 QStringLiteral("error"),
                 QStringLiteral("display_descriptor_missing"),
                 QStringLiteral("operator 缺少 display_descriptor.renderer_id"),
                 op.path);
        return;
    }
    if (op.display.fallbackRenderer.isEmpty()) {
        addIssue(issues,
                 QStringLiteral("error"),
                 QStringLiteral("display_fallback_missing"),
                 QStringLiteral("operator 缺少 display_descriptor.fallback_renderer"),
                 op.path);
    }
    const QSet<QString> outputs = outputPortSet(op);
    for (const QString& port : op.display.primaryOutputs) {
        if (!outputs.contains(port)) {
            addIssue(issues,
                     QStringLiteral("error"),
                     QStringLiteral("display_primary_output_missing"),
                     QStringLiteral("display_descriptor.primary_outputs 引用了不存在的输出端口: ") + port,
                     op.path);
        }
    }
}

void requireSchemaPrefix(const QJsonObject& obj,
                         const QString& expected,
                         const QString& path,
                         QVector<PdkReadIssue>& issues) {
    const QString schema = obj.value(QStringLiteral("schema_version")).toString();
    if (schema.isEmpty()) {
        addIssue(issues,
                 QStringLiteral("warning"),
                 QStringLiteral("schema_version_missing"),
                 QStringLiteral("缺少 schema_version，UI 将按兼容模式读取"),
                 path);
        return;
    }
    if (schema != expected) {
        addIssue(issues,
                 QStringLiteral("error"),
                 QStringLiteral("schema_version_incompatible"),
                 QStringLiteral("schema_version 不兼容: ") + schema + QStringLiteral(" != ") + expected,
                 path);
    }
}

QString firstExistingFile(const QString& dir, const QStringList& names) {
    for (const QString& name : names) {
        const QString path = QDir(dir).filePath(name);
        if (QFileInfo::exists(path)) {
            return path;
        }
    }
    return QDir(dir).filePath(names.isEmpty() ? QString() : names.front());
}

qint64 shapeNodeCount(const QStringList& shape) {
    qint64 product = 1;
    bool hasDimension = false;
    for (const QString& item : shape) {
        bool ok = false;
        const qint64 dim = item.toLongLong(&ok);
        if (!ok || dim <= 0) {
            continue;
        }
        hasDimension = true;
        product *= dim;
    }
    return hasDimension ? product : 0;
}

QString inferFieldName(const QJsonObject& entry) {
    const QString explicitField = entry.value(QStringLiteral("field_name")).toString();
    if (!explicitField.isEmpty()) {
        return explicitField;
    }
    const QString contract = entry.value(QStringLiteral("contract_id")).toString();
    if (!contract.isEmpty()) {
        return contract;
    }
    return entry.value(QStringLiteral("port_id")).toString();
}

} // namespace

bool hasPdkReadErrors(const QVector<PdkReadIssue>& issues) {
    return std::any_of(issues.begin(), issues.end(), [](const PdkReadIssue& issue) {
        return issue.severity == QStringLiteral("error");
    });
}

int PdkDataPlaneView::artifactRefCount() const {
    return std::count_if(entries.begin(), entries.end(), [](const PdkDataPlaneEntryView& entry) {
        return entry.representation == QStringLiteral("artifact_ref");
    });
}

int PdkDataPlaneView::inlineJsonCount() const {
    return std::count_if(entries.begin(), entries.end(), [](const PdkDataPlaneEntryView& entry) {
        return entry.representation == QStringLiteral("inline_json");
    });
}

PdkObjectPackageView PdkObjectPackageReader::read(const QString& objectPackageRoot) const {
    PdkObjectPackageView view;
    if (objectPackageRoot.trimmed().isEmpty()) {
        addIssue(view.issues,
                 QStringLiteral("error"),
                 QStringLiteral("object_package_not_selected"),
                 QStringLiteral("尚未载入对象包"),
                 QString());
        return view;
    }
    view.rootPath = QDir::toNativeSeparators(QFileInfo(objectPackageRoot).absoluteFilePath());

    const QDir root(objectPackageRoot);
    if (!root.exists()) {
        addIssue(view.issues,
                 QStringLiteral("error"),
                 QStringLiteral("object_package_missing"),
                 QStringLiteral("对象包目录不存在"),
                 objectPackageRoot);
        return view;
    }

    const QString twinPath = root.filePath(QStringLiteral("object/twin_object.json"));
    const QString resourcesPath = root.filePath(QStringLiteral("assets/resources.json"));
    view.twinObjectJson = readJsonObject(
        twinPath, view.issues, true, QStringLiteral("twin_object_missing"));
    view.resourcesJson = readJsonObject(
        resourcesPath, view.issues, true, QStringLiteral("resources_missing"));
    view.objectId = view.twinObjectJson.value(QStringLiteral("object_id")).toString();
    if (view.objectId.isEmpty()) {
        view.objectId = view.resourcesJson.value(QStringLiteral("object_id")).toString();
    }
    const QString runtimeProfileRef =
        view.twinObjectJson.value(QStringLiteral("platform_runtime_profile")).toString().trimmed();
    if (!runtimeProfileRef.isEmpty()) {
        const QString runtimeProfilePath =
            QFileInfo(root.filePath(QStringLiteral("object/twin_object.json")))
                .dir()
                .filePath(runtimeProfileRef);
        view.runtimeProfileJson = readJsonObject(
            runtimeProfilePath, view.issues, false, QStringLiteral("runtime_profile_missing"));
    }

    for (const QJsonValue& value : view.resourcesJson.value(QStringLiteral("asset_groups")).toArray()) {
        const QJsonObject obj = value.toObject();
        PdkAssetGroupView group;
        group.groupId = obj.value(QStringLiteral("group_id")).toString();
        group.resourceIds = readStringList(obj.value(QStringLiteral("resources")));
        view.assetGroups.push_back(std::move(group));
    }

    QSet<QString> operatorIds;
    const QDir operatorDir(root.filePath(QStringLiteral("operators")));
    if (!operatorDir.exists()) {
        addIssue(view.issues,
                 QStringLiteral("error"),
                 QStringLiteral("operators_dir_missing"),
                 QStringLiteral("operators 目录不存在"),
                 operatorDir.absolutePath());
    } else {
        const QFileInfoList files = operatorDir.entryInfoList(
            {QStringLiteral("*.atomic.json")}, QDir::Files, QDir::Name);
        for (const QFileInfo& file : files) {
            QVector<PdkReadIssue> localIssues;
            const QJsonObject obj = readJsonObject(
                file.absoluteFilePath(), localIssues, true, QStringLiteral("operator_missing"));
            view.issues += localIssues;
            if (obj.isEmpty()) {
                continue;
            }

            PdkOperatorView op;
            op.path = QDir::toNativeSeparators(file.absoluteFilePath());
            op.rawJson = obj;
            op.operatorId = obj.value(QStringLiteral("operator_id")).toString();
            op.operatorKind = obj.value(QStringLiteral("operator_kind")).toString();
            op.operatorFamily = obj.value(QStringLiteral("operator_family")).toString();
            op.phases = readStringList(obj.value(QStringLiteral("phases")));
            op.resourceRefs = readStringList(obj.value(QStringLiteral("resource_refs")));
            const QJsonObject execution = obj.value(QStringLiteral("execution")).toObject();
            op.executionKind = execution.value(QStringLiteral("kind")).toString();
            op.adapterId = execution.value(QStringLiteral("adapter_id")).toString();
            op.inputs = readPorts(obj.value(QStringLiteral("inputs")).toArray());
            op.outputs = readPorts(obj.value(QStringLiteral("outputs")).toArray());
            op.display = readDisplay(obj);

            validateDisplayPorts(op, view.issues);
            if (!op.operatorId.isEmpty()) {
                operatorIds.insert(op.operatorId);
            }
            view.operators.push_back(std::move(op));
        }
    }

    const QDir workflowDir(root.filePath(QStringLiteral("workflows")));
    if (!workflowDir.exists()) {
        addIssue(view.issues,
                 QStringLiteral("error"),
                 QStringLiteral("workflows_dir_missing"),
                 QStringLiteral("workflows 目录不存在"),
                 workflowDir.absolutePath());
    } else {
        const QFileInfoList files =
            workflowDir.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
        for (const QFileInfo& file : files) {
            const QJsonObject obj = readJsonObject(
                file.absoluteFilePath(), view.issues, true, QStringLiteral("workflow_missing"));
            if (obj.isEmpty()) {
                continue;
            }

            PdkWorkflowView workflow;
            workflow.path = QDir::toNativeSeparators(file.absoluteFilePath());
            workflow.rawJson = obj;
            workflow.workflowId = obj.value(QStringLiteral("workflow_id")).toString();
            workflow.objectId = obj.value(QStringLiteral("object_id")).toString();
            workflow.phase = obj.value(QStringLiteral("phase")).toString();

            for (const QJsonValue& phaseValue : obj.value(QStringLiteral("phases")).toArray()) {
                const QJsonObject phase = phaseValue.toObject();
                const QString phaseId = phase.value(QStringLiteral("phase_id")).toString();
                for (const QJsonValue& stageValue : phase.value(QStringLiteral("stages")).toArray()) {
                    const QJsonObject stage = stageValue.toObject();
                    const QString stageId = stage.value(QStringLiteral("stage_id")).toString();
                    const QJsonObject subgraph = stage.value(QStringLiteral("subgraph")).toObject();
                    for (const QJsonValue& nodeValue : subgraph.value(QStringLiteral("nodes")).toArray()) {
                        const QJsonObject node = nodeValue.toObject();
                        PdkWorkflowNodeView workflowNode;
                        workflowNode.phaseId = phaseId;
                        workflowNode.stageId = stageId;
                        workflowNode.nodeId = node.value(QStringLiteral("node_id")).toString();
                        workflowNode.operatorRef =
                            node.value(QStringLiteral("operator_ref")).toString();
                        if (!workflowNode.operatorRef.isEmpty() &&
                            !operatorIds.contains(workflowNode.operatorRef)) {
                            addIssue(view.issues,
                                     QStringLiteral("error"),
                                     QStringLiteral("workflow_operator_missing"),
                                     QStringLiteral("workflow 引用了不存在的 operator: ") +
                                         workflowNode.operatorRef,
                                     workflow.path);
                        }
                        workflow.nodes.push_back(std::move(workflowNode));
                    }
                }
            }
            view.workflows.push_back(std::move(workflow));
        }
    }

    return view;
}

PdkCompiledWorkflowView PdkCompiledWorkflowReader::read(const QString& compiledWorkflowDir) const {
    PdkCompiledWorkflowView view;
    view.compiledDir = QDir::toNativeSeparators(QFileInfo(compiledWorkflowDir).absoluteFilePath());

    const QDir dir(compiledWorkflowDir);
    if (!dir.exists()) {
        addIssue(view.issues,
                 QStringLiteral("error"),
                 QStringLiteral("compiled_workflow_missing"),
                 QStringLiteral("compiled workflow 目录不存在"),
                 compiledWorkflowDir);
        return view;
    }

    const QString executionPath = dir.filePath(QStringLiteral("execution_plan.json"));
    const QString timePath = dir.filePath(QStringLiteral("time_plan.json"));
    const QString schedulerPath = dir.filePath(QStringLiteral("scheduler_plan.json"));
    const QString dataPlanePath = firstExistingFile(
        compiledWorkflowDir,
        {QStringLiteral("data_plane_manifest.json"), QStringLiteral("data_plane_plan.json")});
    const QString operatorSnapshotPath = dir.filePath(QStringLiteral("operator_snapshot.json"));
    const QString activationSnapshotPath = dir.filePath(QStringLiteral("activation_snapshot.json"));

    view.executionPlanJson = readJsonObject(
        executionPath, view.issues, true, QStringLiteral("execution_plan_missing"));
    view.timePlanJson =
        readJsonObject(timePath, view.issues, true, QStringLiteral("time_plan_missing"));
    view.schedulerPlanJson = readJsonObject(
        schedulerPath, view.issues, true, QStringLiteral("scheduler_plan_missing"));
    view.dataPlanePlanJson = readJsonObject(
        dataPlanePath, view.issues, false, QStringLiteral("data_plane_plan_missing"));
    view.operatorSnapshotJson = readJsonObject(
        operatorSnapshotPath, view.issues, false, QStringLiteral("operator_snapshot_missing"));
    view.activationSnapshotJson = readJsonObject(
        activationSnapshotPath, view.issues, false, QStringLiteral("activation_snapshot_missing"));

    requireSchemaPrefix(view.executionPlanJson,
                        QStringLiteral("flightenv.platform.execution_plan.v1"),
                        executionPath,
                        view.issues);
    requireSchemaPrefix(
        view.timePlanJson, QStringLiteral("flightenv.platform.time_plan.v1"), timePath, view.issues);
    requireSchemaPrefix(view.schedulerPlanJson,
                        QStringLiteral("flightenv.platform.scheduler_plan.v1"),
                        schedulerPath,
                        view.issues);
    if (!view.activationSnapshotJson.isEmpty()) {
        requireSchemaPrefix(view.activationSnapshotJson,
                            QStringLiteral("flightenv.platform.activation_snapshot.v1"),
                            activationSnapshotPath,
                            view.issues);
    }

    view.workflowId = view.executionPlanJson.value(QStringLiteral("workflow_id")).toString();
    view.objectId = view.executionPlanJson.value(QStringLiteral("object_id")).toString();
    view.phase = view.executionPlanJson.value(QStringLiteral("phase")).toString();
    view.runId = view.executionPlanJson.value(QStringLiteral("run_id")).toString();

    for (const QJsonValue& value : view.executionPlanJson.value(QStringLiteral("nodes")).toArray()) {
        const QJsonObject obj = value.toObject();
        PdkPlanNodeView node;
        node.nodeId = obj.value(QStringLiteral("node_id")).toString();
        node.operatorId = obj.value(QStringLiteral("operator_id")).toString();
        node.phaseId = obj.value(QStringLiteral("phase_id")).toString();
        node.stageId = obj.value(QStringLiteral("stage_id")).toString();
        node.executionKind = obj.value(QStringLiteral("execution_kind")).toString();
        node.adapterId = obj.value(QStringLiteral("adapter_id")).toString();
        node.dependsOn = readStringList(obj.value(QStringLiteral("depends_on")));
        node.display = readDisplay(obj);
        node.timePolicy = obj.value(QStringLiteral("time_policy")).toObject();
        node.schedulerPolicy = obj.value(QStringLiteral("scheduler_policy")).toObject();
        if (node.display.rendererId.isEmpty()) {
            addIssue(view.issues,
                     QStringLiteral("error"),
                     QStringLiteral("plan_node_display_missing"),
                     QStringLiteral("execution_plan node 缺少 display_descriptor"),
                     executionPath);
        }
        view.nodes.push_back(std::move(node));
    }

    for (const QJsonValue& value :
         view.activationSnapshotJson.value(QStringLiteral("nodes")).toArray()) {
        const QJsonObject obj = value.toObject();
        PdkActivationNodeView node;
        node.compiledNodeId = obj.value(QStringLiteral("compiled_node_id")).toString();
        node.phaseId = obj.value(QStringLiteral("phase_id")).toString();
        node.stageId = obj.value(QStringLiteral("stage_id")).toString();
        node.nodeId = obj.value(QStringLiteral("node_id")).toString();
        node.operatorRef = obj.value(QStringLiteral("operator_ref")).toString();
        node.feature = obj.value(QStringLiteral("feature")).toString();
        node.status = obj.value(QStringLiteral("status")).toString();
        node.reason = obj.value(QStringLiteral("reason")).toString();
        node.required = obj.value(QStringLiteral("required")).toBool(false);
        node.enabledByPolicy = obj.value(QStringLiteral("enabled_by_policy")).toBool(true);
        node.enabledByProfile = obj.value(QStringLiteral("enabled_by_profile")).toBool(true);
        view.activationNodes.push_back(std::move(node));
    }

    return view;
}

PdkDataPlaneView PdkDataPlaneReader::read(const QString& manifestOrRunDir) const {
    PdkDataPlaneView view;
    QFileInfo info(manifestOrRunDir);
    QString manifestPath = manifestOrRunDir;
    if (info.exists() && info.isDir()) {
        manifestPath = firstExistingFile(
            manifestOrRunDir,
            {QStringLiteral("data_plane_manifest.json"),
             QStringLiteral("data_plane_plan.json"),
             QStringLiteral("run_timeline_index.json")});
    }
    view.path = QDir::toNativeSeparators(QFileInfo(manifestPath).absoluteFilePath());

    const QJsonObject json = readJsonObject(
        manifestPath, view.issues, true, QStringLiteral("data_plane_manifest_missing"));
    if (json.isEmpty()) {
        return view;
    }
    const QString schema = json.value(QStringLiteral("schema_version")).toString();
    if (schema != QStringLiteral("flightenv.platform.data_plane_manifest.v1") &&
        schema != QStringLiteral("flightenv.platform.data_plane_plan.v1") &&
        schema != QStringLiteral("flightenv.platform.run_timeline_index.v1")) {
        addIssue(view.issues,
                 QStringLiteral("error"),
                 QStringLiteral("schema_version_incompatible"),
                 QStringLiteral("不支持的 DataPlane schema_version: ") + schema,
                 manifestPath);
    }

    view.runId = json.value(QStringLiteral("run_id")).toString();
    view.workflowId = json.value(QStringLiteral("workflow_id")).toString();
    view.objectId = json.value(QStringLiteral("object_id")).toString();
    QJsonArray entries = json.value(QStringLiteral("entries")).toArray();
    if (entries.isEmpty() && schema == QStringLiteral("flightenv.platform.run_timeline_index.v1")) {
        entries = json.value(QStringLiteral("artifact_refs")).toArray();
        for (const QJsonValue& value : json.value(QStringLiteral("qoi_refs")).toArray()) {
            entries.append(value);
        }
    }
    const QJsonArray nodes = json.value(QStringLiteral("nodes")).toArray();

    auto appendEntry = [&](const QJsonObject& entry) {
        PdkDataPlaneEntryView out;
        out.branchId = entry.value(QStringLiteral("branch_id")).toString();
        out.sourceRunDir = QDir::fromNativeSeparators(entry.value(QStringLiteral("source_run_dir")).toString());
        out.nodeId = entry.value(QStringLiteral("node_id")).toString();
        out.operatorId = entry.value(QStringLiteral("operator_id")).toString();
        out.direction = entry.value(QStringLiteral("direction")).toString();
        out.portId = entry.value(QStringLiteral("port_id")).toString();
        out.contractId = entry.value(QStringLiteral("contract_id")).toString();
        out.representation = entry.value(QStringLiteral("representation")).toString();
        out.ref = entry.value(QStringLiteral("ref")).toString();
        out.artifactUri = entry.value(QStringLiteral("artifact_uri")).toString(out.ref);
        out.layoutRef = entry.value(QStringLiteral("layout_ref")).toString();
        out.checksum = entry.value(QStringLiteral("checksum")).toString();
        out.evidenceRef = entry.value(QStringLiteral("evidence_ref")).toString();
        out.fieldName = inferFieldName(entry);
        out.componentId = entry.value(QStringLiteral("component_id")).toString();
        out.meshRef = entry.value(QStringLiteral("mesh_ref")).toString();
        out.unit = entry.value(QStringLiteral("unit")).toString();
        out.timePoint = entry.value(QStringLiteral("time_point")).toObject();
        out.statistics = entry.value(QStringLiteral("statistics")).toObject();
        out.shape = readStringList(entry.value(QStringLiteral("shape")));
        if (out.shape.isEmpty()) {
            for (const QJsonValue& dim : entry.value(QStringLiteral("shape")).toArray()) {
                if (dim.isDouble()) {
                    out.shape.push_back(QString::number(dim.toInt()));
                }
            }
        }
        out.nodeCount = entry.value(QStringLiteral("node_count")).toInteger(shapeNodeCount(out.shape));
        out.inlineByteSize = entry.value(QStringLiteral("inline_byte_size")).toInteger(0);
        out.mainlineFrameIndex = entry.value(QStringLiteral("mainline_frame_index")).toInt(-1);
        out.stepIndex = entry.value(QStringLiteral("step_index")).toInt(-1);
        out.loopIterationIndex = entry.value(QStringLiteral("loop_iteration_index")).toInt(-1);
        if (out.representation == QStringLiteral("artifact_ref") && out.artifactUri.isEmpty()) {
            addIssue(view.issues,
                     QStringLiteral("error"),
                     QStringLiteral("artifact_ref_missing"),
                     QStringLiteral("artifact_ref 缺少 ref/artifact_uri"),
                     manifestPath);
        }
        view.entries.push_back(std::move(out));
    };

    for (const QJsonValue& value : entries) {
        appendEntry(value.toObject());
    }
    for (const QJsonValue& nodeValue : nodes) {
        const QJsonObject node = nodeValue.toObject();
        for (const QJsonValue& value : node.value(QStringLiteral("inputs")).toArray()) {
            appendEntry(value.toObject());
        }
        for (const QJsonValue& value : node.value(QStringLiteral("outputs")).toArray()) {
            appendEntry(value.toObject());
        }
    }

    return view;
}

PdkHealthTrendView PdkHealthTrendReader::read(const QString& healthTrendPathOrRunDir) const {
    PdkHealthTrendView view;
    QFileInfo info(healthTrendPathOrRunDir);
    const QString path = info.exists() && info.isDir()
        ? QDir(healthTrendPathOrRunDir).filePath(QStringLiteral("health_trend_summary.json"))
        : healthTrendPathOrRunDir;
    view.path = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());

    const QJsonObject json = readJsonObject(
        path, view.issues, true, QStringLiteral("health_trend_missing"));
    if (json.isEmpty()) {
        return view;
    }
    requireSchemaPrefix(json,
                        QStringLiteral("flightenv.platform.health_trend_summary.v1"),
                        path,
                        view.issues);

    view.runId = json.value(QStringLiteral("run_id")).toString();
    view.triggerFrameIndex = json.value(QStringLiteral("trigger_frame_index")).toInt(-1);
    view.iterationCount = json.value(QStringLiteral("iteration_count")).toInt(0);
    view.stopReason = json.value(QStringLiteral("stop_reason")).toString();
    view.damageNonDecreasing = json.value(QStringLiteral("damage_non_decreasing")).toBool(false);
    view.ablationNonDecreasing = json.value(QStringLiteral("ablation_non_decreasing")).toBool(false);
    view.rulNonIncreasing = json.value(QStringLiteral("rul_non_increasing")).toBool(false);
    view.firstStep = json.value(QStringLiteral("first_step")).toObject();
    view.lastStep = json.value(QStringLiteral("last_step")).toObject();
    for (const QJsonValue& value : json.value(QStringLiteral("trend")).toArray()) {
        view.trend.push_back(value.toObject());
    }
    return view;
}

PdkFieldArtifactView PdkFieldArtifactReader::read(const QString& artifactPath) const {
    PdkFieldArtifactView view;
    view.path = QDir::toNativeSeparators(QFileInfo(artifactPath).absoluteFilePath());

    const QJsonObject json = readJsonObject(
        artifactPath, view.issues, true, QStringLiteral("field_artifact_missing"));
    if (json.isEmpty()) {
        return view;
    }

    view.fieldName = json.value(QStringLiteral("field_name")).toString();
    view.contractId = json.value(QStringLiteral("contract_id")).toString();
    view.layoutRef = json.value(QStringLiteral("layout_ref")).toString();
    view.unit = json.value(QStringLiteral("unit")).toString();
    view.componentId = json.value(QStringLiteral("component_id")).toString();
    view.meshRef = json.value(QStringLiteral("mesh_ref")).toString();
    if (view.meshRef.isEmpty() && view.layoutRef.startsWith(QStringLiteral("mesh."))) {
        view.meshRef = view.layoutRef;
    }
    view.shape = readStringList(json.value(QStringLiteral("shape")));
    if (view.shape.isEmpty()) {
        for (const QJsonValue& dim : json.value(QStringLiteral("shape")).toArray()) {
            if (dim.isDouble()) {
                view.shape.push_back(QString::number(dim.toInt()));
            }
        }
    }
    view.nodeCount = json.value(QStringLiteral("node_count")).toInteger(shapeNodeCount(view.shape));
    view.statistics = json.value(QStringLiteral("statistics")).toObject();

    const QJsonArray values = json.value(QStringLiteral("values")).toArray();
    view.values.reserve(values.size());
    for (const QJsonValue& value : values) {
        if (value.isDouble()) {
            view.values.push_back(value.toDouble());
        }
    }
    if (view.nodeCount > 0 && view.values.size() != view.nodeCount) {
        addIssue(view.issues,
                 QStringLiteral("error"),
                 QStringLiteral("field_artifact_node_count_mismatch"),
                 QStringLiteral("Field artifact values 数量与 node_count 不一致"),
                 artifactPath);
    }
    return view;
}

PdkRuntimeEvidenceView PdkRuntimeEvidenceReader::read(const QString& runDir) const {
    PdkRuntimeEvidenceView view;
    view.runDir = QDir::toNativeSeparators(QFileInfo(runDir).absoluteFilePath());
    const QDir dir(runDir);
    if (!dir.exists()) {
        addIssue(view.issues,
                 QStringLiteral("error"),
                 QStringLiteral("runtime_run_missing"),
                 QStringLiteral("runtime run 目录不存在"),
                 runDir);
        return view;
    }

    view.runtimeNodeSnapshotJson = readJsonObject(
        dir.filePath(QStringLiteral("runtime_node_snapshot.json")),
        view.issues,
        false,
        QStringLiteral("runtime_node_snapshot_missing"));
    view.sensorStreamJson = readJsonObject(
        dir.filePath(QStringLiteral("sensor_stream.json")),
        view.issues,
        false,
        QStringLiteral("sensor_stream_missing"));
    view.runtimeOutputsJson = readJsonObject(
        dir.filePath(QStringLiteral("runtime_outputs.json")),
        view.issues,
        false,
        QStringLiteral("runtime_outputs_missing"));
    view.dataPlane = PdkDataPlaneReader().read(runDir);
    view.healthTrend = PdkHealthTrendReader().read(runDir);

    if (!view.runtimeNodeSnapshotJson.isEmpty()) {
        view.runId = view.runtimeNodeSnapshotJson.value(QStringLiteral("run_id")).toString();
        view.workflowId = view.runtimeNodeSnapshotJson.value(QStringLiteral("workflow_id")).toString();
        view.objectId = view.runtimeNodeSnapshotJson.value(QStringLiteral("object_id")).toString();
    }
    if (view.runId.isEmpty() && !view.sensorStreamJson.isEmpty()) {
        view.runId = view.sensorStreamJson.value(QStringLiteral("run_id")).toString();
        view.workflowId = view.sensorStreamJson.value(QStringLiteral("workflow_id")).toString();
        view.objectId = view.sensorStreamJson.value(QStringLiteral("object_id")).toString();
    }

    int minCount = 0;
    int maxCount = 0;
    for (const QJsonValue& value : view.sensorStreamJson.value(QStringLiteral("frames")).toArray()) {
        const int count = value.toObject().value(QStringLiteral("sensor_count")).toInt(0);
        if (view.sensorFrameCount == 0) {
            minCount = count;
            maxCount = count;
        } else {
            minCount = std::min(minCount, count);
            maxCount = std::max(maxCount, count);
        }
        ++view.sensorFrameCount;
    }
    view.sensorCountMin = minCount;
    view.sensorCountMax = maxCount;

    view.issues += view.dataPlane.issues;
    for (const PdkReadIssue& issue : view.healthTrend.issues) {
        if (issue.code != QStringLiteral("health_trend_missing")) {
            view.issues.push_back(issue);
        }
    }
    return view;
}

} // namespace twin
