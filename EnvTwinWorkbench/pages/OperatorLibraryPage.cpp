#include "OperatorLibraryPage.h"

#include "../widgets/KvList.h"
#include "../widgets/OperatorRenderer.h"
#include "../widgets/Panel.h"
#include "../widgets/PageHeader.h"
#include "../widgets/StatusUtil.h"

#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace twin {

namespace {

QString familyText(const QString& family) {
    if (family == QStringLiteral("state_transition")) {
        return QStringLiteral("状态转移方程");
    }
    if (family == QStringLiteral("observation_equation")) {
        return QStringLiteral("观测方程");
    }
    if (family == QStringLiteral("filter_algorithm")) {
        return QStringLiteral("滤波算法");
    }
    if (family == QStringLiteral("qoi")) {
        return QStringLiteral("QoI 指标/判据");
    }
    return family.isEmpty() ? QStringLiteral("未声明") : family;
}

QString executionText(const QString& kind) {
    if (kind == QStringLiteral("dll")) {
        return QStringLiteral("DLL 常驻适配器");
    }
    if (kind == QStringLiteral("cli")) {
        return QStringLiteral("命令行适配器");
    }
    if (kind == QStringLiteral("ros2_node")) {
        return QStringLiteral("ROS2 节点适配器");
    }
    if (kind == QStringLiteral("python")) {
        return QStringLiteral("Python 适配器");
    }
    if (kind == QStringLiteral("recording")) {
        return QStringLiteral("记录回放适配器");
    }
    return kind.isEmpty() ? QStringLiteral("未声明") : kind;
}

QString phaseText(const QString& phase) {
    if (phase == QStringLiteral("online_filtering")) {
        return QStringLiteral("在线滤波");
    }
    if (phase == QStringLiteral("future_prediction")) {
        return QStringLiteral("未来预测");
    }
    if (phase == QStringLiteral("offline_replay")) {
        return QStringLiteral("离线回放");
    }
    if (phase == QStringLiteral("training_validation")) {
        return QStringLiteral("训练验证");
    }
    return phase;
}

QString phasesText(const QStringList& phases) {
    QStringList out;
    for (const QString& phase : phases) {
        out << phaseText(phase);
    }
    return out.isEmpty() ? QStringLiteral("未声明") : out.join(QStringLiteral(", "));
}

QString valueKindText(const QString& kind) {
    if (kind == QStringLiteral("state")) {
        return QStringLiteral("状态");
    }
    if (kind == QStringLiteral("observation")) {
        return QStringLiteral("观测");
    }
    if (kind == QStringLiteral("field_coefficient")) {
        return QStringLiteral("场系数");
    }
    if (kind == QStringLiteral("field_tensor")) {
        return QStringLiteral("完整场张量");
    }
    if (kind == QStringLiteral("artifact_ref")) {
        return QStringLiteral("制品引用");
    }
    if (kind == QStringLiteral("qoi")) {
        return QStringLiteral("QoI 指标");
    }
    return kind.isEmpty() ? QStringLiteral("未声明") : kind;
}

QString operatorTitle(const PdkOperatorView& op) {
    if (!op.display.displayTitle.isEmpty()) {
        return op.display.displayTitle;
    }
    return op.operatorId;
}

QString portsText(const QVector<PdkPortView>& ports) {
    QStringList lines;
    for (const PdkPortView& port : ports) {
        QString line = QStringLiteral("%1 · 契约=%2 · frame=%3 · 类型=%4 · %5")
                           .arg(port.portId,
                                port.contractId,
                                port.frameContract.isEmpty() ? QStringLiteral("—") : port.frameContract,
                                valueKindText(port.valueKind),
                                port.required ? QStringLiteral("必需") : QStringLiteral("可选"));
        // 结构体化(TypedDTO)：展示端口的强类型 I/O 结构体 + 传输策略。
        const QString structName = port.typedTypeName.isEmpty() ? port.typedDtoName : port.typedTypeName;
        if (!structName.isEmpty()) {
            line += QStringLiteral("\n    └ 结构体 %1").arg(structName);
            if (!port.typedSchemaId.isEmpty()) {
                line += QStringLiteral(" · schema=%1").arg(port.typedSchemaId);
            }
            QStringList flags;
            if (port.jsonIoForbidden) {
                flags << QStringLiteral("Typed缓冲(禁JSON)");
            }
            if (port.zeroCopyEligible) {
                flags << QStringLiteral("zero-copy");
            }
            if (!port.bufferLayoutId.isEmpty()) {
                flags << QStringLiteral("layout=%1").arg(port.bufferLayoutId);
            }
            if (!port.typedStatus.isEmpty()) {
                flags << port.typedStatus;
            }
            if (!flags.isEmpty()) {
                line += QStringLiteral("  [%1]").arg(flags.join(QStringLiteral(" · ")));
            }
        }
        lines << line;
    }
    return lines.isEmpty() ? QStringLiteral("—") : lines.join(QStringLiteral("\n"));
}

} // namespace

