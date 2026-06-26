#include "ModelsPage.h"

#include "../widgets/KvList.h"
#include "../widgets/Panel.h"
#include "../widgets/PageHeader.h"
#include "../widgets/StatusUtil.h"

#include <QBrush>
#include <QColor>
#include <QComboBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPixmap>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <map>
#include <utility>

namespace twin {

namespace {

// 资源类型 → {中文标签, 类别, 类别色}。类别用于色块标签和筛选器分组。
struct ResourceTypeMeta {
    QString label;
    QString category;
    QColor color;
};

ResourceTypeMeta typeMeta(const QString& type) {
    if (type == QStringLiteral("database")) return {QStringLiteral("数据库"), QStringLiteral("数据"), QColor(37, 99, 235)};
    if (type == QStringLiteral("trajectory_replay_dataset")) return {QStringLiteral("轨迹回放数据集"), QStringLiteral("数据"), QColor(37, 99, 235)};
    if (type == QStringLiteral("training_sample_set")) return {QStringLiteral("训练样本集"), QStringLiteral("数据"), QColor(37, 99, 235)};
    if (type == QStringLiteral("mesh")) return {QStringLiteral("网格"), QStringLiteral("几何"), QColor(13, 148, 136)};
    if (type == QStringLiteral("mesh_mapping")) return {QStringLiteral("网格映射"), QStringLiteral("几何"), QColor(13, 148, 136)};
    if (type == QStringLiteral("sensor_layout")) return {QStringLiteral("传感器布局"), QStringLiteral("传感"), QColor(217, 119, 6)};
    if (type == QStringLiteral("noise_model")) return {QStringLiteral("噪声模型"), QStringLiteral("传感"), QColor(217, 119, 6)};
    if (type == QStringLiteral("calibration_model")) return {QStringLiteral("标定模型"), QStringLiteral("传感"), QColor(217, 119, 6)};
    if (type == QStringLiteral("pod_basis")) return {QStringLiteral("POD 基"), QStringLiteral("模型"), QColor(79, 70, 229)};
    if (type == QStringLiteral("field_reconstruction_model")) return {QStringLiteral("场重建模型"), QStringLiteral("模型"), QColor(79, 70, 229)};
    if (type == QStringLiteral("coefficient_prediction_model")) return {QStringLiteral("系数预测模型"), QStringLiteral("模型"), QColor(79, 70, 229)};
    if (type == QStringLiteral("state_transition_model")) return {QStringLiteral("状态转移模型"), QStringLiteral("模型"), QColor(79, 70, 229)};
    if (type == QStringLiteral("state_accumulation_model")) return {QStringLiteral("状态累计模型"), QStringLiteral("模型"), QColor(79, 70, 229)};
    if (type == QStringLiteral("initial_state_field")) return {QStringLiteral("初始场"), QStringLiteral("场·状态"), QColor(22, 163, 74)};
    if (type == QStringLiteral("criterion")) return {QStringLiteral("判据"), QStringLiteral("判据"), QColor(190, 24, 93)};
    return {type.isEmpty() ? QStringLiteral("未声明") : type, QStringLiteral("其他"), QColor(100, 116, 139)};
}

// 类别色块图标：QSS 会覆盖 item 的前景/背景色，但不覆盖 decoration 图标，
// 所以用一个圆角色块小图标承载类别配色。
QIcon categorySwatch(const QColor& color) {
    QPixmap pm(12, 12);
    pm.fill(Qt::transparent);
    QPainter painter(&pm);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawRoundedRect(0, 0, 12, 12, 3, 3);
    painter.end();
    return QIcon(pm);
}

QString compactJson(const QJsonObject& object) {
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
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

std::map<QString, QJsonObject> resourceMap(const QJsonArray& resources) {
    std::map<QString, QJsonObject> out;
    for (const QJsonValue& value : resources) {
        const QJsonObject object = value.toObject();
        const QString id = object.value(QStringLiteral("resource_id")).toString();
        if (!id.isEmpty()) {
            out[id] = object;
        }
    }
    return out;
}

QString outputContractsText(const QJsonObject& resource) {
    if (resource.contains(QStringLiteral("output_contract_ids"))) {
        return jsonValueText(resource.value(QStringLiteral("output_contract_ids")));
    }
    return resource.value(QStringLiteral("output_contract_id")).toString(QStringLiteral("—"));
}

QStringList operatorRefsUsingResource(const QVector<PdkOperatorView>& operators, const QString& resourceId) {
    QStringList out;
    for (const PdkOperatorView& op : operators) {
        if (op.resourceRefs.contains(resourceId)) {
            out << QStringLiteral("%1 · %2").arg(op.operatorFamily, op.operatorId);
        }
    }
    return out;
}

} // namespace

ModelsPage::ModelsPage(
    QString objectPackageRoot,
    QWidget* parent)
    : QWidget(parent),
      objectPackage_(PdkObjectPackageReader().read(objectPackageRoot)) {
    const auto byId = resourceMap(objectPackage_.twinObjectJson.value(QStringLiteral("resources")).toArray());
    for (const PdkAssetGroupView& group : objectPackage_.assetGroups) {
        for (const QString& resourceId : group.resourceIds) {
            const auto it = byId.find(resourceId);
            const QJsonObject raw = it == byId.end() ? QJsonObject{} : it->second;
            resources_.push_back(ObjectResourceRow{
                group.groupId,
                resourceId,
                raw.value(QStringLiteral("resource_type")).toString(),
                raw.value(QStringLiteral("uri")).toString(),
                raw.value(QStringLiteral("component_id")).toString(),
                raw});
        }
    }

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);
    root->addWidget(makePageHeader(
        QStringLiteral("资源与模型资产"),
        QStringLiteral("对象包资源是真源：database / mesh / sensor / POD / predictor / reconstruction / accumulation / criteria"), this));

