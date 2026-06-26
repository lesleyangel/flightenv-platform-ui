#include "DiagnosticsPage.h"

#include "../datahub/LegacyRunCatalogSource.h"
#include "../datahub/PdkUiReaders.h"
#include "../widgets/Panel.h"
#include "../widgets/PageHeader.h"
#include "../widgets/StatusUtil.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileInfoList>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <map>
#include <utility>

namespace twin {

namespace {

QJsonObject readJsonObject(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QByteArray payload = file.readAll();
    QJsonParseError err{};
    QJsonDocument doc = QJsonDocument::fromJson(payload, &err);
    if (err.error != QJsonParseError::NoError) {
        payload.replace("-Infinity", "null");
        payload.replace("Infinity", "null");
        payload.replace("NaN", "null");
        doc = QJsonDocument::fromJson(payload);
    }
    return doc.object();
}

QString pathFromJson(const QJsonValue& value) {
    return QDir::fromNativeSeparators(value.toString());
}

QStringList runtimeRunDirs(const QString& evidenceRoot) {
    QStringList out;
    const QJsonObject mainline =
        readJsonObject(QDir(evidenceRoot).filePath(QStringLiteral("mainline_summary.json")));
    if (mainline.isEmpty()) {
        if (QFileInfo::exists(QDir(evidenceRoot).filePath(QStringLiteral("runtime_node_snapshot.json")))) {
            out << evidenceRoot;
        } else {
            const QFileInfoList children =
                QDir(evidenceRoot).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);
            for (const QFileInfo& child : children) {
                if (QFileInfo::exists(QDir(child.absoluteFilePath()).filePath(QStringLiteral("runtime_node_snapshot.json")))) {
                    out << child.absoluteFilePath();
                }
            }
        }
        return out;
    }
    const QString onlineDir =
        pathFromJson(mainline.value(QStringLiteral("online")).toObject().value(QStringLiteral("run_dir")));
    if (!onlineDir.isEmpty()) {
        out << onlineDir;
    }
    for (const QJsonValue& value :
         mainline.value(QStringLiteral("prediction")).toObject().value(QStringLiteral("runs")).toArray()) {
        const QString runDir = pathFromJson(value.toObject().value(QStringLiteral("run_dir")));
        if (!runDir.isEmpty()) {
            out << runDir;
        }
    }
    return out;
}

void addCheck(QTableWidget* table, const QString& check, const QString& status, const QString& detail) {
    const int row = table->rowCount();
    table->insertRow(row);
    table->setItem(row, 0, new QTableWidgetItem(check));
    setBadgeCell(table, row, 1, status);
    table->setItem(row, 2, new QTableWidgetItem(detail));
}

QString workspaceFromObjectPackage(QString objectPackageRoot) {
    QDir dir(std::move(objectPackageRoot));
    dir.cdUp();
    return dir.absolutePath();
}

QString issueSummary(const QVector<PdkReadIssue>& issues) {
    if (issues.isEmpty()) {
        return QStringLiteral("未发现问题");
    }
    QStringList lines;
    for (const PdkReadIssue& issue : issues) {
        lines << QStringLiteral("%1/%2: %3").arg(issue.severity, issue.code, issue.message);
    }
    return lines.join(QStringLiteral("\n"));
}

} // namespace

