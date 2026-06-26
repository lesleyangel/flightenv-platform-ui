#include "LegacyRunCatalogSource.h"

#include "EnvNodeSupport/PlatformCatalogReader.h"

#include <QFileInfo>

#include <filesystem>

namespace twin {

LegacyRunCatalogSource::LegacyRunCatalogSource(const QString& catalogPath) : catalogPath_(catalogPath) {
    if (catalogPath.isEmpty() || !QFileInfo::exists(catalogPath)) {
        return;
    }
    try {
        launchsupport::PlatformCatalogReader reader;
        reader.open(std::filesystem::path(catalogPath.toStdString()));
        runs_ = reader.read_runs();
        ok_ = true;
    } catch (...) {
        ok_ = false;
    }
}

LegacyRunCatalogSource::~LegacyRunCatalogSource() = default;

} // namespace twin