    auto* twoCol = new QHBoxLayout();
    twoCol->setSpacing(12);

    auto* listPanel = new Panel(QStringLiteral("对象资源"), this);
    listPanel->setSubtitle(QStringLiteral("assets/resources.json + object/twin_object.json"));

    // 筛选条：搜索 + 资源组 + 类别。
    auto* filterBar = new QHBoxLayout();
    filterBar->setSpacing(8);
    searchBox_ = new QLineEdit(listPanel->body());
    searchBox_->setPlaceholderText(QStringLiteral("搜索 resource_id / uri"));
    searchBox_->setClearButtonEnabled(true);
    filterBar->addWidget(searchBox_, 2);

    groupFilter_ = new QComboBox(listPanel->body());
    groupFilter_->addItem(QStringLiteral("全部组"), QString());
    QStringList seenGroups;
    for (const ObjectResourceRow& r : resources_) {
        if (!r.groupId.isEmpty() && !seenGroups.contains(r.groupId)) {
            seenGroups << r.groupId;
            groupFilter_->addItem(r.groupId, r.groupId);
        }
    }
    filterBar->addWidget(groupFilter_, 1);

    categoryFilter_ = new QComboBox(listPanel->body());
    categoryFilter_->addItem(QStringLiteral("全部类别"), QString());
    QStringList seenCats;
    for (const ObjectResourceRow& r : resources_) {
        const QString cat = typeMeta(r.resourceType).category;
        if (!seenCats.contains(cat)) {
            seenCats << cat;
            categoryFilter_->addItem(cat, cat);
        }
    }
    filterBar->addWidget(categoryFilter_, 1);
    listPanel->bodyLayout()->addLayout(filterBar);

    countLabel_ = new QLabel(QString(), listPanel->body());
    countLabel_->setProperty("tiny", true);
    countLabel_->setProperty("muted", true);
    listPanel->bodyLayout()->addWidget(countLabel_);

