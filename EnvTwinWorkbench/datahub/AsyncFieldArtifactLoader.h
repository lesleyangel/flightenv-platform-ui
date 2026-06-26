#pragma once

#include "FieldRenderGuard.h"

#include <QJsonObject>
#include <QString>
#include <functional>
#include <vector>

class QObject;

namespace twin {

struct LoadedFieldArtifact {
    bool ok = false;
    QString message;
    QString artifactPath;
    QString layoutSourcePath;
    QString fieldName;
    QString unit;
    QString title;
    QString bindingMessage;
    QString layoutId;
    QString layoutName;
    qint64 layoutNodeCount = 0;
    PlatformMeshLayoutCatalog meshCatalog;
    std::vector<double> values;
    qint64 nodeCount = 0;
    QJsonObject statistics;
};

struct LoadedMeshLayoutCatalog {
    bool ok = false;
    QString message;
    QString sourcePath;
    PlatformMeshLayoutCatalog catalog;
};

// Background workers only do file IO and platform artifact/layout parsing; VTK is updated on UI thread.
void loadFieldArtifactAsync(
    QObject* receiver,
    QString artifactPath,
    QString objectPackageRoot,
    QString compatibilitySnapshotPath,
    FieldRenderHint renderHint,
    QString title,
    std::function<void(LoadedFieldArtifact)> callback);

void loadMeshLayoutCatalogAsync(
    QObject* receiver,
    QString objectPackageRoot,
    QString compatibilitySnapshotPath,
    std::function<void(LoadedMeshLayoutCatalog)> callback);

} // namespace twin
