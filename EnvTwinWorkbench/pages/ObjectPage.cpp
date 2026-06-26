#include "ObjectPage.h"

#include "../datahub/AsyncFieldArtifactLoader.h"
#include "../widgets/KvList.h"
#include "../widgets/Panel.h"
#include "../widgets/PageHeader.h"
#include "../widgets/StatusUtil.h"
#include "../widgets/VtkModelFieldWidget.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace twin {

using flightenv::ui::demo::VtkModelFieldWidget;

namespace {

QString joinArray(const QJsonValue& value) {
    QStringList items;
    for (const QJsonValue& item : value.toArray()) {
        if (item.isString()) {
            items << item.toString();
        }
    }
    return items.isEmpty() ? QStringLiteral("—") : items.join(QStringLiteral("\n"));
}

QString jsonValueText(const QJsonValue& value) {
    if (value.isString()) {
        return value.toString();
    }
    if (value.isBool()) {
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }
    if (value.isDouble()) {
        return QString::number(value.toDouble(), 'g', 12);
    }
    if (value.isArray()) {
        return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
    }
    if (value.isObject()) {
        return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
    }
    if (value.isNull()) {
        return QStringLiteral("null");
    }
    return QStringLiteral("—");
}

QJsonObject findResourceById(const PdkObjectPackageView& objectPackage, const QString& resourceId) {
    for (const QJsonValue& value : objectPackage.twinObjectJson.value(QStringLiteral("resources")).toArray()) {
        const QJsonObject resource = value.toObject();
        if (resource.value(QStringLiteral("resource_id")).toString() == resourceId) {
            return resource;
        }
    }
    return {};
}

QString resourceLabel(const QJsonObject& resource, const QString& fallbackId) {
    const QString resourceId = resource.value(QStringLiteral("resource_id")).toString(fallbackId);
    const QString type = resource.value(QStringLiteral("resource_type")).toString();
    return type.isEmpty() ? resourceId : QStringLiteral("%1 · %2").arg(resourceId, type);
}

QString operatorPortsText(const QVector<PdkPortView>& ports) {
    QStringList out;
    for (const PdkPortView& port : ports) {
        out << QStringLiteral("%1(%2)").arg(port.portId, port.contractId);
    }
    return out.isEmpty() ? QStringLiteral("—") : out.join(QStringLiteral("\n"));
}

QTreeWidgetItem* makeItem(const QString& label, const QString& id, const QString& kind) {
    auto* item = new QTreeWidgetItem();
    item->setText(0, label);
    item->setData(0, Qt::UserRole, id);
    item->setData(0, Qt::UserRole + 1, kind);
    return item;
}

} // namespace