    table_ = makeTable(
        {QStringLiteral("类别"), QStringLiteral("resource_id"), QStringLiteral("类型"),
         QStringLiteral("组"), QStringLiteral("组件"), QStringLiteral("输入契约"),
         QStringLiteral("输出契约"), QStringLiteral("使用算子"), QStringLiteral("uri")},
        listPanel->body());
    connect(table_, &QTableWidget::currentCellChanged, this, [this](int row, int, int, int) {
        showDetail(row);
    });
    listPanel->bodyLayout()->addWidget(table_);
    twoCol->addWidget(listPanel, 2);

    connect(searchBox_, &QLineEdit::textChanged, this, [this](const QString&) { populateTable(); });
    connect(groupFilter_, &QComboBox::currentIndexChanged, this, [this](int) { populateTable(); });
    connect(categoryFilter_, &QComboBox::currentIndexChanged, this, [this](int) { populateTable(); });

    auto* detailPanel = new Panel(QStringLiteral("资源详情"), this);
    detailPanel->setMinimumWidth(340);
    detailPanel->setMaximumWidth(480);
    detailTitle_ = new QLabel(QStringLiteral("选择一个对象资源"), detailPanel->body());
    detailTitle_->setProperty("mono", true);
    detailTitle_->setStyleSheet(QStringLiteral("font-weight:700;font-size:13px;color:#1b1f27;"));
    detailTitle_->setWordWrap(true);
    detailPanel->bodyLayout()->addWidget(detailTitle_);

    detailApplies_ = new QLabel(QStringLiteral("资源属于对象包，不属于平台 UI 固定结构"), detailPanel->body());
    detailApplies_->setProperty("muted", true);
    detailApplies_->setWordWrap(true);
    detailPanel->bodyLayout()->addWidget(detailApplies_);

    detailKv_ = new KvList(detailPanel->body());
    detailPanel->bodyLayout()->addWidget(detailKv_);
    detailPanel->bodyLayout()->addStretch(1);
    twoCol->addWidget(detailPanel);

    root->addLayout(twoCol, 1);

