#include "OverviewPage.h"

#include "../datahub/LegacyRunCatalogSource.h"
#include "../datahub/PdkUiReaders.h"
#include "../widgets/Panel.h"
#include "../widgets/PageHeader.h"
#include "../widgets/StageStrip.h"
#include "../widgets/StatusUtil.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <map>

namespace twin {

namespace {

int familyCount(const PdkObjectPackageView& objectPackage, const QString& family) {
    int count = 0;
    for (const PdkOperatorView& op : objectPackage.operators) {
        if (op.operatorFamily == family) {
            ++count;
        }
    }
    return count;
}

StageStrip::Status statusForCount(int count) {
    return count > 0 ? StageStrip::Status::Ok : StageStrip::Status::Unknown;
}

QString workflowSummary(const PdkObjectPackageView& objectPackage) {
    return QStringLiteral("%1 个工作流，%2 个算子")
        .arg(objectPackage.workflows.size())
        .arg(objectPackage.operators.size());
}

QJsonObject readJsonObject(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    return (err.error == QJsonParseError::NoError && doc.isObject()) ? doc.object() : QJsonObject{};
}

QString pathFromJson(const QJsonValue& value) {
    return QDir::fromNativeSeparators(value.toString());
}

void addRiskRow(QTableWidget* table, const QString& severity, const QString& item, const QString& detail) {
    const int row = table->rowCount();
    table->insertRow(row);
    setBadgeCell(table, row, 0, severity, severity);
    table->setItem(row, 1, new QTableWidgetItem(item));
    table->setItem(row, 2, new QTableWidgetItem(detail));
}

} // namespace

OverviewPage::OverviewPage(
    LegacyRunCatalogSource* legacyRunCatalog,
    QString evidenceRoot,
    QString objectPackageRoot,
    QWidget* parent)
    : QWidget(parent), legacyRunCatalog_(legacyRunCatalog) {
    const PdkObjectPackageView objectPackage = PdkObjectPackageReader().read(objectPackageRoot);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    auto* headRow = new QHBoxLayout();
    headRow->addWidget(makePageHeader(
        QStringLiteral("运行总览"),
        QStringLiteral("对象包 -> 工作流编译 -> 运行时主机 -> 数据平面 -> 证据链"), this));
    headRow->addStretch(1);
    auto* toOperators = new QPushButton(QStringLiteral("算子库"), this);
    connect(toOperators, &QPushButton::clicked, this, [this]() { emit navigateTo(QStringLiteral("operators")); });
    auto* toRuntime = new QPushButton(QStringLiteral("运行时主机"), this);
    connect(toRuntime, &QPushButton::clicked, this, [this]() { emit navigateTo(QStringLiteral("runtime")); });
    auto* toOnline = new QPushButton(QStringLiteral("在线融合"), this);
    toOnline->setProperty("primary", true);
    connect(toOnline, &QPushButton::clicked, this, [this]() { emit navigateTo(QStringLiteral("online")); });
    headRow->addWidget(toOperators);
    headRow->addWidget(toRuntime);
    headRow->addWidget(toOnline);
    root->addLayout(headRow);

    auto* platformPanel = new Panel(QStringLiteral("平台运行闭环"), this);
    platformPanel->setSubtitle(QStringLiteral("平台固有流程；具体对象资源和算子从对象包动态读取"));
    auto* platformStrip = new StageStrip(platformPanel->body());
    platformStrip->setMinimumHeight(82);
    const bool hasObjectPackage = objectPackage.ok();
    const bool hasEvidence = !evidenceRoot.isEmpty() && QFileInfo::exists(evidenceRoot);
    std::vector<StageStrip::Stage> platformStages = {
        {QStringLiteral("对象包"), QStringLiteral("对象/资源/算子/工作流"),
         hasObjectPackage ? objectPackage.objectId : QStringLiteral("缺失"),
         QString(), QDir::toNativeSeparators(objectPackageRoot), hasObjectPackage ? StageStrip::Status::Ok : StageStrip::Status::Fail},
        {QStringLiteral("工作流"), QStringLiteral("编译源"),
         workflowSummary(objectPackage), QString(), QStringLiteral("对象工作流"), statusForCount(objectPackage.workflows.size())},
        {QStringLiteral("执行计划"), QStringLiteral("编译产物"),
         QStringLiteral("执行/时间/调度/状态"), QString(), QStringLiteral("compiled-workflows"), StageStrip::Status::Unknown},
        {QStringLiteral("运行时主机"), QStringLiteral("时钟/调度/端口状态"),
         QStringLiteral("证据驱动"), QString(), QStringLiteral("runtime-host-runs"), hasEvidence ? StageStrip::Status::Ok : StageStrip::Status::Unknown},
        {QStringLiteral("数据平面"), QStringLiteral("内联/制品/张量/缓冲"),
         QStringLiteral("场张量引用"), QString(), QStringLiteral("data_plane_manifest"), hasEvidence ? StageStrip::Status::Ok : StageStrip::Status::Unknown},
        {QStringLiteral("证据链"), QStringLiteral("快照/锁定/耗时/错误"),
         hasEvidence ? QStringLiteral("已连接") : QStringLiteral("空"),
         QString(), QDir::toNativeSeparators(evidenceRoot), hasEvidence ? StageStrip::Status::Ok : StageStrip::Status::Unknown},
    };
    platformStrip->setStages(std::move(platformStages));
    platformPanel->bodyLayout()->addWidget(platformStrip);
    root->addWidget(platformPanel);

    auto* familyPanel = new Panel(QStringLiteral("平台算子族"), this);
    familyPanel->setSubtitle(QStringLiteral("主框架只认 family；弹道、多场、损伤、寿命都是对象可选 AtomicOperator"));
    auto* familyStrip = new StageStrip(familyPanel->body());
    familyStrip->setMinimumHeight(82);
    const int transitionCount = familyCount(objectPackage, QStringLiteral("state_transition"));
    const int observationCount = familyCount(objectPackage, QStringLiteral("observation_equation"));
    const int filterCount = familyCount(objectPackage, QStringLiteral("filter_algorithm"));
    const int qoiCount = familyCount(objectPackage, QStringLiteral("qoi"));
    std::vector<StageStrip::Stage> familyStages = {
        {QStringLiteral("state_transition"), QStringLiteral("状态转移方程"),
         QStringLiteral("%1 个算子").arg(transitionCount), QString(), QStringLiteral("状态/场/累计量"), statusForCount(transitionCount)},
        {QStringLiteral("observation_equation"), QStringLiteral("观测方程"),
         QStringLiteral("%1 个算子").arg(observationCount), QString(), QStringLiteral("传感器投影"), statusForCount(observationCount)},
        {QStringLiteral("filter_algorithm"), QStringLiteral("滤波算法"),
         QStringLiteral("%1 个算子").arg(filterCount), QString(), QStringLiteral("后验估计"), statusForCount(filterCount)},
        {QStringLiteral("qoi"), QStringLiteral("QoI 方程"),
         QStringLiteral("%1 个算子").arg(qoiCount), QString(), QStringLiteral("判据/剩余寿命/极值"), statusForCount(qoiCount)},
    };
    familyStrip->setStages(std::move(familyStages));
    familyPanel->bodyLayout()->addWidget(familyStrip);
    root->addWidget(familyPanel);

    auto* timelineRiskRow = new QHBoxLayout();
    timelineRiskRow->setSpacing(12);

    const QJsonObject mainline = readJsonObject(QDir(evidenceRoot).filePath(QStringLiteral("mainline_summary.json")));
    auto* timelinePanel = new Panel(QStringLiteral("Online / prediction timelines"), this);
    timelinePanel->setSubtitle(QStringLiteral("mainline_summary.json"));
    auto* timelineTable = makeTable(
        {QStringLiteral("branch"), QStringLiteral("run_id"), QStringLiteral("status"),
         QStringLiteral("frames/steps"), QStringLiteral("stop"), QStringLiteral("path")},
        timelinePanel->body());
    const QJsonObject online = mainline.value(QStringLiteral("online")).toObject();
    if (!online.isEmpty()) {
        const int row = timelineTable->rowCount();
        timelineTable->insertRow(row);
        timelineTable->setItem(row, 0, new QTableWidgetItem(QStringLiteral("online")));
        timelineTable->setItem(row, 1, new QTableWidgetItem(online.value(QStringLiteral("run_id")).toString()));
        setBadgeCell(timelineTable, row, 2, online.value(QStringLiteral("status")).toString(QStringLiteral("ok")));
        timelineTable->setItem(row, 3, new QTableWidgetItem(QString::number(
            online.value(QStringLiteral("frame_count")).toInt(
                online.value(QStringLiteral("completed_frames")).toInt()))));
        timelineTable->setItem(row, 4, new QTableWidgetItem(online.value(QStringLiteral("stop_reason")).toString(QStringLiteral("-"))));
        timelineTable->setItem(row, 5, new QTableWidgetItem(pathFromJson(online.value(QStringLiteral("run_dir")))));
    }
    for (const QJsonValue& value : mainline.value(QStringLiteral("prediction")).toObject()
                                      .value(QStringLiteral("runs")).toArray()) {
        const QJsonObject run = value.toObject();
        const int row = timelineTable->rowCount();
        timelineTable->insertRow(row);
        timelineTable->setItem(row, 0, new QTableWidgetItem(QStringLiteral("prediction")));
        timelineTable->setItem(row, 1, new QTableWidgetItem(run.value(QStringLiteral("run_id")).toString()));
        setBadgeCell(timelineTable, row, 2, run.value(QStringLiteral("status")).toString(QStringLiteral("ok")));
        timelineTable->setItem(row, 3, new QTableWidgetItem(QString::number(
            run.value(QStringLiteral("iteration_count")).toInt(
                run.value(QStringLiteral("step_count")).toInt()))));
        timelineTable->setItem(row, 4, new QTableWidgetItem(run.value(QStringLiteral("stop_reason")).toString(QStringLiteral("-"))));
        timelineTable->setItem(row, 5, new QTableWidgetItem(pathFromJson(run.value(QStringLiteral("run_dir")))));
    }
    if (timelineTable->rowCount() == 0) {
        timelineTable->insertRow(0);
        timelineTable->setItem(0, 0, new QTableWidgetItem(QStringLiteral("no evidence")));
        timelineTable->setItem(0, 5, new QTableWidgetItem(QDir::toNativeSeparators(evidenceRoot)));
    }
    timelinePanel->bodyLayout()->addWidget(timelineTable);
    timelineRiskRow->addWidget(timelinePanel, 2);

    auto* riskPanel = new Panel(QStringLiteral("Platform risk summary"), this);
    riskPanel->setSubtitle(QStringLiteral("object package / evidence / data plane"));
    auto* riskTable = makeTable(
        {QStringLiteral("severity"), QStringLiteral("item"), QStringLiteral("detail")},
        riskPanel->body());
    for (const PdkReadIssue& issue : objectPackage.issues) {
        addRiskRow(riskTable, issue.severity, issue.code, issue.message);
    }
    if (!hasObjectPackage) {
        addRiskRow(riskTable, QStringLiteral("error"), QStringLiteral("object_package"), objectPackageRoot);
    }
    if (!hasEvidence) {
        addRiskRow(riskTable, QStringLiteral("warning"), QStringLiteral("evidence_root"), evidenceRoot);
    } else {
        const PdkDataPlaneView dataPlane = PdkDataPlaneReader().read(evidenceRoot);
        if (!dataPlane.ok()) {
            for (const PdkReadIssue& issue : dataPlane.issues) {
                addRiskRow(riskTable, issue.severity, issue.code, issue.message);
            }
        }
        if (dataPlane.inlineJsonCount() > 0) {
            addRiskRow(riskTable,
                       QStringLiteral("warning"),
                       QStringLiteral("inline_json"),
                       QStringLiteral("DataPlane contains %1 inline JSON entries").arg(dataPlane.inlineJsonCount()));
        }
    }
    if (riskTable->rowCount() == 0) {
        addRiskRow(riskTable, QStringLiteral("ok"), QStringLiteral("platform_ui"), QStringLiteral("No blocking UI risk found"));
    }
    riskPanel->bodyLayout()->addWidget(riskTable);
    timelineRiskRow->addWidget(riskPanel, 1);
    root->addLayout(timelineRiskRow, 1);

    auto* twoCol = new QHBoxLayout();
    twoCol->setSpacing(12);

    auto* workflowPanel = new Panel(QStringLiteral("对象工作流"), this);
    workflowPanel->setSubtitle(QStringLiteral("flightenv-object-reentry-vehicle/workflows/*.json"));
    auto* workflowTable = makeTable({QStringLiteral("工作流ID"), QStringLiteral("阶段"), QStringLiteral("节点数")}, workflowPanel->body());
    for (const PdkWorkflowView& wf : objectPackage.workflows) {
        const int r = workflowTable->rowCount();
        workflowTable->insertRow(r);
        workflowTable->setItem(r, 0, new QTableWidgetItem(wf.workflowId));
        workflowTable->setItem(r, 1, new QTableWidgetItem(wf.phase));
        workflowTable->setItem(r, 2, new QTableWidgetItem(QString::number(wf.nodes.size())));
    }
    if (workflowTable->rowCount() == 0) {
        workflowTable->insertRow(0);
        workflowTable->setItem(0, 0, new QTableWidgetItem(QStringLiteral("对象包暂无工作流")));
    }
    connect(workflowTable, &QTableWidget::cellClicked, this, [this](int, int) { emit navigateTo(QStringLiteral("graph")); });
    workflowPanel->bodyLayout()->addWidget(workflowTable);
    twoCol->addWidget(workflowPanel, 1);

    auto* runPanel = new Panel(QStringLiteral("最近证据"), this);
    runPanel->setSubtitle(QStringLiteral("catalog run_catalog · 作为回放/追溯入口"));
    auto* runTable = makeTable({QStringLiteral("run_id"), QStringLiteral("状态"), QStringLiteral("模型/资源摘要")}, runPanel->body());
    if (legacyRunCatalog && legacyRunCatalog->ok()) {
        for (const auto& r : legacyRunCatalog->runs()) {
            const int row = runTable->rowCount();
            runTable->insertRow(row);
            runTable->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(r.run_id)));
            setBadgeCell(runTable, row, 1, QString::fromStdString(r.status.empty() ? "ok" : r.status));
            runTable->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(r.model_asset_summary)));
        }
    }
    if (runTable->rowCount() == 0) {
        runTable->insertRow(0);
        runTable->setItem(0, 0, new QTableWidgetItem(QStringLiteral("—")));
        runTable->setItem(0, 2, new QTableWidgetItem(QStringLiteral("catalog 暂无 run 记录")));
    }
    connect(runTable, &QTableWidget::cellClicked, this, [this](int, int) { emit navigateTo(QStringLiteral("replay")); });
    runPanel->bodyLayout()->addWidget(runTable);
    twoCol->addWidget(runPanel, 1);

    root->addLayout(twoCol, 1);
}

} // namespace twin