ObjectPage::ObjectPage(
    QString objectPackageRoot,
    QWidget* parent)
    : QWidget(parent),
      objectPackage_(PdkObjectPackageReader().read(objectPackageRoot)),
      objectPackageRoot_(objectPackageRoot) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);
    root->addWidget(makePageHeader(
        QStringLiteral("对象画像"),
        QStringLiteral("对象包驱动：组件、资源、传感器、模型绑定随对象动态变化"), this));

    auto* twoCol = new QHBoxLayout();
    twoCol->setSpacing(12);

    auto* treePanel = new Panel(QStringLiteral("对象包 / 组件 / 资源组"), this);
    treePanel->setMinimumWidth(280);
    treePanel->setMaximumWidth(400);
    tree_ = new QTreeWidget(treePanel->body());
    tree_->setHeaderHidden(true);
    tree_->setFocusPolicy(Qt::NoFocus);
    tree_->setStyleSheet(QStringLiteral("QTreeWidget{border:none;background:#ffffff;}"));

    if (objectPackage_.ok()) {
        const QString objectId = objectPackage_.objectId.isEmpty()
            ? objectPackage_.twinObjectJson.value(QStringLiteral("object_id")).toString()
            : objectPackage_.objectId;
        auto* objectItem = makeItem(
            objectPackage_.twinObjectJson.value(QStringLiteral("object_type")).toString(objectId),
            objectId,
            QStringLiteral("object"));
        tree_->addTopLevelItem(objectItem);

        auto* componentsRoot = makeItem(QStringLiteral("Components"), QString(), QStringLiteral("group"));
        objectItem->addChild(componentsRoot);
        for (const QJsonValue& value : objectPackage_.twinObjectJson.value(QStringLiteral("components")).toArray()) {
            const QJsonObject component = value.toObject();
            const QString componentId = component.value(QStringLiteral("component_id")).toString();
            componentsRoot->addChild(makeItem(
                component.value(QStringLiteral("display_name")).toString(componentId),
                componentId,
                QStringLiteral("component")));
        }

        auto* assetsRoot = makeItem(QStringLiteral("Asset groups"), QString(), QStringLiteral("group"));
        objectItem->addChild(assetsRoot);
        for (const PdkAssetGroupView& group : objectPackage_.assetGroups) {
            auto* groupItem = makeItem(
                QStringLiteral("%1 (%2)").arg(group.groupId).arg(group.resourceIds.size()),
                group.groupId,
                QStringLiteral("asset_group"));
            assetsRoot->addChild(groupItem);
            for (const QString& resourceId : group.resourceIds) {
                const QJsonObject resource = findResourceById(objectPackage_, resourceId);
                groupItem->addChild(makeItem(
                    resourceLabel(resource, resourceId),
                    resourceId,
                    QStringLiteral("resource")));
            }
        }
        tree_->expandAll();
    } else {
        auto* placeholder = makeItem(QStringLiteral("对象包读取失败"), QString(), QStringLiteral("object"));
        tree_->addTopLevelItem(placeholder);
    }

    connect(tree_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* cur, QTreeWidgetItem*) {
        if (!cur) {
            return;
        }
        const QString kind = cur->data(0, Qt::UserRole + 1).toString();
        const QString id = cur->data(0, Qt::UserRole).toString();
        if (kind == QStringLiteral("component")) {
            showComponentDetail(id);
            showSensorTable(id);
            renderLayout(layoutIdForComponent(id));
        } else if (kind == QStringLiteral("asset_group")) {
            showAssetGroupDetail(id);
            showResourceListTable(id);
        } else if (kind == QStringLiteral("resource")) {
            showResourceDetail(id);
            showResourceUsageTable(id);
            const QJsonObject resource = findResourceById(objectPackage_, id);
            const QString component = resource.value(QStringLiteral("component_id")).toString();
            const QString layoutId = layoutIdForResource(id);
            renderLayout(layoutId.isEmpty() ? layoutIdForComponent(component) : layoutId);
        } else if (kind == QStringLiteral("object")) {
            showDetail(id);
            showSensorTable(QString());
            renderLayout({});
        }
    });
    treePanel->bodyLayout()->addWidget(tree_);
    twoCol->addWidget(treePanel);

    // 中列做成纵向：上=对象包详情(KV)，下=上下文表(传感器布局 / 数据库内容)。
    auto* midWidget = new QWidget(this);
    midWidget->setMinimumWidth(340);
    auto* midCol = new QVBoxLayout(midWidget);
    midCol->setContentsMargins(0, 0, 0, 0);
    midCol->setSpacing(12);

    auto* detailPanel = new Panel(QStringLiteral("对象包详情"), this);
    detailTitle_ = new QLabel(QStringLiteral("选择对象包节点"), detailPanel->body());
    detailTitle_->setStyleSheet(QStringLiteral("font-weight:700;font-size:14px;color:#1b1f27;"));
    detailPanel->bodyLayout()->addWidget(detailTitle_);
    detailKv_ = new KvList(detailPanel->body());
    detailPanel->bodyLayout()->addWidget(detailKv_);

    auto* boundTitle = new QLabel(QStringLiteral("关联模型 / 资源"), detailPanel->body());
    boundTitle->setStyleSheet(QStringLiteral("font-weight:600;font-size:11px;color:#79818f;margin-top:6px;"));
    detailPanel->bodyLayout()->addWidget(boundTitle);
    boundModels_ = new QLabel(QStringLiteral("—"), detailPanel->body());
    boundModels_->setProperty("mono", true);
    boundModels_->setWordWrap(true);
    detailPanel->bodyLayout()->addWidget(boundModels_);
    midCol->addWidget(detailPanel);

    auto* contextPanel = new Panel(QStringLiteral("资源关系 / 数据内容"), this);
    contextPanel->setSubtitle(QStringLiteral("随选中节点切换：资源清单 / 算子引用 / 传感器布局 / 数据库内容"));
    contextTitle_ = new QLabel(QStringLiteral("选择资源组查看资源清单，选择单个资源查看算子引用"), contextPanel->body());
    contextTitle_->setWordWrap(true);
    contextTitle_->setProperty("muted", true);
    contextPanel->bodyLayout()->addWidget(contextTitle_);
    contextTable_ = makeTable({QStringLiteral("—")}, contextPanel->body());
    contextTable_->setMinimumHeight(200);
    contextPanel->bodyLayout()->addWidget(contextTable_, 1);
    midCol->addWidget(contextPanel, 1);

    twoCol->addWidget(midWidget, 1);

    // 第三列：真实组件网格三维预览（来自对象包 mesh layout + 兼容节点布局资产）。
    auto* fieldPanel = new Panel(QStringLiteral("真实组件网格"), this);
    fieldPanel->setSubtitle(QStringLiteral("对象包 mesh/layout/resource · 选中组件或资源切换布局"));
    fieldPanel->setMinimumWidth(380);
    fieldHint_ = new QLabel(QStringLiteral("等运行 evidence 后显示真实网格"), fieldPanel->body());
    fieldHint_->setWordWrap(true);
    fieldHint_->setProperty("muted", true);
    fieldPanel->bodyLayout()->addWidget(fieldHint_);
    fieldWidget_ = new VtkModelFieldWidget(fieldPanel->body());
    fieldWidget_->setMinimumHeight(360);
    fieldWidget_->setAssetRoot(objectPackageRoot_);
    fieldWidget_->clearField(QStringLiteral("正在从对象包解析真实网格布局..."));
    fieldPanel->bodyLayout()->addWidget(fieldWidget_, 1);
    twoCol->addWidget(fieldPanel, 1);

    root->addLayout(twoCol, 1);

    if (tree_->topLevelItemCount() > 0) {
        tree_->setCurrentItem(tree_->topLevelItem(0));
    }
}

