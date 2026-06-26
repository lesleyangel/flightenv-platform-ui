#pragma once

#include <QString>

#include <vector>

#include "EnvNodeSupport/RunEvidenceModels.h"

namespace launchsupport {
class PlatformCatalogReader;
}

namespace twin {

// 旧 platform-catalog 的运行记录兼容读者。
//
// 注意：这个类故意只暴露 run catalog，不再暴露 objects/models/bindings。
// 对象身份、组件、资源、算子和 workflow 的主真源只能来自对象包：
// object/twin_object.json、assets/resources.json、operators/*.atomic.json、workflows/*.json。
class LegacyRunCatalogSource {
public:
    explicit LegacyRunCatalogSource(const QString& catalogPath);
    ~LegacyRunCatalogSource();

    bool ok() const { return ok_; }
    QString catalogPath() const { return catalogPath_; }

    const std::vector<launchsupport::RunCatalogDTO>& runs() const { return runs_; }

private:
    bool ok_ = false;
    QString catalogPath_;
    std::vector<launchsupport::RunCatalogDTO> runs_;
};

} // namespace twin
