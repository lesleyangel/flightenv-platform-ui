#pragma once

#include <QString>
#include <QWidget>

namespace twin {

class LegacyRunCatalogSource;

// 总览页：平台运行门户。
// 主表达固定为对象包 -> workflow compile -> Runtime Host -> DataPlane -> Evidence；
// 算子状态按平台 family（state_transition/observation/filter/qoi）动态统计。
class OverviewPage : public QWidget {
    Q_OBJECT
public:
    OverviewPage(
        LegacyRunCatalogSource* legacyRunCatalog,
        QString evidenceRoot,
        QString objectPackageRoot,
        QWidget* parent = nullptr);

signals:
    void navigateTo(const QString& page);

private:
    LegacyRunCatalogSource* legacyRunCatalog_ = nullptr;
};

} // namespace twin