void ObjectPage::setEvidenceRoot(const QString& evidenceRoot) {
    const QString clean = QDir::fromNativeSeparators(evidenceRoot);
    if (clean == evidenceRoot_ && meshCatalog_.has_value()) {
        return;
    }
    evidenceRoot_ = clean;
    meshCatalog_.reset();
    if (!fieldWidget_) {
        return;
    }
    const QString snapshotPath = !evidenceRoot_.isEmpty() && QFileInfo::exists(QDir(evidenceRoot_).filePath(QStringLiteral("runtime_snapshot.json")))
        ? QDir(evidenceRoot_).filePath(QStringLiteral("runtime_snapshot.json"))
        : QString();
    fieldWidget_->clearField(QStringLiteral("对象包真实网格布局后台加载中..."));
    loadMeshLayoutCatalogAsync(this, objectPackageRoot_, snapshotPath, [this](LoadedMeshLayoutCatalog loaded) {
        if (!loaded.ok) {
            if (fieldWidget_) {
                fieldWidget_->clearField(loaded.message.isEmpty()
                    ? QStringLiteral("mesh layout catalog 读取失败")
                    : loaded.message);
            }
            return;
        }
        meshCatalog_ = loaded.catalog;
        if (fieldWidget_) {
            fieldWidget_->setMeshLayoutCatalog(*meshCatalog_);
        }
        renderLayout(currentLayoutId_);
    });
}

void ObjectPage::renderLayout(const QString& layoutId) {
    currentLayoutId_ = layoutId;
    if (!fieldWidget_) {
        return;
    }
    if (!meshCatalog_.has_value()) {
        fieldWidget_->clearField(QStringLiteral("对象包真实网格布局加载中"));
        if (fieldHint_) {
            fieldHint_->setText(QStringLiteral("对象包真实网格布局加载中"));
        }
        return;
    }
    QString target = layoutId;
    if (target.isEmpty() && !meshCatalog_->layouts.isEmpty()) {
        target = meshCatalog_->layouts.front().layoutId;
    }
    const auto stats = fieldWidget_->renderGeometryPreview(
        target, QStringLiteral("真实组件网格 · %1").arg(target));
    if (fieldHint_) {
        fieldHint_->setText(stats.ok
            ? QStringLiteral("%1 · 节点 %2 · 顶点 %3").arg(target).arg(stats.nodeCount).arg(stats.vertexCount)
            : stats.message);
    }
}

