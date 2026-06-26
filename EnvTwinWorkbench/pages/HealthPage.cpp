#include "HealthPage.h"

#include "../datahub/AsyncFieldArtifactLoader.h"
#include "../datahub/FieldRenderGuard.h"
#include "../datahub/PdkUiReaders.h"
#include "../theme/Palette.h"
#include "../widgets/GraphWorkflowDisplayWidgets.h"
#include "../widgets/KvList.h"
#include "../widgets/Meter.h"
#include "../widgets/Panel.h"
#include "../widgets/PageHeader.h"
#include "../widgets/StatusUtil.h"
#include "../widgets/VtkModelFieldWidget.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace twin {

using flightenv::ui::demo::VtkModelFieldWidget;
using flightenv::ui::display::ScalarTrendWidget;

namespace {

QJsonObject readJsonObject(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

QString predictionRunDirFromEvidenceRoot(const QString& evidenceRoot) {
    const QDir root(evidenceRoot);
    const QJsonObject summary = readJsonObject(root.filePath(QStringLiteral("mainline_summary.json")));
    const QJsonArray runs = summary.value(QStringLiteral("prediction")).toObject()
                               .value(QStringLiteral("runs")).toArray();
    if (!runs.isEmpty()) {
        return QDir::fromNativeSeparators(runs.last().toObject().value(QStringLiteral("run_dir")).toString());
    }
    if (QFileInfo::exists(root.filePath(QStringLiteral("health_trend_summary.json")))) {
        return evidenceRoot;
    }
    return {};
}

QString runtimeSnapshotPathFor(const QString& evidenceRoot, const QString& predictionRunDir) {
    const QStringList candidates = {
        QDir(predictionRunDir).filePath(QStringLiteral("runtime_snapshot.json")),
        QDir(evidenceRoot).filePath(QStringLiteral("runtime_snapshot.json"))
    };
    for (const QString& path : candidates) {
        if (QFileInfo::exists(path)) {
            return path;
        }
    }
    return {};
}

QString resolveArtifactPath(const QString& runDir, const PdkDataPlaneEntryView& entry) {
    QString path = entry.artifactUri.isEmpty() ? entry.ref : entry.artifactUri;
    path = QDir::fromNativeSeparators(path);
    if (path.startsWith(QStringLiteral("file://"))) {
        path = path.mid(QStringLiteral("file://").size());
    }
    if (QDir::isAbsolutePath(path)) {
        return path;
    }
    return QDir(runDir).filePath(path);
}

QStringList stringListFromJsonArray(const QJsonArray& values) {
    QStringList out;
    for (const QJsonValue& value : values) {
        const QString text = value.toString().trimmed();
        if (!text.isEmpty()) {
            out.push_back(text);
        }
    }
    return out;
}

QStringList metricKeysFor(const QJsonObject& runtimeProfile,
                          const QString& metricName) {
    const QJsonObject healthLedger = runtimeProfile.value(QStringLiteral("health_ledger")).toObject();
    const QJsonObject metricKeys = healthLedger.value(QStringLiteral("metric_keys")).toObject();
    return stringListFromJsonArray(metricKeys.value(metricName).toArray());
}

double metricValue(const QJsonObject& value, const QStringList& keys, double fallback) {
    for (const QString& key : keys) {
        const QJsonValue candidate = value.value(key);
        if (candidate.isDouble()) {
            return candidate.toDouble();
        }
    }
    return fallback;
}

struct HealthDisplayOutputRef {
    QString role;
    QString rendererId;
    QString displayTitle;
    QString phaseId;
    QString stageId;
    QString nodeId;
    QString operatorRef;
    QString portId;
    QString contractId;
    QString expectedValueKind;
    int priority = 0;
};

bool isHealthDisplayRole(const QString& role) {
    return role == QStringLiteral("health.default_field") ||
           role.startsWith(QStringLiteral("health."));
}

QString outputContractId(const PdkOperatorView& op, const QString& portId) {
    for (const PdkPortView& port : op.outputs) {
        if (port.portId == portId) {
            return port.contractId;
        }
    }
    return {};
}

HealthDisplayOutputRef readDisplayOutputRef(const QJsonObject& obj) {
    HealthDisplayOutputRef out;
    out.role = obj.value(QStringLiteral("role")).toString();
    out.rendererId = obj.value(QStringLiteral("renderer_id")).toString();
    out.displayTitle = obj.value(QStringLiteral("display_title")).toString();
    out.phaseId = obj.value(QStringLiteral("phase_id")).toString();
    out.stageId = obj.value(QStringLiteral("stage_id")).toString();
    out.nodeId = obj.value(QStringLiteral("node_id")).toString();
    out.operatorRef = obj.value(QStringLiteral("operator_ref")).toString();
    out.portId = obj.value(QStringLiteral("port_id")).toString();
    out.contractId = obj.value(QStringLiteral("contract_id")).toString();
    out.expectedValueKind = obj.value(QStringLiteral("expected_value_kind")).toString();
    out.priority = obj.value(QStringLiteral("priority")).toInt(0);
    return out;
}

QVector<HealthDisplayOutputRef> fallbackHealthOutputsFromOperators(const PdkObjectPackageView& objectPackage) {
    QVector<HealthDisplayOutputRef> out;
    for (const PdkOperatorView& op : objectPackage.operators) {
        if (!op.display.displayRoles.contains(QStringLiteral("health.default_field")) &&
            !op.display.displayRoles.contains(QStringLiteral("health.secondary_field")) &&
            !op.display.displayRoles.contains(QStringLiteral("health.accumulated_state_field"))) {
            continue;
        }
        const QString portId = !op.display.valueRefPort.isEmpty()
            ? op.display.valueRefPort
            : (!op.display.artifactPorts.isEmpty() ? op.display.artifactPorts.front()
                                                   : (!op.display.primaryOutputs.isEmpty() ? op.display.primaryOutputs.front() : QString()));
        if (portId.isEmpty()) {
            continue;
        }
        HealthDisplayOutputRef ref;
        ref.role = op.display.displayRoles.contains(QStringLiteral("health.default_field"))
            ? QStringLiteral("health.default_field")
            : op.display.displayRoles.front();
        ref.rendererId = op.display.rendererId;
        ref.displayTitle = op.display.displayTitle;
        ref.operatorRef = op.operatorId;
        ref.portId = portId;
        ref.contractId = outputContractId(op, portId);
        ref.expectedValueKind = op.display.expectedValueKind;
        ref.priority = ref.role == QStringLiteral("health.default_field") ? 10 : 1;
        out.push_back(std::move(ref));
    }
    return out;
}

QVector<HealthDisplayOutputRef> healthDisplayOutputsForWorkflow(const PdkObjectPackageView& objectPackage,
                                                                const QString& workflowId) {
    QVector<HealthDisplayOutputRef> out;
    auto appendWorkflow = [&out](const PdkWorkflowView& workflow) {
        for (const QJsonValue& value : workflow.rawJson.value(QStringLiteral("display_outputs")).toArray()) {
            const HealthDisplayOutputRef ref = readDisplayOutputRef(value.toObject());
            if (isHealthDisplayRole(ref.role)) {
                out.push_back(ref);
            }
        }
    };

    for (const PdkWorkflowView& workflow : objectPackage.workflows) {
        if (!workflowId.isEmpty() && workflow.workflowId != workflowId) {
            continue;
        }
        appendWorkflow(workflow);
    }
    if (out.isEmpty() && !workflowId.isEmpty()) {
        for (const PdkWorkflowView& workflow : objectPackage.workflows) {
            if (workflow.phase == QStringLiteral("posterior_future_prediction")) {
                appendWorkflow(workflow);
            }
        }
    }
    if (out.isEmpty()) {
        out = fallbackHealthOutputsFromOperators(objectPackage);
    }
    std::stable_sort(out.begin(), out.end(), [](const HealthDisplayOutputRef& lhs, const HealthDisplayOutputRef& rhs) {
        if (lhs.role == QStringLiteral("health.default_field") && rhs.role != QStringLiteral("health.default_field")) {
            return true;
        }
        if (lhs.role != QStringLiteral("health.default_field") && rhs.role == QStringLiteral("health.default_field")) {
            return false;
        }
        return lhs.priority > rhs.priority;
    });
    return out;
}

bool nodeMatches(const QString& compiledNodeId, const HealthDisplayOutputRef& ref) {
    if (ref.nodeId.isEmpty()) {
        return true;
    }
    if (compiledNodeId == ref.nodeId || compiledNodeId.endsWith(QStringLiteral(".") + ref.nodeId)) {
        return true;
    }
    if (!ref.stageId.isEmpty() &&
        compiledNodeId.endsWith(QStringLiteral(".") + ref.stageId + QStringLiteral(".") + ref.nodeId)) {
        return true;
    }
    return false;
}

int scoreDataPlaneEntry(const PdkDataPlaneEntryView& entry,
                        const HealthDisplayOutputRef& ref,
                        int renderStep) {
    if (entry.representation != QStringLiteral("artifact_ref")) {
        return -1;
    }
    if (!entry.direction.isEmpty() && entry.direction != QStringLiteral("output")) {
        return -1;
    }
    if (!ref.portId.isEmpty() && entry.portId != ref.portId) {
        return -1;
    }
    if (!ref.contractId.isEmpty() && entry.contractId != ref.contractId) {
        return -1;
    }
    if (!ref.operatorRef.isEmpty() && entry.operatorId != ref.operatorRef) {
        return -1;
    }
    if (!nodeMatches(entry.nodeId, ref)) {
        return -1;
    }

    int score = ref.role == QStringLiteral("health.default_field") ? 1000 : 100;
    score += ref.priority;
    if (entry.loopIterationIndex == renderStep) {
        score += 50;
    } else if (entry.loopIterationIndex >= 0 && renderStep >= 0) {
        score -= std::abs(entry.loopIterationIndex - renderStep);
    }
    if (!entry.artifactUri.isEmpty() || !entry.ref.isEmpty()) {
        score += 5;
    }
    return score;
}

struct SelectedHealthField {
    bool ok = false;
    HealthDisplayOutputRef display;
    PdkDataPlaneEntryView entry;
};

SelectedHealthField selectHealthField(const PdkDataPlaneView& dataPlane,
                                      const QVector<HealthDisplayOutputRef>& refs,
                                      int renderStep) {
    SelectedHealthField best;
    int bestScore = -1;
    for (const HealthDisplayOutputRef& ref : refs) {
        for (const PdkDataPlaneEntryView& entry : dataPlane.entries) {
            const int score = scoreDataPlaneEntry(entry, ref, renderStep);
            if (score > bestScore) {
                bestScore = score;
                best.ok = true;
                best.display = ref;
                best.entry = entry;
            }
        }
    }
    return best;
}

} // namespace

HealthPage::HealthPage(QString objectPackageRoot, QString evidenceRoot, QWidget* parent)
    : QWidget(parent),
      objectPackage_(PdkObjectPackageReader().read(objectPackageRoot)),
      objectPackageRoot_(std::move(objectPackageRoot)),
      evidenceRoot_(std::move(evidenceRoot)) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);
    root->addWidget(makePageHeader(
        QStringLiteral("健康账本"),
        QStringLiteral("对象/区域的累计损伤、剩余寿命与首超破坏时间，可追溯到 run / 模型 / 证据"), this));

    auto* cols = new QHBoxLayout();
    cols->setSpacing(12);

    auto* regionPanel = new Panel(QStringLiteral("对象 / 区域"), this);
    regionPanel->setMinimumWidth(220);
    regionPanel->setMaximumWidth(300);
    regionPanel->setSubtitle(QStringLiteral("累计损伤 0-1"));
    struct RegionRow {
        std::string material_ref;
        std::string name;
        std::string object_id;
    };
    std::vector<RegionRow> objectPackageRegions;
    if (objectPackage_.ok()) {
        // 只把物理组件当作损伤区域；database/criteria 等资源组不是可损伤的区域，列出来只是噪声。
        for (const QJsonValue& value : objectPackage_.twinObjectJson.value(QStringLiteral("components")).toArray()) {
            const QJsonObject component = value.toObject();
            const QString componentId = component.value(QStringLiteral("component_id")).toString();
            if (!componentId.isEmpty()) {
                objectPackageRegions.push_back(RegionRow{
                    "component",
                    component.value(QStringLiteral("display_name")).toString(componentId).toStdString(),
                    componentId.toStdString()});
            }
        }
    }
    int regionCount = 0;
    for (const auto& o : objectPackageRegions) {
        if (o.material_ref.empty()) {
            continue;
        }
        auto* row = new QWidget(regionPanel->body());
        auto* rl = new QVBoxLayout(row);
        rl->setContentsMargins(2, 4, 2, 4);
        rl->setSpacing(3);
        auto* name = new QLabel(QString::fromStdString(o.name.empty() ? o.object_id : o.name), row);
        name->setStyleSheet(QStringLiteral("font-weight:700;font-size:12px;color:#1b1f27;"));
        rl->addWidget(name);
        auto* meter = new Meter(row);
        meter->setValue(0.0);
        meter->setColor(palette::ok());
        rl->addWidget(meter);
        auto* sub = new QLabel(QString::fromStdString(o.object_id), row);
        sub->setProperty("mono", true);
        sub->setProperty("muted", true);
        sub->setStyleSheet(QStringLiteral("font-size:10px;"));
        rl->addWidget(sub);
        regionPanel->bodyLayout()->addWidget(row);
        ++regionCount;
    }
    if (regionCount == 0) {
        auto* empty = new QLabel(QStringLiteral("对象包暂无可显示的组件或资源组"), regionPanel->body());
        empty->setProperty("muted", true);
        empty->setWordWrap(true);
        regionPanel->bodyLayout()->addWidget(empty);
    }
    regionPanel->bodyLayout()->addStretch(1);
    cols->addWidget(regionPanel);

    auto* centerCol = new QVBoxLayout();
    centerCol->setSpacing(12);

    auto* fieldPanel = new Panel(QStringLiteral("寿命 / 损伤场"), this);
    fieldPanel->setSubtitle(QStringLiteral("FieldTensor artifact / 真实网格"));
    fieldWidget_ = new VtkModelFieldWidget(fieldPanel->body());
    fieldWidget_->setMinimumHeight(260);
    fieldWidget_->clearField(QStringLiteral("寿命场需要运行 evidence：健康预测后可视化"));
    fieldPanel->bodyLayout()->addWidget(fieldWidget_, 1);
    centerCol->addWidget(fieldPanel, 2);

    auto* trendPanel = new Panel(QStringLiteral("损伤随未来步累计"), this);
    trendPanel->setSubtitle(QStringLiteral("damage.forecast.steps"));
    damageTrend_ = new ScalarTrendWidget(trendPanel->body());
    damageTrend_->setTitle(QStringLiteral("damage_index"), QString());
    damageTrend_->setFixedRange(0.0, 1.0);
    damageTrend_->setMinimumHeight(110);
    trendPanel->bodyLayout()->addWidget(damageTrend_);
    rulTrend_ = new ScalarTrendWidget(trendPanel->body());
    rulTrend_->setTitle(QStringLiteral("RUL"), QStringLiteral("s"));
    rulTrend_->setMinimumHeight(100);
    trendPanel->bodyLayout()->addWidget(rulTrend_);
    centerCol->addWidget(trendPanel, 1);
    cols->addLayout(centerCol, 1);

    auto* assessPanel = new Panel(QStringLiteral("当前评估"), this);
    assessPanel->setMinimumWidth(240);
    assessPanel->setMaximumWidth(320);
    assessValue_ = new QLabel(QStringLiteral("-"), assessPanel->body());
    assessValue_->setAlignment(Qt::AlignCenter);
    assessValue_->setStyleSheet(QStringLiteral("font-family:'JetBrains Mono';font-size:32px;font-weight:800;color:#0e8a9c;"));
    assessPanel->bodyLayout()->addWidget(assessValue_);
    auto* assessCap = new QLabel(QStringLiteral("累计损伤 (0-1)"), assessPanel->body());
    assessCap->setAlignment(Qt::AlignCenter);
    assessCap->setProperty("muted", true);
    assessCap->setProperty("tiny", true);
    assessPanel->bodyLayout()->addWidget(assessCap);
    assessKv_ = new KvList(assessPanel->body());
    assessPanel->bodyLayout()->addWidget(assessKv_);
    fieldTable_ = makeTable({QStringLiteral("字段"), QStringLiteral("节点"), QStringLiteral("min"), QStringLiteral("max")}, assessPanel->body());
    fieldTable_->setMinimumHeight(150);
    assessPanel->bodyLayout()->addWidget(fieldTable_);
    assessPanel->bodyLayout()->addStretch(1);
    cols->addWidget(assessPanel);

    root->addLayout(cols, 1);

    loadEvidenceAssessment();
}

