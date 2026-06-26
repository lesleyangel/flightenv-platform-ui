#pragma once

#include "PdkUiReaders.h"
#include "PlatformMeshLayoutReader.h"

#include <QString>

namespace twin {

struct FieldRenderHint {
    QString fieldName;
    QString contractId;
    QString layoutRef;
    QString meshRef;
    QString componentId;
    qint64 nodeCount = 0;
};

struct FieldRenderBinding {
    bool ok = false;
    bool ambiguousGeometry = false;
    QString message;
    QString layoutId;
    QString layoutName;
    QString layoutRef;
    QString meshRef;
    qint64 artifactNodeCount = 0;
    qint64 layoutNodeCount = 0;
    qint64 valueCount = 0;
};

FieldRenderHint fieldRenderHintFromEntry(const PdkDataPlaneEntryView& entry);
FieldRenderHint fieldRenderHintFromJson(const QJsonObject& option);

// Central guard for U2/U4:
// - field value count must match node_count exactly;
// - artifact node_count must match the selected runtime layout;
// - pages/widgets must not guess legacy SubjectType from pressure/heatflux/damage names.
FieldRenderBinding bindFieldArtifactForRendering(
    const PdkFieldArtifactView& artifact,
    const PlatformMeshLayoutCatalog& catalog,
    const FieldRenderHint& hint = {});

} // namespace twin