QString ObjectPage::layoutIdForComponent(const QString& componentId) const {
    if (!meshCatalog_.has_value()) {
        return {};
    }
    for (const PlatformMeshLayoutView& layout : meshCatalog_->layouts) {
        if (layout.componentId == componentId) {
            return layout.layoutId;
        }
    }
    return meshCatalog_->layouts.isEmpty() ? QString() : meshCatalog_->layouts.front().layoutId;
}

QString ObjectPage::layoutIdForResource(const QString& resourceId) const {
    if (!meshCatalog_.has_value()) {
        return {};
    }
    for (const PlatformMeshLayoutView& layout : meshCatalog_->layouts) {
        if (layout.meshRef == resourceId || layout.aliases.contains(resourceId.trimmed().toLower())) {
            return layout.layoutId;
        }
    }
    return {};
}

void ObjectPage::showDetail(const QString&) {
    detailTitle_->setText(objectPackage_.objectId.isEmpty() ? QStringLiteral("Object Package") : objectPackage_.objectId);
    detailKv_->clear();
    detailKv_->addRow(QStringLiteral("object_id"), objectPackage_.twinObjectJson.value(QStringLiteral("object_id")).toString());
    detailKv_->addRow(QStringLiteral("object_type"), objectPackage_.twinObjectJson.value(QStringLiteral("object_type")).toString(), false);
    detailKv_->addRow(QStringLiteral("schema_version"), objectPackage_.twinObjectJson.value(QStringLiteral("schema_version")).toString());
    detailKv_->addRow(QStringLiteral("components"), QString::number(objectPackage_.twinObjectJson.value(QStringLiteral("components")).toArray().size()));
    detailKv_->addRow(QStringLiteral("resources"), QString::number(objectPackage_.twinObjectJson.value(QStringLiteral("resources")).toArray().size()));
    detailKv_->addRow(QStringLiteral("operators"), QString::number(objectPackage_.operators.size()));
    detailKv_->addRow(QStringLiteral("workflows"), QString::number(objectPackage_.workflows.size()));
    detailKv_->addRow(QStringLiteral("object_qoi_schema_ids"), joinArray(objectPackage_.twinObjectJson.value(QStringLiteral("object_qoi_schema_ids"))), false);

    QStringList issueLines;
    for (const PdkReadIssue& issue : objectPackage_.issues) {
        issueLines << QStringLiteral("%1 · %2 · %3").arg(issue.severity, issue.code, issue.path);
    }
    boundModels_->setText(issueLines.isEmpty() ? QStringLiteral("对象包 preflight: ok") : issueLines.join(QStringLiteral("\n")));
}

void ObjectPage::showComponentDetail(const QString& componentId) {
    for (const QJsonValue& value : objectPackage_.twinObjectJson.value(QStringLiteral("components")).toArray()) {
        const QJsonObject component = value.toObject();
        if (component.value(QStringLiteral("component_id")).toString() != componentId) {
            continue;
        }
        detailTitle_->setText(component.value(QStringLiteral("display_name")).toString(componentId));
        detailKv_->clear();
        detailKv_->addRow(QStringLiteral("component_id"), componentId);
        detailKv_->addRow(QStringLiteral("mesh_resource_ids"), joinArray(component.value(QStringLiteral("mesh_resource_ids"))), false);
        detailKv_->addRow(QStringLiteral("mesh_mapping_resource_ids"), joinArray(component.value(QStringLiteral("mesh_mapping_resource_ids"))), false);
        detailKv_->addRow(QStringLiteral("sensor_group_ids"), joinArray(component.value(QStringLiteral("sensor_group_ids"))), false);
        detailKv_->addRow(QStringLiteral("latent_state_schema_ids"), joinArray(component.value(QStringLiteral("latent_state_schema_ids"))), false);
        detailKv_->addRow(QStringLiteral("field_state_schema_ids"), joinArray(component.value(QStringLiteral("field_state_schema_ids"))), false);
        detailKv_->addRow(QStringLiteral("accumulated_state_schema_ids"), joinArray(component.value(QStringLiteral("accumulated_state_schema_ids"))), false);
        detailKv_->addRow(QStringLiteral("observation_schema_ids"), joinArray(component.value(QStringLiteral("observation_schema_ids"))), false);
        detailKv_->addRow(QStringLiteral("qoi_schema_ids"), joinArray(component.value(QStringLiteral("qoi_schema_ids"))), false);
        boundModels_->setText(joinArray(component.value(QStringLiteral("model_binding_ids"))));
        return;
    }
}

