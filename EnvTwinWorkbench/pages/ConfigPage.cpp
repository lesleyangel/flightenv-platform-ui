#include "ConfigPage.h"

#include "../datahub/LegacyRunCatalogSource.h"
#include "../datahub/PdkUiReaders.h"
#include "../widgets/KvList.h"
#include "../widgets/Panel.h"
#include "../widgets/PageHeader.h"
#include "../widgets/StatusUtil.h"

#include <QHBoxLayout>
#include <QJsonArray>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <set>
#include <string>

namespace twin {

ConfigPage::ConfigPage(
    LegacyRunCatalogSource* legacyRunCatalog,
    QString evidenceRoot,
    QString objectPackageRoot,
    QWidget* parent)
    : QWidget(parent), legacyRunCatalog_(legacyRunCatalog) {
    const PdkObjectPackageView objectPackage = PdkObjectPackageReader().read(objectPackageRoot);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);
    root->addWidget(makePageHeader(
        QStringLiteral("配置与数据源"),
        QStringLiteral("当前数字孪生的数据源、catalog 与锁定资产 · SDK 只读视图"), this));

    // ---- 运行配置 ----
    auto* cfgPanel = new Panel(QStringLiteral("运行配置"), this);
    auto* cfgKv = new KvList(cfgPanel->body());
    cfgKv->addRow(QStringLiteral("主真源"), objectPackage.ok() ? QStringLiteral("对象包") : QStringLiteral("对象包未加载"), false);
    cfgKv->addRow(QStringLiteral("对象包路径"), objectPackageRoot.isEmpty() ? QStringLiteral("—") : objectPackageRoot);
    cfgKv->addRow(QStringLiteral("对象ID"), objectPackage.objectId.isEmpty() ? QStringLiteral("—") : objectPackage.objectId);
    cfgKv->addRow(QStringLiteral("旧 run catalog"), legacyRunCatalog->ok() ? QStringLiteral("可用，仅用于历史 run/兼容索引") : QStringLiteral("未连接"), false);
    cfgKv->addRow(QStringLiteral("旧 catalog 路径"), legacyRunCatalog->catalogPath().isEmpty() ? QStringLiteral("—") : legacyRunCatalog->catalogPath());
    cfgKv->addRow(QStringLiteral("evidence 输出根"), evidenceRoot.isEmpty() ? QStringLiteral("—") : evidenceRoot);
    cfgKv->addRow(QStringLiteral("RMW 实现"), QStringLiteral("rmw_cyclonedds_cpp"), false);
    cfgKv->addRow(QStringLiteral("对象包组件数"), QString::number(objectPackage.twinObjectJson.value(QStringLiteral("components")).toArray().size()));
    cfgKv->addRow(QStringLiteral("对象包资源组数"), QString::number(objectPackage.assetGroups.size()));
    cfgKv->addRow(QStringLiteral("对象包算子数"), QString::number(objectPackage.operators.size()));
    cfgKv->addRow(QStringLiteral("对象包 workflow 数"), QString::number(objectPackage.workflows.size()));
    cfgKv->addRow(QStringLiteral("历史 run 数"), QString::number(legacyRunCatalog->ok() ? legacyRunCatalog->runs().size() : 0));
    cfgPanel->bodyLayout()->addWidget(cfgKv);
    root->addWidget(cfgPanel);

    // ---- 锁定数据资产（汇总 runs 的 resource_locks）----
    auto* assetPanel = new Panel(QStringLiteral("锁定数据资产"), this);
    assetPanel->setSubtitle(QStringLiteral("resource_lock 去重汇总"));
    auto* assetTable = makeTable({QStringLiteral("role"), QStringLiteral("resource_id"), QStringLiteral("schema")}, assetPanel->body());
    std::set<std::string> seen;
    if (legacyRunCatalog->ok()) {
        for (const auto& r : legacyRunCatalog->runs()) {
            for (const auto& lock : r.resource_locks) {
                const std::string key = lock.resource_type + "|" + lock.resource_id;
                if (!seen.insert(key).second) continue;
                const int row = assetTable->rowCount();
                assetTable->insertRow(row);
                assetTable->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(lock.resource_type)));
                assetTable->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(lock.resource_id)));
                assetTable->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(lock.schema_version)));
            }
        }
    }
    if (assetTable->rowCount() == 0) {
        assetTable->insertRow(0);
        assetTable->setItem(0, 0, new QTableWidgetItem(QStringLiteral("—")));
        assetTable->setItem(0, 1, new QTableWidgetItem(QStringLiteral("暂无锁定资产记录（先跑一次 run）")));
    }
    assetPanel->bodyLayout()->addWidget(assetTable);
    root->addWidget(assetPanel, 1);
}

} // namespace twin
