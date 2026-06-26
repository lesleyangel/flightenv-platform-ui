#pragma once

#include "PdkUiReaders.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace twin {

struct PlatformMeshNode {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct PlatformMeshLayoutView {
    QString layoutId;
    QString layoutName;
    QString layoutRef;
    QString meshRef;
    QString componentId;
    QString unit;
    QString surfaceIndexPath;
    QString meshResourcePath;
    QString sourcePath;
    QStringList aliases;
    qint64 nodeCount = 0;
    int valueDim = 1;
    QVector<PlatformMeshNode> nodes;

    bool ok() const { return nodeCount > 0 && !nodes.isEmpty() && !surfaceIndexPath.isEmpty(); }
};

struct PlatformMeshLayoutCatalog {
    QString objectPackageRoot;
    QString sourcePath;
    QVector<PlatformMeshLayoutView> layouts;
    QVector<PdkReadIssue> issues;

    bool ok() const { return !hasPdkReadErrors(issues) && !layouts.isEmpty(); }
};

class PlatformMeshLayoutReader {
public:
    PlatformMeshLayoutCatalog read(const QString& objectPackageRoot,
                                   const QString& compatibilitySnapshotPath = QString()) const;
};

QString platformMeshLayoutSummary(const PlatformMeshLayoutView& layout);

} // namespace twin