void ObjectPage::showAssetGroupDetail(const QString& groupId) {
    detailTitle_->setText(groupId);
    detailKv_->clear();
    detailKv_->addRow(QStringLiteral("group_id"), groupId);
    for (const PdkAssetGroupView& group : objectPackage_.assetGroups) {
        if (group.groupId == groupId) {
            detailKv_->addRow(QStringLiteral("resource_count"), QString::number(group.resourceIds.size()));
            detailKv_->addRow(QStringLiteral("resources"), group.resourceIds.join(QStringLiteral("\n")), false);
            boundModels_->setText(QStringLiteral("展开左侧资源组，或点击下方资源清单，可查看每个模型/数据/网格/传感器的完整字段和算子引用。"));
            return;
        }
    }
}

void ObjectPage::showResourceDetail(const QString& resourceId) {
    const QJsonObject resource = findResourceById(objectPackage_, resourceId);
    detailTitle_->setText(resourceId);
    detailKv_->clear();
    if (resource.isEmpty()) {
        detailKv_->addRow(QStringLiteral("resource_id"), resourceId);
        detailKv_->addRow(QStringLiteral("状态"), QStringLiteral("对象包 resources[] 中未找到该资源"), false);
        boundModels_->setText(QStringLiteral("该资源可能只出现在 assets/resources.json 分组中，缺少 object/twin_object.json 资源记录。"));
        return;
    }

    QStringList keys = resource.keys();
    std::sort(keys.begin(), keys.end());
    const QStringList priority = {
        QStringLiteral("resource_id"),
        QStringLiteral("resource_type"),
        QStringLiteral("component_id"),
        QStringLiteral("model_id"),
        QStringLiteral("legacy_model_id"),
        QStringLiteral("adapter_backend"),
        QStringLiteral("uri"),
        QStringLiteral("input_contract_ids"),
        QStringLiteral("output_contract_id"),
        QStringLiteral("output_contract_ids"),
        QStringLiteral("basis_ref"),
        QStringLiteral("source_database_ref"),
        QStringLiteral("model_package_root"),
        QStringLiteral("pod_text_dump_dir"),
        QStringLiteral("pred_train_model_dir"),
        QStringLiteral("pod_model_dir")
    };
    for (const QString& key : priority) {
        if (resource.contains(key)) {
            detailKv_->addRow(key, jsonValueText(resource.value(key)), false);
            keys.removeAll(key);
        }
    }
    for (const QString& key : keys) {
        detailKv_->addRow(key, jsonValueText(resource.value(key)), false);
    }

    QStringList operators;
    for (const PdkOperatorView& op : objectPackage_.operators) {
        if (op.resourceRefs.contains(resourceId)) {
            operators << QStringLiteral("%1 · %2").arg(op.operatorFamily, op.operatorId);
        }
    }
    boundModels_->setText(operators.isEmpty()
        ? QStringLiteral("暂无 AtomicOperator 通过 resource_refs 引用该资源")
        : operators.join(QStringLiteral("\n")));
}

void ObjectPage::clearContext(const QString& hint) {
    if (contextTitle_) {
        contextTitle_->setText(hint);
    }
    if (contextTable_) {
        contextTable_->clear();
        contextTable_->setColumnCount(1);
        contextTable_->setHorizontalHeaderLabels({QStringLiteral("—")});
        contextTable_->setRowCount(0);
    }
}

