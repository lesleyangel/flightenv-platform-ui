#pragma once

#include <QString>
#include <QWidget>

namespace twin {

class LegacyRunCatalogSource;

// 配置与数据源页：等价 htmlUI config.png —— 运行配置 + 锁定的数据资产清单。
// SDK 只读视图：catalog 路径、evidence 输出根、各 run 锁定的 DB/POD/BPNN/mesh 资产。
class ConfigPage : public QWidget {
    Q_OBJECT
public:
    ConfigPage(
        LegacyRunCatalogSource* legacyRunCatalog,
        QString evidenceRoot,
        QString objectPackageRoot,
        QWidget* parent = nullptr);

private:
    LegacyRunCatalogSource* legacyRunCatalog_ = nullptr;
};

} // namespace twin
