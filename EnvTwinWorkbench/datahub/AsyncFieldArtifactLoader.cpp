#include "AsyncFieldArtifactLoader.h"

#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>

#include <thread>
#include <utility>

namespace twin {

void loadFieldArtifactAsync(
    QObject* receiver,
    QString artifactPath,
    QString objectPackageRoot,
    QString compatibilitySnapshotPath,
    FieldRenderHint renderHint,
    QString title,
    std::function<void(LoadedFieldArtifact)> callback) {
    QPointer<QObject> guard(receiver);
    std::thread([guard,
                 artifactPath = std::move(artifactPath),
                 objectPackageRoot = std::move(objectPackageRoot),
                 compatibilitySnapshotPath = std::move(compatibilitySnapshotPath),
                 renderHint = std::move(renderHint),
                 title = std::move(title),
                 callback = std::move(callback)]() mutable {
        LoadedFieldArtifact loaded;
        loaded.artifactPath = artifactPath;
        loaded.layoutSourcePath = compatibilitySnapshotPath;
        loaded.title = title;

        PlatformMeshLayoutCatalog catalog =
            PlatformMeshLayoutReader().read(objectPackageRoot, compatibilitySnapshotPath);
        loaded.meshCatalog = catalog;
        loaded.layoutSourcePath = catalog.sourcePath;
        if (!catalog.ok()) {
            loaded.message = QStringLiteral("对象包 mesh layout catalog 读取失败，无法映射真实网格");
        } else {
            const PdkFieldArtifactView artifact = PdkFieldArtifactReader().read(artifactPath);
            const FieldRenderBinding binding =
                bindFieldArtifactForRendering(artifact, catalog, renderHint);
            if (!binding.ok) {
                loaded.message = binding.message.isEmpty()
                    ? QStringLiteral("field artifact 与对象包 mesh layout 无法绑定")
                    : binding.message;
            } else {
                loaded.ok = true;
                loaded.bindingMessage = binding.message;
                loaded.layoutId = binding.layoutId;
                loaded.layoutName = binding.layoutName;
                loaded.layoutNodeCount = binding.layoutNodeCount;
                loaded.fieldName = artifact.fieldName;
                loaded.unit = artifact.unit;
                loaded.nodeCount = binding.artifactNodeCount;
                loaded.statistics = artifact.statistics;
                loaded.values.assign(artifact.values.begin(), artifact.values.end());
            }
        }

        if (!guard) {
            return;
        }
        QMetaObject::invokeMethod(
            guard,
            [guard, callback = std::move(callback), loaded = std::move(loaded)]() mutable {
                if (!guard) {
                    return;
                }
                callback(std::move(loaded));
            },
            Qt::QueuedConnection);
    }).detach();
}

void loadMeshLayoutCatalogAsync(
    QObject* receiver,
    QString objectPackageRoot,
    QString compatibilitySnapshotPath,
    std::function<void(LoadedMeshLayoutCatalog)> callback) {
    QPointer<QObject> guard(receiver);
    std::thread([guard,
                 objectPackageRoot = std::move(objectPackageRoot),
                 compatibilitySnapshotPath = std::move(compatibilitySnapshotPath),
                 callback = std::move(callback)]() mutable {
        LoadedMeshLayoutCatalog loaded;

        PlatformMeshLayoutCatalog catalog =
            PlatformMeshLayoutReader().read(objectPackageRoot, compatibilitySnapshotPath);
        loaded.sourcePath = catalog.sourcePath;
        loaded.catalog = catalog;
        if (!catalog.ok()) {
            loaded.message = QStringLiteral("对象包 mesh layout catalog 读取失败，无法显示真实网格");
        } else {
            loaded.ok = true;
        }

        if (!guard) {
            return;
        }
        QMetaObject::invokeMethod(
            guard,
            [guard, callback = std::move(callback), loaded = std::move(loaded)]() mutable {
                if (!guard) {
                    return;
                }
                callback(std::move(loaded));
            },
            Qt::QueuedConnection);
    }).detach();
}

} // namespace twin