void ObjectPage::showResourceListTable(const QString& groupId) {
    if (!contextTable_) {
        return;
    }
    const QStringList headers = {
        QStringLiteral("resource_id"),
        QStringLiteral("类型"),
        QStringLiteral("组件"),
        QStringLiteral("模型/后端"),
        QStringLiteral("输入契约"),
        QStringLiteral("输出契约"),
        QStringLiteral("uri")
    };
    contextTable_->clear();
    contextTable_->setColumnCount(headers.size());
    contextTable_->setHorizontalHeaderLabels(headers);
    contextTable_->setRowCount(0);

    int count = 0;
    for (const PdkAssetGroupView& group : objectPackage_.assetGroups) {
        if (group.groupId != groupId) {
            continue;
        }
        for (const QString& resourceId : group.resourceIds) {
            const QJsonObject resource = findResourceById(objectPackage_, resourceId);
            const int row = contextTable_->rowCount();
            contextTable_->insertRow(row);
            contextTable_->setItem(row, 0, new QTableWidgetItem(resourceId));
            contextTable_->setItem(row, 1, new QTableWidgetItem(resource.value(QStringLiteral("resource_type")).toString(QStringLiteral("—"))));
            contextTable_->setItem(row, 2, new QTableWidgetItem(resource.value(QStringLiteral("component_id")).toString(QStringLiteral("—"))));
            const QString modelText = resource.value(QStringLiteral("model_id")).toString(
                resource.value(QStringLiteral("adapter_backend")).toString(QStringLiteral("—")));
            contextTable_->setItem(row, 3, new QTableWidgetItem(modelText));
            contextTable_->setItem(row, 4, new QTableWidgetItem(jsonValueText(resource.value(QStringLiteral("input_contract_ids")))));
            const QString outputs = resource.contains(QStringLiteral("output_contract_ids"))
                ? jsonValueText(resource.value(QStringLiteral("output_contract_ids")))
                : resource.value(QStringLiteral("output_contract_id")).toString(QStringLiteral("—"));
            contextTable_->setItem(row, 5, new QTableWidgetItem(outputs));
            contextTable_->setItem(row, 6, new QTableWidgetItem(resource.value(QStringLiteral("uri")).toString(QStringLiteral("—"))));
            ++count;
        }
        break;
    }
    if (count == 0) {
        contextTable_->insertRow(0);
        contextTable_->setItem(0, 0, new QTableWidgetItem(QStringLiteral("该资源组为空或未找到")));
    }
    contextTable_->resizeColumnsToContents();
    contextTable_->horizontalHeader()->setStretchLastSection(true);
    if (contextTitle_) {
        contextTitle_->setText(QStringLiteral("资源组 %1 · %2 个资源。点击左侧具体资源可看完整字段和算子引用。")
            .arg(groupId)
            .arg(count));
    }
}

void ObjectPage::showResourceUsageTable(const QString& resourceId) {
    if (!contextTable_) {
        return;
    }
    const QStringList headers = {
        QStringLiteral("算子ID"),
        QStringLiteral("算子族"),
        QStringLiteral("阶段"),
        QStringLiteral("输入端口/契约"),
        QStringLiteral("输出端口/契约"),
        QStringLiteral("展示组件")
    };
    contextTable_->clear();
    contextTable_->setColumnCount(headers.size());
    contextTable_->setHorizontalHeaderLabels(headers);
    contextTable_->setRowCount(0);

    int count = 0;
    for (const PdkOperatorView& op : objectPackage_.operators) {
        if (!op.resourceRefs.contains(resourceId)) {
            continue;
        }
        const int row = contextTable_->rowCount();
        contextTable_->insertRow(row);
        contextTable_->setItem(row, 0, new QTableWidgetItem(op.operatorId));
        contextTable_->setItem(row, 1, new QTableWidgetItem(op.operatorFamily));
        contextTable_->setItem(row, 2, new QTableWidgetItem(op.phases.join(QStringLiteral("\n"))));
        contextTable_->setItem(row, 3, new QTableWidgetItem(operatorPortsText(op.inputs)));
        contextTable_->setItem(row, 4, new QTableWidgetItem(operatorPortsText(op.outputs)));
        contextTable_->setItem(row, 5, new QTableWidgetItem(op.display.rendererId));
        ++count;
    }
    if (count == 0) {
        contextTable_->insertRow(0);
        contextTable_->setItem(0, 0, new QTableWidgetItem(QStringLiteral("暂无算子通过 resource_refs 引用该资源")));
    }
    contextTable_->resizeColumnsToContents();
    contextTable_->horizontalHeader()->setStretchLastSection(true);
    if (contextTitle_) {
        contextTitle_->setText(QStringLiteral("资源 %1 的算子引用 · %2 个").arg(resourceId).arg(count));
    }
}

