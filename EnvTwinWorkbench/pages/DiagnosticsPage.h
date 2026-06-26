#pragma once

#include <QString>
#include <QWidget>

namespace twin {

class LegacyRunCatalogSource;

class DiagnosticsPage final : public QWidget {
    Q_OBJECT
public:
    DiagnosticsPage(
        LegacyRunCatalogSource* legacyRunCatalog,
        QString evidenceRoot,
        QString objectPackageRoot,
        QWidget* parent = nullptr);

private:
    LegacyRunCatalogSource* legacyRunCatalog_ = nullptr;
    QString evidenceRoot_;
    QString objectPackageRoot_;
};

} // namespace twin