OperatorLibraryPage::OperatorLibraryPage(
    QString objectPackageRoot,
    QWidget* parent)
    : QWidget(parent),
      objectPackage_(PdkObjectPackageReader().read(objectPackageRoot)),
      objectPackageRoot_(objectPackageRoot) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);
    root->addWidget(makePageHeader(
        QStringLiteral("算子库"),
        QStringLiteral("AtomicOperator = 可编排接口身份；DLL/CLI/ROS2/Python 只是执行 backend"), this));

    auto* body = new QHBoxLayout();
    body->setSpacing(12);

    auto* listPanel = new Panel(QStringLiteral("AtomicOperator 列表"), this);
    table_ = makeTable({
        QStringLiteral("算子ID"),
        QStringLiteral("算子族"),
        QStringLiteral("执行后端"),
        QStringLiteral("阶段"),
        QStringLiteral("显示标题")
    }, listPanel->body());
    for (const PdkOperatorView& op : objectPackage_.operators) {
        const int row = table_->rowCount();
        table_->insertRow(row);
        table_->setItem(row, 0, new QTableWidgetItem(op.operatorId));
        table_->setItem(row, 1, new QTableWidgetItem(familyText(op.operatorFamily)));
        table_->setItem(row, 2, new QTableWidgetItem(executionText(op.executionKind)));
        table_->setItem(row, 3, new QTableWidgetItem(phasesText(op.phases)));
        table_->setItem(row, 4, new QTableWidgetItem(operatorTitle(op)));
    }
    if (table_->rowCount() == 0) {
        table_->insertRow(0);
        table_->setItem(0, 0, new QTableWidgetItem(QStringLiteral("对象包暂无 AtomicOperator")));
    }
    connect(table_, &QTableWidget::currentCellChanged, this, [this](int row, int, int, int) { showDetail(row); });
    listPanel->bodyLayout()->addWidget(table_);
    body->addWidget(listPanel, 2);

    auto* detailPanel = new Panel(QStringLiteral("算子详情"), this);
    detailPanel->setMinimumWidth(360);
    detailPanel->setMaximumWidth(500);
    detailTitle_ = new QLabel(QStringLiteral("选择一个算子"), detailPanel->body());
    detailTitle_->setProperty("mono", true);
    detailTitle_->setWordWrap(true);
    detailTitle_->setStyleSheet(QStringLiteral("font-weight:700;font-size:13px;color:#1b1f27;"));
    detailPanel->bodyLayout()->addWidget(detailTitle_);
    detailKv_ = new KvList(detailPanel->body());
    detailPanel->bodyLayout()->addWidget(detailKv_);

    rendererMatch_ = new QLabel(QStringLiteral("—"), detailPanel->body());
    rendererMatch_->setProperty("tiny", true);
    rendererMatch_->setWordWrap(true);
    detailPanel->bodyLayout()->addWidget(rendererMatch_);

    // 平台 renderer 嵌入位：由 display_descriptor 选 renderer，不在页面里按算子写死。
    rendererHost_ = new QWidget(detailPanel->body());
    rendererHostLayout_ = new QVBoxLayout(rendererHost_);
    rendererHostLayout_->setContentsMargins(0, 0, 0, 0);
    detailPanel->bodyLayout()->addWidget(rendererHost_, 1);
    body->addWidget(detailPanel);

    root->addLayout(body, 1);
    if (!objectPackage_.operators.isEmpty()) {
        table_->setCurrentCell(0, 0);
        showDetail(0);
    }
}