DiagnosticsPage::DiagnosticsPage(
    LegacyRunCatalogSource* legacyRunCatalog,
    QString evidenceRoot,
    QString objectPackageRoot,
    QWidget* parent)
    : QWidget(parent),
      legacyRunCatalog_(legacyRunCatalog),
      evidenceRoot_(std::move(evidenceRoot)),
      objectPackageRoot_(std::move(objectPackageRoot)) {
    const PdkObjectPackageView objectPackage = PdkObjectPackageReader().read(objectPackageRoot_);
    const QString workspaceRoot = workspaceFromObjectPackage(objectPackageRoot_);
    const QDir compiledRoot(QDir(workspaceRoot).filePath(QStringLiteral("_local_artifacts/platform-pdk/compiled-workflows")));
    const QStringList runDirs = runtimeRunDirs(evidenceRoot_);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);
    root->addWidget(makePageHeader(
        QStringLiteral("诊断报告"),
        QStringLiteral("对象包 / 工作流 / 编译计划 / 运行时证据的平台一致性检查"), this));

    auto* body = new QHBoxLayout();
    body->setSpacing(12);

    auto* preflightPanel = new Panel(QStringLiteral("平台预检"), this);
    auto* preTable = makeTable(
        {QStringLiteral("检查项"), QStringLiteral("状态"), QStringLiteral("详情")},
        preflightPanel->body());

    addCheck(preTable,
             QStringLiteral("旧 run catalog"),
             (legacyRunCatalog_ && legacyRunCatalog_->ok()) ? QStringLiteral("ok") : QStringLiteral("warn"),
             (legacyRunCatalog_ && legacyRunCatalog_->ok()) ? QStringLiteral("历史 run catalog 可用")
                                                            : QStringLiteral("对象包是主真源；缺少旧 catalog 仅影响历史 run 兼容视图"));
    addCheck(preTable,
             QStringLiteral("对象包"),
             objectPackage.ok() ? QStringLiteral("ok") : QStringLiteral("fail"),
             objectPackage.ok() ? objectPackage.objectId : issueSummary(objectPackage.issues));
    addCheck(preTable,
             QStringLiteral("components"),
             objectPackage.twinObjectJson.value(QStringLiteral("components")).toArray().isEmpty()
                 ? QStringLiteral("warn")
                 : QStringLiteral("ok"),
             QString::number(objectPackage.twinObjectJson.value(QStringLiteral("components")).toArray().size()));
    addCheck(preTable,
             QStringLiteral("asset_groups"),
             objectPackage.assetGroups.isEmpty() ? QStringLiteral("warn") : QStringLiteral("ok"),
             QString::number(objectPackage.assetGroups.size()));

    std::map<QString, int> familyCounts;
    for (const PdkOperatorView& op : objectPackage.operators) {
        ++familyCounts[op.operatorFamily];
    }
    for (const QString& family :
         {QStringLiteral("state_transition"), QStringLiteral("observation_equation"),
          QStringLiteral("filter_algorithm"), QStringLiteral("qoi")}) {
        const int count = familyCounts[family];
        addCheck(preTable,
                 QStringLiteral("算子族 · %1").arg(family),
                 count > 0 ? QStringLiteral("ok") : QStringLiteral("warn"),
                 QString::number(count));
    }
    addCheck(preTable,
             QStringLiteral("工作流规格"),
             objectPackage.workflows.isEmpty() ? QStringLiteral("fail") : QStringLiteral("ok"),
             QString::number(objectPackage.workflows.size()));

    int compiledOk = 0;
    for (const PdkWorkflowView& workflow : objectPackage.workflows) {
        if (QFileInfo::exists(compiledRoot.filePath(workflow.workflowId))) {
            ++compiledOk;
        }
    }
    addCheck(preTable,
             QStringLiteral("已编译工作流"),
             compiledOk == objectPackage.workflows.size() ? QStringLiteral("ok") : QStringLiteral("warn"),
             QStringLiteral("%1 / %2").arg(compiledOk).arg(objectPackage.workflows.size()));
    addCheck(preTable,
             QStringLiteral("运行时证据"),
             runDirs.isEmpty() ? QStringLiteral("warn") : QStringLiteral("ok"),
             QString::number(runDirs.size()));
    preflightPanel->bodyLayout()->addWidget(preTable);
    body->addWidget(preflightPanel, 1);

    auto* runtimePanel = new Panel(QStringLiteral("运行时节点"), this);
    runtimePanel->setSubtitle(QStringLiteral("runtime_node_snapshot.json"));
    auto* rtTable = makeTable(
        {QStringLiteral("运行"), QStringLiteral("节点"), QStringLiteral("算子"), QStringLiteral("算子族"),
         QStringLiteral("状态")},
        runtimePanel->body());
    for (const QString& runDir : runDirs) {
        const QJsonObject snapshot =
            readJsonObject(QDir(runDir).filePath(QStringLiteral("runtime_node_snapshot.json")));
        for (const QJsonValue& value : snapshot.value(QStringLiteral("nodes")).toArray()) {
            const QJsonObject node = value.toObject();
            const int row = rtTable->rowCount();
            rtTable->insertRow(row);
            rtTable->setItem(row, 0, new QTableWidgetItem(QFileInfo(runDir).fileName()));
            rtTable->setItem(row, 1, new QTableWidgetItem(node.value(QStringLiteral("node_id")).toString()));
            rtTable->setItem(row, 2, new QTableWidgetItem(node.value(QStringLiteral("operator_id")).toString()));
            rtTable->setItem(row, 3, new QTableWidgetItem(node.value(QStringLiteral("operator_family")).toString()));
            setBadgeCell(rtTable, row, 4, node.value(QStringLiteral("status")).toString(QStringLiteral("ok")));
        }
    }
    if (rtTable->rowCount() == 0) {
        rtTable->insertRow(0);
        rtTable->setItem(0, 0, new QTableWidgetItem(QStringLiteral("尚无运行时证据")));
    }
    runtimePanel->bodyLayout()->addWidget(rtTable);
    body->addWidget(runtimePanel, 1);

    root->addLayout(body, 1);
}

} // namespace twin
