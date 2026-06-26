#pragma once

#include <QString>
#include <QWidget>

#include <optional>

#include "../datahub/PlatformMeshLayoutReader.h"
#include "../datahub/PdkUiReaders.h"

class QLabel;
class QTableWidget;
class QTreeWidget;
class QTreeWidgetItem;

namespace flightenv::ui::demo { class VtkModelFieldWidget; }

namespace twin {

class KvList;

// 对象画像页：以对象包为主真源，动态展示 object/twin_object.json、
// assets/resources.json 中的组件、资源组、传感器和模型绑定。
class ObjectPage : public QWidget {
    Q_OBJECT
public:
    ObjectPage(QString objectPackageRoot, QWidget* parent = nullptr);

    // evidence 根可提供兼容布局快照；缺省时从对象包资产解析 mesh layout。
    void setEvidenceRoot(const QString& evidenceRoot);

signals:
    void navigateTo(const QString& page);

private:
    void showDetail(const QString& objectId);
    void showComponentDetail(const QString& componentId);
    void showAssetGroupDetail(const QString& groupId);
    void showResourceDetail(const QString& resourceId);
    void renderLayout(const QString& layoutId);
    QString layoutIdForComponent(const QString& componentId) const;
    QString layoutIdForResource(const QString& resourceId) const;
    // 上下文表：传感器布局表 / 数据库内容表，随选中节点切换。
    void clearContext(const QString& hint);
    void showResourceListTable(const QString& groupId);
    void showResourceUsageTable(const QString& resourceId);
    void showSensorTable(const QString& componentFilter);
    void showDatabaseContent();

    PdkObjectPackageView objectPackage_;
    QString objectPackageRoot_;
    QString evidenceRoot_;
    QTreeWidget* tree_ = nullptr;
    QLabel* detailTitle_ = nullptr;
    KvList* detailKv_ = nullptr;
    QLabel* boundModels_ = nullptr;
    QLabel* contextTitle_ = nullptr;
    QTableWidget* contextTable_ = nullptr;
    flightenv::ui::demo::VtkModelFieldWidget* fieldWidget_ = nullptr;
    QLabel* fieldHint_ = nullptr;
    std::optional<PlatformMeshLayoutCatalog> meshCatalog_;
    QString currentLayoutId_;
};

} // namespace twin