void ObjectPage::showSensorTable(const QString& componentFilter) {
    if (!contextTable_) {
        return;
    }
    const QStringList headers = {
        QStringLiteral("传感器ID"), QStringLiteral("类型"), QStringLiteral("组件"),
        QStringLiteral("观测场契约"), QStringLiteral("输出契约"),
        QStringLiteral("采样率Hz"), QStringLiteral("量程")};
    contextTable_->clear();
    contextTable_->setColumnCount(headers.size());
    contextTable_->setHorizontalHeaderLabels(headers);
    contextTable_->setRowCount(0);

    int count = 0;
    for (const QJsonValue& v : objectPackage_.twinObjectJson.value(QStringLiteral("resources")).toArray()) {
        const QJsonObject r = v.toObject();
        if (r.value(QStringLiteral("resource_type")).toString() != QStringLiteral("sensor_layout")) {
            continue;
        }
        const QString comp = r.value(QStringLiteral("component_id")).toString();
        if (!componentFilter.isEmpty() && comp != componentFilter) {
            continue;
        }
        const QString rid = r.value(QStringLiteral("resource_id")).toString();
        const QStringList parts = rid.split(QLatin1Char('.'));
        const QString type = parts.size() >= 2 ? parts.at(1) : rid;
        QString range = QStringLiteral("—");
        const QJsonArray vr = r.value(QStringLiteral("valid_range")).toArray();
        if (vr.size() == 2) {
            range = QStringLiteral("[%1, %2]").arg(vr.at(0).toDouble()).arg(vr.at(1).toDouble());
        }
        const int row = contextTable_->rowCount();
        contextTable_->insertRow(row);
        contextTable_->setItem(row, 0, new QTableWidgetItem(rid));
        contextTable_->setItem(row, 1, new QTableWidgetItem(type));
        contextTable_->setItem(row, 2, new QTableWidgetItem(comp));
        contextTable_->setItem(row, 3, new QTableWidgetItem(
            r.value(QStringLiteral("observed_field_contract_id")).toString(QStringLiteral("—"))));
        contextTable_->setItem(row, 4, new QTableWidgetItem(r.value(QStringLiteral("output_contract_id")).toString()));
        contextTable_->setItem(row, 5, new QTableWidgetItem(QString::number(r.value(QStringLiteral("sampling_rate_hz")).toDouble())));
        contextTable_->setItem(row, 6, new QTableWidgetItem(range));
        ++count;
    }
    if (count == 0) {
        contextTable_->insertRow(0);
        contextTable_->setItem(0, 0, new QTableWidgetItem(QStringLiteral("该选择下无传感器布局")));
    }
    contextTable_->resizeColumnsToContents();
    contextTable_->horizontalHeader()->setStretchLastSection(true);
    if (contextTitle_) {
        contextTitle_->setText(QStringLiteral(
            "传感器布局 · %1 个%2。注：传感器节点坐标未由运行时导出（runtime_snapshot.sensor_layouts 为空），"
            "此处先列布局元数据；几何位置见右侧组件网格（按所属组件着色）。")
            .arg(count)
            .arg(componentFilter.isEmpty() ? QString() : QStringLiteral("（组件 %1）").arg(componentFilter)));
    }
}