void OperatorLibraryPage::setEvidenceRoot(const QString& evidenceRoot) {
    evidenceRoot_ = evidenceRoot;
    if (currentRow_ >= 0) {
        rebuildRenderer();
    }
}

void OperatorLibraryPage::rebuildRenderer() {
    if (!rendererHostLayout_ || currentRow_ < 0 || currentRow_ >= objectPackage_.operators.size()) {
        return;
    }
    if (currentRenderer_) {
        rendererHostLayout_->removeWidget(currentRenderer_);
        currentRenderer_->deleteLater();
        currentRenderer_ = nullptr;
    }
    const PdkOperatorView& op = objectPackage_.operators[currentRow_];

    OperatorRenderContext ctx;
    ctx.op = op;
    ctx.evidenceRoot = evidenceRoot_;
    ctx.objectPackageRoot = objectPackageRoot_;
    if (!evidenceRoot_.isEmpty()) {
        ctx.dataPlane = PdkDataPlaneReader().read(evidenceRoot_).entries;
    }

    const RendererMatch match = RendererRegistry::instance().resolve(op);
    QString matchText = QStringLiteral("展示组件: %1").arg(match.resolvedRendererId);
    if (match.usedFallback) {
        matchText += match.usedGeneric
            ? QStringLiteral("（声明 %1 未注册 → 通用端口视图）").arg(match.requestedRendererId)
            : QStringLiteral("（回退 fallback）");
    }
    rendererMatch_->setText(matchText);

    currentRenderer_ = RendererRegistry::instance().render(ctx, rendererHost_);
    rendererHostLayout_->addWidget(currentRenderer_, 1);
}

void OperatorLibraryPage::showDetail(int row) {
    if (row < 0 || row >= objectPackage_.operators.size()) {
        return;
    }
    const PdkOperatorView& op = objectPackage_.operators[row];
    detailTitle_->setText(QStringLiteral("%1\n%2").arg(operatorTitle(op), op.operatorId));
    detailKv_->clear();
    detailKv_->addRow(QStringLiteral("算子族"), familyText(op.operatorFamily), false);
    detailKv_->addRow(QStringLiteral("算子类型"), op.operatorKind, false);
    detailKv_->addRow(QStringLiteral("执行后端"), executionText(op.executionKind), false);
    detailKv_->addRow(QStringLiteral("适配器ID"), op.adapterId);
    detailKv_->addRow(QStringLiteral("适用阶段"), phasesText(op.phases), false);
    detailKv_->addRow(QStringLiteral("输入端口"), portsText(op.inputs), false);
    detailKv_->addRow(QStringLiteral("输出端口"), portsText(op.outputs), false);
    detailKv_->addRow(QStringLiteral("资源引用"), op.resourceRefs.join(QStringLiteral("\n")), false);
    detailKv_->addRow(QStringLiteral("显示组件"), op.display.rendererId);
    detailKv_->addRow(QStringLiteral("备用显示"), op.display.fallbackRenderer);
    detailKv_->addRow(QStringLiteral("主要输出"), op.display.primaryOutputs.join(QStringLiteral("\n")), false);
    detailKv_->addRow(QStringLiteral("时间策略"), QString::fromUtf8(QJsonDocument(op.rawJson.value(QStringLiteral("time_policy")).toObject()).toJson(QJsonDocument::Compact)));
    detailKv_->addRow(QStringLiteral("调度策略"), QString::fromUtf8(QJsonDocument(op.rawJson.value(QStringLiteral("scheduler_policy")).toObject()).toJson(QJsonDocument::Compact)));

    currentRow_ = row;
    rebuildRenderer();
}

} // namespace twin