void HealthPage::loadEvidenceAssessment() {
    assessKv_->clear();
    fieldTable_->setRowCount(0);
    if (evidenceRoot_.isEmpty()) {
        assessKv_->addRow(QStringLiteral("状态"), QStringLiteral("无 evidence"), false);
        return;
    }
    const QString predictionRunDir = predictionRunDirFromEvidenceRoot(evidenceRoot_);
    if (predictionRunDir.isEmpty()) {
        assessKv_->addRow(QStringLiteral("状态"), QStringLiteral("未找到 prediction run"), false);
        return;
    }

    const PdkHealthTrendView health = PdkHealthTrendReader().read(predictionRunDir);
    if (!health.ok() || health.trend.isEmpty()) {
        assessKv_->addRow(QStringLiteral("状态"), QStringLiteral("缺少 health_trend_summary.json"), false);
        return;
    }

    std::vector<double> damageTrend;
    std::vector<double> rulTrend;
    const QStringList damageKeys = metricKeysFor(
        objectPackage_.runtimeProfileJson,
        QStringLiteral("damage_max"));
    const QStringList rulKeys = metricKeysFor(
        objectPackage_.runtimeProfileJson,
        QStringLiteral("remaining_life"));
    double lastDamage = 0.0;
    double lastRul = -1.0;
    for (const QJsonObject& step : health.trend) {
        const double damage = std::min(1.0, std::max(0.0, metricValue(step, damageKeys, 0.0)));
        damageTrend.push_back(damage);
        lastDamage = damage;
        const double rul = metricValue(step, rulKeys, -1.0);
        if (rul >= 0.0) {
            rulTrend.push_back(rul);
            lastRul = rul;
        }
    }
    if (!damageTrend.empty()) {
        damageTrend_->setSamples(damageTrend);
        assessValue_->setText(QString::number(lastDamage, 'f', 2));
    }
    if (!rulTrend.empty()) {
        rulTrend_->setSamples(rulTrend);
    }

    assessKv_->addRow(QStringLiteral("prediction run"), QFileInfo(predictionRunDir).fileName());
    assessKv_->addRow(QStringLiteral("RUL 剩余寿命"), lastRul >= 0 ? QStringLiteral("%1 s").arg(lastRul, 0, 'f', 2) : QStringLiteral("unknown"));
    assessKv_->addRow(QStringLiteral("预测步数"), QString::number(health.iterationCount));
    assessKv_->addRow(QStringLiteral("停止原因"), health.stopReason.isEmpty() ? QStringLiteral("-") : health.stopReason, false);
    assessKv_->addRow(QStringLiteral("损伤单调"), health.damageNonDecreasing ? QStringLiteral("ok") : QStringLiteral("warn"), false);
    assessKv_->addRow(QStringLiteral("RUL 单调"), health.rulNonIncreasing ? QStringLiteral("ok") : QStringLiteral("warn"), false);

    const int renderStep = std::max(0, health.iterationCount - 1);
    const PdkDataPlaneView dataPlane = PdkDataPlaneReader().read(predictionRunDir);
    const QVector<HealthDisplayOutputRef> displayOutputs =
        healthDisplayOutputsForWorkflow(objectPackage_, dataPlane.workflowId);
    const SelectedHealthField selected = selectHealthField(dataPlane, displayOutputs, renderStep);
    if (displayOutputs.isEmpty()) {
        fieldWidget_->clearField(QStringLiteral("对象包 workflow/display_descriptor 未声明健康页默认展示输出"));
        assessKv_->addRow(QStringLiteral("显示角色"), QStringLiteral("missing"), false);
        return;
    }
    if (!selected.ok) {
        fieldWidget_->clearField(QStringLiteral("对象包 display_outputs 未匹配到 data_plane 中的真实场 artifact"));
        assessKv_->addRow(QStringLiteral("显示角色"), displayOutputs.front().role, false);
        assessKv_->addRow(QStringLiteral("输出端口"), displayOutputs.front().portId, false);
        return;
    }

    const QString artifactPath = resolveArtifactPath(predictionRunDir, selected.entry);
    if (artifactPath.isEmpty()) {
        fieldWidget_->clearField(QStringLiteral("未找到可渲染 field artifact"));
        return;
    }
    const QString snapshotPath = runtimeSnapshotPathFor(evidenceRoot_, predictionRunDir);
    assessKv_->addRow(QStringLiteral("显示角色"), selected.display.role, false);
    assessKv_->addRow(QStringLiteral("显示输出"), QStringLiteral("%1 / %2").arg(selected.entry.nodeId, selected.entry.portId), false);
    fieldWidget_->clearField(QStringLiteral("寿命/损伤场后台加载中，界面保持可操作..."));
    loadFieldArtifactAsync(
        this,
        artifactPath,
        objectPackageRoot_,
        snapshotPath,
        fieldRenderHintFromEntry(selected.entry),
        selected.display.displayTitle.isEmpty() ? QStringLiteral("step %1").arg(renderStep) : selected.display.displayTitle,
        [this, snapshotPath](LoadedFieldArtifact loaded) {
            if (!loaded.ok) {
                fieldWidget_->clearField(loaded.message.isEmpty() ? QStringLiteral("field artifact 读取失败") : loaded.message);
                assessKv_->addRow(QStringLiteral("mesh_layout_source"), snapshotPath.isEmpty() ? QStringLiteral("object package default") : QFileInfo(snapshotPath).fileName());
                return;
            }
            fieldWidget_->setMeshLayoutCatalog(loaded.meshCatalog);
            const int row = fieldTable_->rowCount();
            fieldTable_->insertRow(row);
            fieldTable_->setItem(row, 0, new QTableWidgetItem(loaded.fieldName));
            fieldTable_->setItem(row, 1, new QTableWidgetItem(QString::number(loaded.nodeCount)));
            fieldTable_->setItem(row, 2, new QTableWidgetItem(QString::number(loaded.statistics.value(QStringLiteral("min")).toDouble(), 'g', 4)));
            fieldTable_->setItem(row, 3, new QTableWidgetItem(QString::number(loaded.statistics.value(QStringLiteral("max")).toDouble(), 'g', 4)));
            const auto stats = fieldWidget_->renderFlattenedValues(
                loaded.values,
                loaded.layoutId,
                1,
                0,
                QStringLiteral("%1 / %2").arg(loaded.fieldName, loaded.title),
                loaded.unit);
            assessKv_->addRow(QStringLiteral("mesh_layout_source"), loaded.layoutSourcePath.isEmpty() ? QStringLiteral("object package default") : QFileInfo(loaded.layoutSourcePath).fileName());
            assessKv_->addRow(QStringLiteral("场/网格绑定"), loaded.bindingMessage, false);
            assessKv_->addRow(QStringLiteral("VTK 场"), stats.ok ? QStringLiteral("ok") : stats.message, false);
        });
}

} // namespace twin