void ObjectPage::showDatabaseContent() {
    if (!contextTable_) {
        return;
    }
    QJsonObject dataset;
    QString dbProvides;
    for (const QJsonValue& v : objectPackage_.twinObjectJson.value(QStringLiteral("resources")).toArray()) {
        const QJsonObject r = v.toObject();
        const QString type = r.value(QStringLiteral("resource_type")).toString();
        if (type == QStringLiteral("trajectory_replay_dataset") && dataset.isEmpty()) {
            dataset = r;
        } else if (type == QStringLiteral("database")) {
            QStringList provides;
            for (const QJsonValue& p : r.value(QStringLiteral("provides")).toArray()) {
                provides << p.toString();
            }
            dbProvides = provides.join(QStringLiteral(", "));
        }
    }
    if (dataset.isEmpty()) {
        clearContext(QStringLiteral("数据库组无可直接读取的数据集（catalog:// 资源需平台 catalog 解析）"));
        return;
    }

    QString rel = dataset.value(QStringLiteral("uri")).toString();
    if (rel.startsWith(QStringLiteral("object://"))) {
        rel = rel.mid(QStringLiteral("object://").size());
    }
    const QStringList candidates = {
        QDir(objectPackageRoot_).filePath(rel),
        QDir(objectPackageRoot_).filePath(QStringLiteral("../") + rel),
        rel};
    QString path;
    for (const QString& c : candidates) {
        if (QFileInfo::exists(c)) {
            path = c;
            break;
        }
    }
    if (path.isEmpty()) {
        clearContext(QStringLiteral("数据集文件未找到：%1").arg(dataset.value(QStringLiteral("uri")).toString()));
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        clearContext(QStringLiteral("数据集打开失败：%1").arg(path));
        return;
    }
    const QJsonObject doc = QJsonDocument::fromJson(file.readAll()).object();
    const QJsonArray samples = doc.value(QStringLiteral("samples")).toArray();

    const QStringList headers = {
        QStringLiteral("序号"), QStringLiteral("taskpoint"), QStringLiteral("t(s)"),
        QStringLiteral("h(m)"), QStringLiteral("Ma"), QStringLiteral("alpha"),
        QStringLiteral("q"), QStringLiteral("pos_z")};
    contextTable_->clear();
    contextTable_->setColumnCount(headers.size());
    contextTable_->setHorizontalHeaderLabels(headers);
    contextTable_->setRowCount(0);

    int shown = 0;
    for (const QJsonValue& sv : samples) {
        if (shown >= 500) {
            break;
        }
        const QJsonObject s = sv.toObject();
        const auto num = [&s](const char* key, int prec) {
            return QString::number(s.value(QLatin1String(key)).toDouble(), 'f', prec);
        };
        const int row = contextTable_->rowCount();
        contextTable_->insertRow(row);
        contextTable_->setItem(row, 0, new QTableWidgetItem(QString::number(s.value(QStringLiteral("sample_index")).toInt())));
        contextTable_->setItem(row, 1, new QTableWidgetItem(QString::number(s.value(QStringLiteral("taskpoint_id")).toInt())));
        contextTable_->setItem(row, 2, new QTableWidgetItem(num("time", 2)));
        contextTable_->setItem(row, 3, new QTableWidgetItem(num("h", 1)));
        contextTable_->setItem(row, 4, new QTableWidgetItem(num("ma", 4)));
        contextTable_->setItem(row, 5, new QTableWidgetItem(num("alpha", 3)));
        contextTable_->setItem(row, 6, new QTableWidgetItem(num("q", 2)));
        contextTable_->setItem(row, 7, new QTableWidgetItem(num("pos_z", 1)));
        ++shown;
    }
    contextTable_->resizeColumnsToContents();
    contextTable_->horizontalHeader()->setStretchLastSection(true);
    if (contextTitle_) {
        contextTitle_->setText(QStringLiteral("数据库内容 · %1 · 样本 %2/%3 · 源库 %4 · 逻辑表: %5")
            .arg(doc.value(QStringLiteral("trajectory_id")).toString(dataset.value(QStringLiteral("resource_id")).toString()))
            .arg(shown).arg(samples.size())
            .arg(dataset.value(QStringLiteral("source_database_path")).toString(QStringLiteral("—")))
            .arg(dbProvides.isEmpty() ? QStringLiteral("—") : dbProvides));
    }
}

} // namespace twin
