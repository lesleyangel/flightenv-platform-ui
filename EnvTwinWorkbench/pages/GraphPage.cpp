#include "GraphPage.h"

#include "../widgets/GraphWorkflowDisplayWidgets.h"
#include "../widgets/OperatorRenderer.h"
#include "../widgets/Panel.h"

#include <QAbstractItemView>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileInfoList>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QMap>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QVector>

#include <algorithm>
#include <utility>

namespace twin {

using flightenv::ui::display::WorkflowDagEdge;
using flightenv::ui::display::WorkflowDagNode;
using flightenv::ui::display::WorkflowDagWidget;

namespace {

QJsonObject readJsonObject(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

QString pathFromJson(const QJsonValue& value) {
    return QDir::fromNativeSeparators(value.toString());
}

const PdkOperatorView* findOperator(const PdkObjectPackageView& package, const QString& operatorId) {
    for (const PdkOperatorView& op : package.operators) {
        if (op.operatorId == operatorId) {
            return &op;
        }
    }
    return nullptr;
}

struct WorkflowPortEdgeView {
    QString fromStage;
    QString fromNode;
    QString fromPort;
    QString toStage;
    QString toNode;
    QString toPort;
    QString scope;
};

QString endpointLabel(const QString& stage, const QString& node) {
    if (stage.isEmpty()) {
        return node.isEmpty() ? QStringLiteral("—") : node;
    }
    if (node.isEmpty()) {
        return stage + QStringLiteral(".—");
    }
    return stage + QStringLiteral(".") + node;
}

QString portLabel(const QString& port) {
    return port.isEmpty() ? QStringLiteral("—") : port;
}

QVector<WorkflowPortEdgeView> collectWorkflowPortEdges(const PdkWorkflowView& workflow) {
    QVector<WorkflowPortEdgeView> out;
    for (const QJsonValue& pv : workflow.rawJson.value(QStringLiteral("phases")).toArray()) {
        const QJsonObject phase = pv.toObject();
        for (const QJsonValue& sv : phase.value(QStringLiteral("stages")).toArray()) {
            const QJsonObject stage = sv.toObject();
            const QString stageId = stage.value(QStringLiteral("stage_id")).toString();
            const QJsonObject subgraph = stage.value(QStringLiteral("subgraph")).toObject();
            for (const QJsonValue& ev : subgraph.value(QStringLiteral("edges")).toArray()) {
                const QJsonObject edge = ev.toObject();
                const QJsonObject from = edge.value(QStringLiteral("from")).toObject();
                const QJsonObject to = edge.value(QStringLiteral("to")).toObject();
                const QString fromNode = from.value(QStringLiteral("node_id")).toString();
                const QString toNode = to.value(QStringLiteral("node_id")).toString();
                if (fromNode.isEmpty() && toNode.isEmpty()) {
                    continue;
                }
                out.push_back(WorkflowPortEdgeView{
                    stageId,
                    fromNode,
                    from.value(QStringLiteral("port_id")).toString(),
                    stageId,
                    toNode,
                    to.value(QStringLiteral("port_id")).toString(),
                    QStringLiteral("stage 内")});
            }
        }
        for (const QJsonValue& ev : phase.value(QStringLiteral("stage_edges")).toArray()) {
            const QJsonObject edge = ev.toObject();
            const QJsonObject from = edge.value(QStringLiteral("from")).toObject();
            const QJsonObject to = edge.value(QStringLiteral("to")).toObject();
            const QString fromNode = from.value(QStringLiteral("node_id")).toString();
            const QString toNode = to.value(QStringLiteral("node_id")).toString();
            if (fromNode.isEmpty() && toNode.isEmpty()) {
                continue;
            }
            out.push_back(WorkflowPortEdgeView{
                from.value(QStringLiteral("stage_id")).toString(),
                fromNode,
                from.value(QStringLiteral("port_id")).toString(),
                to.value(QStringLiteral("stage_id")).toString(),
                toNode,
                to.value(QStringLiteral("port_id")).toString(),
                QStringLiteral("跨 stage")});
        }
    }
    return out;
}

// 从 workflow rawJson 还原真实 DAG：stage 顺序 + stage 内并行节点 + 真实依赖边
// （subgraph.edges 为 stage 内边，stage_edges 为跨 stage 边）。节点键统一为 stageId.nodeId。
void buildWorkflowDag(const PdkWorkflowView& workflow,
                      const PdkObjectPackageView& package,
                      QStringList& stageOrder,
                      std::vector<WorkflowDagNode>& nodes,
                      std::vector<WorkflowDagEdge>& edges) {
    const auto qualify = [](const QString& stage, const QString& node) {
        return stage + QStringLiteral(".") + node;
    };
    for (const QJsonValue& pv : workflow.rawJson.value(QStringLiteral("phases")).toArray()) {
        const QJsonObject phase = pv.toObject();
        for (const QJsonValue& sv : phase.value(QStringLiteral("stages")).toArray()) {
            const QJsonObject stage = sv.toObject();
            const QString sid = stage.value(QStringLiteral("stage_id")).toString();
            if (sid.isEmpty()) {
                continue;
            }
            const QString sfamily = stage.value(QStringLiteral("stage_family")).toString();
            if (!stageOrder.contains(sid)) {
                stageOrder << sid;
            }
            const QJsonObject subgraph = stage.value(QStringLiteral("subgraph")).toObject();
            for (const QJsonValue& nv : subgraph.value(QStringLiteral("nodes")).toArray()) {
                const QJsonObject node = nv.toObject();
                const QString nid = node.value(QStringLiteral("node_id")).toString();
                const PdkOperatorView* op =
                    findOperator(package, node.value(QStringLiteral("operator_ref")).toString());
                WorkflowDagNode dn;
                dn.id = qualify(sid, nid);
                dn.label = nid;
                dn.stageId = sid;
                dn.family = op ? op->operatorFamily
                               : (sfamily.isEmpty() ? QStringLiteral("missing") : sfamily);
                dn.status = op ? QStringLiteral("ok") : QStringLiteral("missing");
                nodes.push_back(std::move(dn));
            }
            for (const QJsonValue& ev : subgraph.value(QStringLiteral("edges")).toArray()) {
                const QJsonObject e = ev.toObject();
                const QString f = e.value(QStringLiteral("from")).toObject().value(QStringLiteral("node_id")).toString();
                const QString t = e.value(QStringLiteral("to")).toObject().value(QStringLiteral("node_id")).toString();
                if (!f.isEmpty() && !t.isEmpty()) {
                    edges.push_back(WorkflowDagEdge{qualify(sid, f), qualify(sid, t)});
                }
            }
        }
        for (const QJsonValue& ev : phase.value(QStringLiteral("stage_edges")).toArray()) {
            const QJsonObject e = ev.toObject();
            const QJsonObject f = e.value(QStringLiteral("from")).toObject();
            const QJsonObject t = e.value(QStringLiteral("to")).toObject();
            edges.push_back(WorkflowDagEdge{
                qualify(f.value(QStringLiteral("stage_id")).toString(), f.value(QStringLiteral("node_id")).toString()),
                qualify(t.value(QStringLiteral("stage_id")).toString(), t.value(QStringLiteral("node_id")).toString())});
        }
    }
}

QTableWidget* makeTable(const QStringList& headers, QWidget* parent) {
    auto* table = new QTableWidget(parent);
    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->verticalHeader()->hide();
    table->horizontalHeader()->setStretchLastSection(true);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    return table;
}

QStringList platformRunDirsForWorkflow(const QString& evidenceRoot, const QString& workflowId) {
    QStringList out;
    const QJsonObject mainline = readJsonObject(QDir(evidenceRoot).filePath(QStringLiteral("mainline_summary.json")));
    if (mainline.isEmpty()) {
        if (QFileInfo::exists(QDir(evidenceRoot).filePath(QStringLiteral("runtime_node_snapshot.json")))) {
            out << evidenceRoot;
        } else {
            const QFileInfoList children =
                QDir(evidenceRoot).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);
            for (const QFileInfo& child : children) {
                const QJsonObject snapshot =
                    readJsonObject(QDir(child.absoluteFilePath()).filePath(QStringLiteral("runtime_node_snapshot.json")));
                const QString runWorkflow = snapshot.value(QStringLiteral("workflow_id")).toString();
                if (!runWorkflow.isEmpty() && (workflowId.isEmpty() || runWorkflow == workflowId)) {
                    out << child.absoluteFilePath();
                }
            }
        }
        return out;
    }

    const QJsonObject online = mainline.value(QStringLiteral("online")).toObject();
    const QString onlineDir = pathFromJson(online.value(QStringLiteral("run_dir")));
    if (!onlineDir.isEmpty() && (workflowId.isEmpty() || workflowId.contains(QStringLiteral("online")))) {
        out << onlineDir;
    }
    for (const QJsonValue& value : mainline.value(QStringLiteral("prediction")).toObject()
                                      .value(QStringLiteral("runs")).toArray()) {
        const QString runDir = pathFromJson(value.toObject().value(QStringLiteral("run_dir")));
        if (!runDir.isEmpty() && (workflowId.isEmpty() || workflowId.contains(QStringLiteral("prediction")))) {
            out << runDir;
        }
    }
    return out;
}

} // namespace

GraphPage::GraphPage(QString objectPackageRoot, QWidget* parent)
    : QWidget(parent),
      objectPackage_(PdkObjectPackageReader().read(objectPackageRoot)) {
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    auto* templatePanel = new Panel(QStringLiteral("工作流模板"), this);
    templatePanel->setSubtitle(QStringLiteral("来自对象包 workflows/*.json"));
    templateList_ = new QListWidget(templatePanel->body());
    for (const PdkWorkflowView& workflow : objectPackage_.workflows) {
        auto* item = new QListWidgetItem(
            QStringLiteral("%1\n%2 · %3 个节点")
                .arg(workflow.workflowId, workflow.phase)
                .arg(workflow.nodes.size()));
        templateList_->addItem(item);
    }
    if (objectPackage_.workflows.isEmpty()) {
        templateList_->addItem(QStringLiteral("对象包暂无工作流"));
    }
    connect(templateList_, &QListWidget::currentRowChanged, this, &GraphPage::selectRow);
    templatePanel->bodyLayout()->addWidget(templateList_);
    root->addWidget(templatePanel, 1);

    auto* rightCol = new QVBoxLayout();
    rightCol->setSpacing(12);

    auto* canvasPanel = new Panel(QStringLiteral("工作流画布"), this);
    canvasPanel->setSubtitle(QStringLiteral("AtomicOperator 节点 · stage/family/端口"));
    canvasHint_ = new QLabel(QStringLiteral("选择一个工作流"), canvasPanel->body());
    canvasHint_->setProperty("muted", true);
    canvasHint_->setAlignment(Qt::AlignCenter);
    canvasPanel->bodyLayout()->addWidget(canvasHint_);
    canvas_ = new WorkflowDagWidget(canvasPanel->body());
    canvas_->setMinimumHeight(340);
    canvasPanel->bodyLayout()->addWidget(canvas_, 1);
    rightCol->addWidget(canvasPanel, 2);

    auto* nodeRendererRow = new QHBoxLayout();
    nodeRendererRow->setSpacing(12);

    auto* nodePanel = new Panel(QStringLiteral("算子节点与端口连接"), this);
    nodePanel->setSubtitle(QStringLiteral("节点来自 workflow；端口连接来自 edge.from/to.port_id"));
    nodeTable_ = makeTable(
        {QStringLiteral("节点ID"), QStringLiteral("算子族"), QStringLiteral("展示组件"), QStringLiteral("状态")},
        nodePanel->body());
    connect(nodeTable_, &QTableWidget::currentCellChanged, this, [this](int row, int, int, int) {
        showNodeRenderer(row);
    });
    nodePanel->bodyLayout()->addWidget(nodeTable_, 1);
    auto* edgeLabel = new QLabel(QStringLiteral("端口连接（上游输出端口 → 下游输入端口）"), nodePanel->body());
    edgeLabel->setProperty("tiny", true);
    nodePanel->bodyLayout()->addWidget(edgeLabel);
    edgeTable_ = makeTable(
        {QStringLiteral("上游节点"),
         QStringLiteral("输出端口"),
         QStringLiteral("下游节点"),
         QStringLiteral("输入端口"),
         QStringLiteral("范围")},
        nodePanel->body());
    edgeTable_->setMinimumHeight(150);
    nodePanel->bodyLayout()->addWidget(edgeTable_, 1);
    nodeRendererRow->addWidget(nodePanel, 1);

    auto* rendererPanel = new Panel(QStringLiteral("算子展示组件"), this);
    rendererPanel->setSubtitle(QStringLiteral("RendererRegistry 根据 renderer_id/value_kind 选择控件"));
    rendererMatch_ = new QLabel(QStringLiteral("-"), rendererPanel->body());
    rendererMatch_->setProperty("tiny", true);
    rendererMatch_->setWordWrap(true);
    rendererPanel->bodyLayout()->addWidget(rendererMatch_);
    rendererHost_ = new QWidget(rendererPanel->body());
    rendererHostLayout_ = new QVBoxLayout(rendererHost_);
    rendererHostLayout_->setContentsMargins(0, 0, 0, 0);
    rendererPanel->bodyLayout()->addWidget(rendererHost_, 1);
    nodeRendererRow->addWidget(rendererPanel, 1);
    rightCol->addLayout(nodeRendererRow, 2);

    auto* evidencePanel = new Panel(QStringLiteral("工作流摘要"), this);
    evidenceText_ = new QLabel(QStringLiteral("—"), evidencePanel->body());
    evidenceText_->setProperty("mono", true);
    evidenceText_->setWordWrap(true);
    evidenceText_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    evidenceText_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    evidencePanel->bodyLayout()->addWidget(evidenceText_);
    rightCol->addWidget(evidencePanel, 1);

    root->addLayout(rightCol, 2);

    templateList_->setCurrentRow(0);
}

void GraphPage::setEvidenceRoot(const QString& runDir) {
    evidenceRoot_ = runDir;
    refresh();
}

void GraphPage::selectRow(int index) {
    currentRow_ = index;
    refresh();
}

void GraphPage::refresh() {
    if (currentRow_ < 0 || currentRow_ >= objectPackage_.workflows.size()) {
        canvas_->clear();
        canvasHint_->setText(QStringLiteral("对象包暂无工作流，或工作流读取失败"));
        canvasHint_->show();
        if (nodeTable_) {
            nodeTable_->setRowCount(0);
        }
        if (edgeTable_) {
            edgeTable_->setRowCount(0);
        }
        evidenceText_->setText(QStringLiteral("—"));
        return;
    }
    showWorkflow(currentRow_);
}

void GraphPage::showWorkflow(int workflowIndex) {
    const PdkWorkflowView& workflow = objectPackage_.workflows[workflowIndex];
    canvasHint_->hide();
    currentNodeRow_ = 0;

    nodeTable_->setRowCount(0);
    for (const PdkWorkflowNodeView& node : workflow.nodes) {
        const PdkOperatorView* op = findOperator(objectPackage_, node.operatorRef);
        const int row = nodeTable_->rowCount();
        nodeTable_->insertRow(row);
        nodeTable_->setItem(row, 0, new QTableWidgetItem(node.nodeId));
        nodeTable_->setItem(row, 1, new QTableWidgetItem(op ? op->operatorFamily : QStringLiteral("missing")));
        nodeTable_->setItem(row, 2, new QTableWidgetItem(op ? op->display.rendererId : QStringLiteral("-")));
        nodeTable_->setItem(row, 3, new QTableWidgetItem(op ? QStringLiteral("ok") : QStringLiteral("missing operator")));
    }

    const QVector<WorkflowPortEdgeView> portEdges = collectWorkflowPortEdges(workflow);
    edgeTable_->setRowCount(0);
    for (const WorkflowPortEdgeView& edge : portEdges) {
        const int row = edgeTable_->rowCount();
        edgeTable_->insertRow(row);
        edgeTable_->setItem(row, 0, new QTableWidgetItem(endpointLabel(edge.fromStage, edge.fromNode)));
        edgeTable_->setItem(row, 1, new QTableWidgetItem(portLabel(edge.fromPort)));
        edgeTable_->setItem(row, 2, new QTableWidgetItem(endpointLabel(edge.toStage, edge.toNode)));
        edgeTable_->setItem(row, 3, new QTableWidgetItem(portLabel(edge.toPort)));
        edgeTable_->setItem(row, 4, new QTableWidgetItem(edge.scope));
    }
    if (edgeTable_->rowCount() == 0) {
        edgeTable_->insertRow(0);
        edgeTable_->setItem(0, 0, new QTableWidgetItem(QStringLiteral("当前 workflow 未声明端口连接")));
    }

    // 画布渲染真实 DAG：stage 列 + 列内并行 + 真实依赖边（不再串行平铺）。
    QStringList stageOrder;
    std::vector<WorkflowDagNode> dagNodes;
    std::vector<WorkflowDagEdge> dagEdges;
    buildWorkflowDag(workflow, objectPackage_, stageOrder, dagNodes, dagEdges);
    canvas_->setGraph(stageOrder, std::move(dagNodes), std::move(dagEdges));
    if (nodeTable_->rowCount() > 0) {
        nodeTable_->setCurrentCell(0, 0);
        showNodeRenderer(0);
    } else {
        showNodeRenderer(-1);
    }

    QString summary;
    summary += QStringLiteral("工作流: %1\n").arg(workflow.workflowId);
    summary += QStringLiteral("对象: %1\n").arg(workflow.objectId);
    summary += QStringLiteral("阶段: %1\n").arg(workflow.phase);
    summary += QStringLiteral("来源: %1\n\n").arg(workflow.path);

    QMap<QString, int> familyCounts;
    for (const PdkWorkflowNodeView& node : workflow.nodes) {
        const PdkOperatorView* op = findOperator(objectPackage_, node.operatorRef);
        familyCounts[op ? op->operatorFamily : QStringLiteral("missing")] += 1;
    }
    summary += QStringLiteral("算子族统计:\n");
    for (auto it = familyCounts.cbegin(); it != familyCounts.cend(); ++it) {
        summary += QStringLiteral("  · %1: %2\n").arg(it.key()).arg(it.value());
    }

    summary += QStringLiteral("\n节点:\n");
    int shown = 0;
    for (const PdkWorkflowNodeView& node : workflow.nodes) {
        if (shown++ >= 14) {
            summary += QStringLiteral("  …\n");
            break;
        }
        const PdkOperatorView* op = findOperator(objectPackage_, node.operatorRef);
        summary += QStringLiteral("  · %1 [%2/%3] -> %4\n")
            .arg(node.nodeId,
                 node.phaseId,
                 node.stageId,
                 op ? op->operatorId : node.operatorRef);
    }

    summary += QStringLiteral("\n端口连接:\n");
    shown = 0;
    for (const WorkflowPortEdgeView& edge : portEdges) {
        if (shown++ >= 18) {
            summary += QStringLiteral("  …\n");
            break;
        }
        summary += QStringLiteral("  · %1.%2 -> %3.%4 (%5)\n")
            .arg(endpointLabel(edge.fromStage, edge.fromNode),
                 portLabel(edge.fromPort),
                 endpointLabel(edge.toStage, edge.toNode),
                 portLabel(edge.toPort),
                 edge.scope);
    }
    if (portEdges.isEmpty()) {
        summary += QStringLiteral("  · 当前 workflow 未声明端口连接\n");
    }

    QString evidenceSummary;
    showRuntimeEvidenceForWorkflow(workflow.workflowId, &evidenceSummary);
    if (!evidenceSummary.isEmpty()) {
        summary += QStringLiteral("\n当前运行证据:\n") + evidenceSummary;
    }
    evidenceText_->setText(summary);
}

void GraphPage::showNodeRenderer(int row) {
    currentNodeRow_ = row;
    if (!rendererHostLayout_) {
        return;
    }
    if (currentRenderer_) {
        rendererHostLayout_->removeWidget(currentRenderer_);
        currentRenderer_->deleteLater();
        currentRenderer_ = nullptr;
    }
    if (currentRow_ < 0 || currentRow_ >= objectPackage_.workflows.size() ||
        row < 0 || row >= objectPackage_.workflows[currentRow_].nodes.size()) {
        if (rendererMatch_) {
            rendererMatch_->setText(QStringLiteral("未选择 workflow 节点"));
        }
        return;
    }

    const PdkWorkflowView& workflow = objectPackage_.workflows[currentRow_];
    const PdkWorkflowNodeView& node = workflow.nodes[row];
    const PdkOperatorView* op = findOperator(objectPackage_, node.operatorRef);
    if (!op) {
        if (rendererMatch_) {
            rendererMatch_->setText(QStringLiteral("缺少算子定义: %1").arg(node.operatorRef));
        }
        return;
    }

    OperatorRenderContext ctx;
    ctx.op = *op;
    ctx.objectPackageRoot = objectPackage_.rootPath;

    const QStringList runDirs = platformRunDirsForWorkflow(evidenceRoot_, workflow.workflowId);
    for (const QString& runDir : runDirs) {
        PdkDataPlaneView dataPlane = PdkDataPlaneReader().read(runDir);
        const bool hasOperatorOutput = std::any_of(
            dataPlane.entries.cbegin(),
            dataPlane.entries.cend(),
            [op](const PdkDataPlaneEntryView& entry) {
                return entry.operatorId == op->operatorId &&
                       entry.direction == QStringLiteral("output");
            });
        if (hasOperatorOutput || ctx.dataPlane.isEmpty()) {
            ctx.evidenceRoot = runDir;
            ctx.dataPlane = std::move(dataPlane.entries);
        }
        if (hasOperatorOutput) {
            break;
        }
    }

    const RendererMatch match = RendererRegistry::instance().resolve(*op);
    if (rendererMatch_) {
        QString text = QStringLiteral("节点=%1 | 算子=%2 | 展示组件=%3")
                           .arg(node.nodeId, op->operatorId, match.resolvedRendererId);
        if (match.usedFallback) {
            text += match.usedGeneric
                ? QStringLiteral(" | 回退=通用端口视图")
                : QStringLiteral(" | 回退=fallback");
        }
        if (ctx.evidenceRoot.isEmpty()) {
            text += QStringLiteral(" | 证据=仅规格定义");
        }
        rendererMatch_->setText(text);
    }

    currentRenderer_ = RendererRegistry::instance().render(ctx, rendererHost_);
    rendererHostLayout_->addWidget(currentRenderer_, 1);
}

void GraphPage::showRuntimeEvidenceForWorkflow(const QString& workflowId, QString* summarySuffix) {
    if (!summarySuffix || evidenceRoot_.isEmpty()) {
        return;
    }
    const QStringList runDirs = platformRunDirsForWorkflow(evidenceRoot_, workflowId);
    for (const QString& runDir : runDirs) {
        const QJsonObject snapshot = readJsonObject(QDir(runDir).filePath(QStringLiteral("runtime_node_snapshot.json")));
        const QJsonArray nodes = snapshot.value(QStringLiteral("nodes")).toArray();
        if (nodes.isEmpty()) {
            continue;
        }
        int okCount = 0;
        int nonOkCount = 0;
        for (const QJsonValue& value : nodes) {
            const QString status = value.toObject().value(QStringLiteral("status")).toString();
            if (status == QStringLiteral("ok")) {
                ++okCount;
            } else if (!status.isEmpty() && status != QStringLiteral("disabled")) {
                ++nonOkCount;
            }
        }
        *summarySuffix += QStringLiteral("  · %1: 节点=%2 正常=%3 异常=%4\n")
            .arg(QFileInfo(runDir).fileName())
            .arg(nodes.size())
            .arg(okCount)
            .arg(nonOkCount);
    }
}

} // namespace twin