    populateTable();
}

void ModelsPage::populateTable() {
    if (!table_) {
        return;
    }
    const QString search = searchBox_ ? searchBox_->text().trimmed() : QString();
    const QString grp = groupFilter_ ? groupFilter_->currentData().toString() : QString();
    const QString cat = categoryFilter_ ? categoryFilter_->currentData().toString() : QString();

    const QSignalBlocker blocker(table_);
    table_->setRowCount(0);
    int shown = 0;
    for (int i = 0; i < resources_.size(); ++i) {
        const ObjectResourceRow& r = resources_[i];
        const ResourceTypeMeta meta = typeMeta(r.resourceType);
        if (!grp.isEmpty() && r.groupId != grp) {
            continue;
        }
        if (!cat.isEmpty() && meta.category != cat) {
            continue;
        }
        if (!search.isEmpty() &&
            !r.resourceId.contains(search, Qt::CaseInsensitive) &&
            !r.uri.contains(search, Qt::CaseInsensitive) &&
            !r.resourceType.contains(search, Qt::CaseInsensitive) &&
            !r.componentId.contains(search, Qt::CaseInsensitive) &&
            !compactJson(r.rawJson).contains(search, Qt::CaseInsensitive)) {
            continue;
        }
        const int row = table_->rowCount();
        table_->insertRow(row);

        auto* tag = new QTableWidgetItem(categorySwatch(meta.color), meta.category);
        tag->setData(Qt::UserRole, i);  // 行 → resources_ 下标
        table_->setItem(row, 0, tag);
        table_->setItem(row, 1, new QTableWidgetItem(r.resourceId));
        table_->setItem(row, 2, new QTableWidgetItem(meta.label));
        table_->setItem(row, 3, new QTableWidgetItem(r.groupId));
        table_->setItem(row, 4, new QTableWidgetItem(r.componentId));
        table_->setItem(row, 5, new QTableWidgetItem(jsonValueText(r.rawJson.value(QStringLiteral("input_contract_ids")))));
        table_->setItem(row, 6, new QTableWidgetItem(outputContractsText(r.rawJson)));
        table_->setItem(row, 7, new QTableWidgetItem(QString::number(operatorRefsUsingResource(objectPackage_.operators, r.resourceId).size())));
        table_->setItem(row, 8, new QTableWidgetItem(r.uri));
        ++shown;
    }
    if (shown == 0) {
        table_->insertRow(0);
        table_->setItem(0, 0, new QTableWidgetItem(QStringLiteral("无匹配资源")));
    }
    if (countLabel_) {
        countLabel_->setText(QStringLiteral("显示 %1 / 共 %2 个资源").arg(shown).arg(resources_.size()));
    }
    if (shown > 0) {
        table_->setCurrentCell(0, 0);
        showDetail(0);
    } else {
        detailKv_->clear();
        detailTitle_->setText(QStringLiteral("无匹配资源"));
        detailApplies_->setText(QStringLiteral("调整搜索 / 组 / 类别筛选"));
    }
}

void ModelsPage::showDetail(int row) {
    if (row < 0 || !table_ || !table_->item(row, 0)) {
        return;
    }
    bool ok = false;
    const int idx = table_->item(row, 0)->data(Qt::UserRole).toInt(&ok);
    if (!ok || idx < 0 || idx >= resources_.size()) {
        return;
    }
    const ObjectResourceRow& resource = resources_[idx];
    const ResourceTypeMeta meta = typeMeta(resource.resourceType);
    detailTitle_->setText(resource.resourceId);
    detailApplies_->setText(QStringLiteral("%1 · %2 · 组 %3")
                                .arg(meta.category, meta.label, resource.groupId));

    detailKv_->clear();
    detailKv_->addRow(QStringLiteral("类别 / 标签"), QStringLiteral("%1 · %2").arg(meta.category, meta.label), false);
    detailKv_->addRow(QStringLiteral("resource_type"), resource.resourceType, false);
    detailKv_->addRow(QStringLiteral("uri"), resource.uri);
    detailKv_->addRow(QStringLiteral("component_id"), resource.componentId);
    detailKv_->addRow(QStringLiteral("model_id"), resource.rawJson.value(QStringLiteral("model_id")).toString());
    detailKv_->addRow(QStringLiteral("legacy_model_id"), resource.rawJson.value(QStringLiteral("legacy_model_id")).toString());
    detailKv_->addRow(QStringLiteral("adapter_backend"), resource.rawJson.value(QStringLiteral("adapter_backend")).toString());
    detailKv_->addRow(QStringLiteral("input_contract_ids"), jsonValueText(resource.rawJson.value(QStringLiteral("input_contract_ids"))), false);
    detailKv_->addRow(QStringLiteral("output_contract"), outputContractsText(resource.rawJson), false);
    detailKv_->addRow(QStringLiteral("covers_contract_ids"), jsonValueText(resource.rawJson.value(QStringLiteral("covers_contract_ids"))), false);
    detailKv_->addRow(QStringLiteral("basis_ref"), resource.rawJson.value(QStringLiteral("basis_ref")).toString());
    detailKv_->addRow(QStringLiteral("source_database_ref"), resource.rawJson.value(QStringLiteral("source_database_ref")).toString());
    detailKv_->addRow(QStringLiteral("model_package_root"), resource.rawJson.value(QStringLiteral("model_package_root")).toString(), false);
    detailKv_->addRow(QStringLiteral("pod_text_dump_dir"), resource.rawJson.value(QStringLiteral("pod_text_dump_dir")).toString(), false);
    detailKv_->addRow(QStringLiteral("pred_train_model_dir"), resource.rawJson.value(QStringLiteral("pred_train_model_dir")).toString(), false);
    detailKv_->addRow(QStringLiteral("pod_model_dir"), resource.rawJson.value(QStringLiteral("pod_model_dir")).toString());
    const QStringList users = operatorRefsUsingResource(objectPackage_.operators, resource.resourceId);
    detailKv_->addRow(QStringLiteral("引用算子"), users.isEmpty() ? QStringLiteral("—") : users.join(QStringLiteral("\n")), false);
    detailKv_->addRow(QStringLiteral("raw"), compactJson(resource.rawJson));
}

} // namespace twin
