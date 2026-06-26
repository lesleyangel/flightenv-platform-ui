#include "PlatformMeshLayoutReader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <algorithm>
#include <utility>

namespace twin {
namespace {

struct ResourceDecl {
    QString resourceId;
    QString displayName;
    QString componentId;
    QString path;
    QString layoutRole;
};

struct FieldSchemaDecl {
    QString schemaId;
    QString displayName;
    QString componentId;
    QString quantity;
    QString unit;
    QString layoutRef;
};

void addIssue(QVector<PdkReadIssue>& issues,
              QString severity,
              QString code,
              QString message,
              QString path = {}) {
    issues.push_back(PdkReadIssue{
        std::move(severity),
        std::move(code),
        std::move(message),
        QDir::toNativeSeparators(std::move(path))});
}

QJsonObject readJsonObject(const QString& path,
                           QVector<PdkReadIssue>& issues,
                           bool required,
                           const QString& missingCode) {
    QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        addIssue(issues,
                 required ? QStringLiteral("error") : QStringLiteral("warning"),
                 missingCode,
                 QStringLiteral("JSON 文件不存在"),
                 path);
        return {};
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        addIssue(issues,
                 required ? QStringLiteral("error") : QStringLiteral("warning"),
                 QStringLiteral("file_open_failed"),
                 file.errorString(),
                 path);
        return {};
    }
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        addIssue(issues,
                 required ? QStringLiteral("error") : QStringLiteral("warning"),
                 QStringLiteral("json_parse_failed"),
                 error.errorString(),
                 path);
        return {};
    }
    return doc.object();
}

QString normalized(QString value) {
    return value.trimmed().toLower().replace(QLatin1Char('\\'), QLatin1Char('/'));
}

void addAlias(QStringList& aliases, const QString& value) {
    const QString alias = normalized(value);
    if (!alias.isEmpty() && !aliases.contains(alias)) {
        aliases.push_back(alias);
    }
}

QString workspaceRootFromObjectPackage(const QString& objectPackageRoot) {
    QDir dir(QDir::fromNativeSeparators(objectPackageRoot));
    if (dir.dirName().startsWith(QStringLiteral("flightenv-object-")) && dir.cdUp()) {
        return dir.absolutePath();
    }
    return QDir::currentPath();
}

QString resolvePath(const QString& ref, const QString& objectPackageRoot) {
    if (ref.trimmed().isEmpty()) {
        return {};
    }
    const QString clean = QDir::fromNativeSeparators(ref.trimmed());
    const QFileInfo direct(clean);
    if (direct.isAbsolute() && direct.exists()) {
        return direct.absoluteFilePath();
    }

    const QString workspaceRoot = workspaceRootFromObjectPackage(objectPackageRoot);
    const QStringList roots{
        objectPackageRoot,
        QDir(objectPackageRoot).filePath(QStringLiteral("assets")),
        workspaceRoot,
        QDir(workspaceRoot).filePath(QStringLiteral("_deps/example")),
        QDir(workspaceRoot).filePath(QStringLiteral("_deps/data")),
        QDir::currentPath(),
        QDir(QDir::currentPath()).filePath(QStringLiteral("_deps/example")),
        QDir(QDir::currentPath()).filePath(QStringLiteral("_deps/data"))
    };
    for (const QString& root : roots) {
        if (root.isEmpty()) {
            continue;
        }
        const QFileInfo candidate(QDir(root).filePath(clean));
        if (candidate.exists()) {
            return candidate.absoluteFilePath();
        }
    }
    return {};
}

QHash<QString, ResourceDecl> readMeshResources(const QJsonObject& objectJson) {
    QHash<QString, ResourceDecl> resources;
    for (const QJsonValue& value : objectJson.value(QStringLiteral("resources")).toArray()) {
        const QJsonObject obj = value.toObject();
        if (obj.value(QStringLiteral("resource_type")).toString() != QStringLiteral("mesh")) {
            continue;
        }
        ResourceDecl decl;
        decl.resourceId = obj.value(QStringLiteral("resource_id")).toString();
        decl.displayName = obj.value(QStringLiteral("display_name")).toString();
        decl.componentId = obj.value(QStringLiteral("component_id")).toString();
        decl.path = obj.value(QStringLiteral("path")).toString();
        decl.layoutRole = obj.value(QStringLiteral("layout_role")).toString();
        if (!decl.resourceId.isEmpty()) {
            resources.insert(decl.resourceId, decl);
        }
    }
    return resources;
}

QVector<FieldSchemaDecl> readFieldSchemas(const QString& objectPackageRoot, QVector<PdkReadIssue>& issues) {
    QVector<FieldSchemaDecl> schemas;
    const QDir schemaDir(QDir(objectPackageRoot).filePath(QStringLiteral("domain_schemas")));
    if (!schemaDir.exists()) {
        addIssue(issues,
                 QStringLiteral("warning"),
                 QStringLiteral("domain_schema_dir_missing"),
                 QStringLiteral("domain_schemas 目录不存在，布局只能按兼容快照推断"),
                 schemaDir.absolutePath());
        return schemas;
    }
    const QFileInfoList files = schemaDir.entryInfoList({QStringLiteral("*.json")}, QDir::Files);
    for (const QFileInfo& file : files) {
        const QJsonObject obj = readJsonObject(file.absoluteFilePath(), issues, false, QStringLiteral("domain_schema_missing"));
        if (obj.value(QStringLiteral("value_kind")).toString() != QStringLiteral("field")) {
            continue;
        }
        FieldSchemaDecl schema;
        schema.schemaId = obj.value(QStringLiteral("schema_id")).toString();
        schema.displayName = obj.value(QStringLiteral("display_name")).toString();
        schema.componentId = obj.value(QStringLiteral("component_id")).toString();
        schema.quantity = obj.value(QStringLiteral("quantity")).toString();
        schema.unit = obj.value(QStringLiteral("unit")).toString();
        schema.layoutRef = obj.value(QStringLiteral("layout_ref")).toString();
        if (!schema.schemaId.isEmpty() && !schema.layoutRef.isEmpty()) {
            schemas.push_back(std::move(schema));
        }
    }
    return schemas;
}

bool schemaMatchesLayoutName(const FieldSchemaDecl& schema, const QString& layoutName) {
    const QString name = normalized(layoutName);
    if (name.isEmpty()) {
        return false;
    }
    const QString quantity = normalized(schema.quantity);
    const QString schemaId = normalized(schema.schemaId);
    const QString display = normalized(schema.displayName);
    return quantity.contains(name) || schemaId.contains(name) || display.contains(name);
}

QString defaultMeshRefForSubject(const QString& subject) {
    const QString clean = subject.trimmed().toUpper();
    if (clean == QStringLiteral("P") || clean == QStringLiteral("K")) {
        return QStringLiteral("mesh.shell.surface");
    }
    if (clean == QStringLiteral("S") || clean == QStringLiteral("T")) {
        return QStringLiteral("mesh.structure.full");
    }
    return {};
}

const FieldSchemaDecl* selectSchemaForLayout(const QVector<FieldSchemaDecl>& schemas,
                                             const QString& layoutName,
                                             qint64 nodeCount,
                                             const QString& subject) {
    QVector<const FieldSchemaDecl*> named;
    for (const FieldSchemaDecl& schema : schemas) {
        if (schemaMatchesLayoutName(schema, layoutName)) {
            named.push_back(&schema);
        }
    }
    if (!named.isEmpty()) {
        return named.front();
    }

    const QString defaultMesh = defaultMeshRefForSubject(subject);
    if (!defaultMesh.isEmpty()) {
        for (const FieldSchemaDecl& schema : schemas) {
            if (schema.layoutRef == defaultMesh) {
                return &schema;
            }
        }
    }
    Q_UNUSED(nodeCount);
    return schemas.isEmpty() ? nullptr : &schemas.front();
}

QHash<QString, QString> readSubjectMeshPaths(const QJsonArray& meshes) {
    QHash<QString, QString> paths;
    for (const QJsonValue& value : meshes) {
        const QJsonObject mesh = value.toObject();
        const QString path = mesh.value(QStringLiteral("path")).toString();
        if (path.isEmpty()) {
            continue;
        }
        for (const QJsonValue& subjectValue : mesh.value(QStringLiteral("subjects")).toArray()) {
            const QString subject = subjectValue.toString().trimmed().toUpper();
            if (!subject.isEmpty() && !paths.contains(subject)) {
                paths.insert(subject, path);
            }
        }
    }
    return paths;
}

PlatformMeshLayoutView makeLayoutFromSnapshot(const QJsonObject& layoutObj,
                                              const QVector<FieldSchemaDecl>& schemas,
                                              const QHash<QString, ResourceDecl>& resources,
                                              const QHash<QString, QString>& subjectMeshPaths,
                                              const QString& objectPackageRoot,
                                              const QString& sourcePath,
                                              int ordinal) {
    const QString layoutName = layoutObj.value(QStringLiteral("name")).toString();
    const QString subject = layoutObj.value(QStringLiteral("subject")).toString().trimmed().toUpper();
    const qint64 declaredNodeCount = static_cast<qint64>(layoutObj.value(QStringLiteral("node_count")).toDouble());
    const FieldSchemaDecl* schema = selectSchemaForLayout(schemas, layoutName, declaredNodeCount, subject);

    PlatformMeshLayoutView view;
    view.layoutName = layoutName.isEmpty()
        ? QStringLiteral("layout_%1").arg(ordinal)
        : layoutName;
    view.layoutId = schema && !schema->schemaId.isEmpty()
        ? schema->schemaId
        : QStringLiteral("%1.%2").arg(defaultMeshRefForSubject(subject), view.layoutName);
    view.meshRef = schema && !schema->layoutRef.isEmpty()
        ? schema->layoutRef
        : defaultMeshRefForSubject(subject);
    view.layoutRef = view.meshRef;
    view.componentId = schema ? schema->componentId : QString();
    view.unit = schema && !schema->unit.isEmpty()
        ? schema->unit
        : layoutObj.value(QStringLiteral("unit")).toString();
    view.valueDim = std::max(1, layoutObj.value(QStringLiteral("value_dim")).toInt(1));
    view.sourcePath = QDir::toNativeSeparators(sourcePath);

    const QJsonArray nodes = layoutObj.value(QStringLiteral("nodes")).toArray();
    view.nodes.reserve(nodes.size());
    for (const QJsonValue& nodeValue : nodes) {
        const QJsonObject node = nodeValue.toObject();
        view.nodes.push_back(PlatformMeshNode{
            node.value(QStringLiteral("x")).toDouble(),
            node.value(QStringLiteral("y")).toDouble(),
            node.value(QStringLiteral("z")).toDouble()});
    }
    view.nodeCount = declaredNodeCount > 0 ? declaredNodeCount : view.nodes.size();

    const ResourceDecl resource = resources.value(view.meshRef);
    view.meshResourcePath = resolvePath(resource.path, objectPackageRoot);
    const QString compatSurface = subjectMeshPaths.value(subject);
    view.surfaceIndexPath = resolvePath(compatSurface, objectPackageRoot);
    if (view.surfaceIndexPath.isEmpty()) {
        view.surfaceIndexPath = view.meshResourcePath;
    }

    addAlias(view.aliases, view.layoutId);
    addAlias(view.aliases, view.layoutName);
    addAlias(view.aliases, view.meshRef);
    addAlias(view.aliases, view.layoutRef);
    addAlias(view.aliases, view.componentId);
    addAlias(view.aliases, subject);
    if (schema) {
        addAlias(view.aliases, schema->schemaId);
        addAlias(view.aliases, schema->quantity);
        addAlias(view.aliases, schema->displayName);
        addAlias(view.aliases, schema->layoutRef);
    }
    addAlias(view.aliases, resource.resourceId);
    addAlias(view.aliases, resource.displayName);
    return view;
}

QString compatibilitySnapshotPath(const QString& objectPackageRoot, const QString& explicitPath) {
    if (!explicitPath.trimmed().isEmpty() && QFileInfo::exists(explicitPath)) {
        return QFileInfo(explicitPath).absoluteFilePath();
    }
    const QStringList candidates{
        QDir(objectPackageRoot).filePath(QStringLiteral("assets/runtime_snapshot.json")),
        QDir(objectPackageRoot).filePath(QStringLiteral("runtime_snapshot.json"))
    };
    for (const QString& candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return QFileInfo(candidate).absoluteFilePath();
        }
    }
    return {};
}

} // namespace

PlatformMeshLayoutCatalog PlatformMeshLayoutReader::read(
    const QString& objectPackageRoot,
    const QString& compatibilitySnapshotPathOverride) const {
    PlatformMeshLayoutCatalog catalog;
    catalog.objectPackageRoot = QDir::toNativeSeparators(QFileInfo(objectPackageRoot).absoluteFilePath());

    const QString objectJsonPath = QDir(objectPackageRoot).filePath(QStringLiteral("object/twin_object.json"));
    const QJsonObject objectJson = readJsonObject(
        objectJsonPath,
        catalog.issues,
        true,
        QStringLiteral("object_manifest_missing"));
    const QHash<QString, ResourceDecl> resources = readMeshResources(objectJson);
    const QVector<FieldSchemaDecl> schemas = readFieldSchemas(objectPackageRoot, catalog.issues);

    const QString snapshotPath = compatibilitySnapshotPath(objectPackageRoot, compatibilitySnapshotPathOverride);
    catalog.sourcePath = QDir::toNativeSeparators(snapshotPath);
    const QJsonObject snapshotJson = readJsonObject(
        snapshotPath,
        catalog.issues,
        true,
        QStringLiteral("mesh_layout_source_missing"));
    if (snapshotJson.isEmpty()) {
        return catalog;
    }

    const QHash<QString, QString> subjectMeshPaths =
        readSubjectMeshPaths(snapshotJson.value(QStringLiteral("meshes")).toArray());
    int ordinal = 0;
    QSet<QString> seenLayoutIds;
    for (const QJsonValue& layoutValue : snapshotJson.value(QStringLiteral("field_layouts")).toArray()) {
        PlatformMeshLayoutView layout = makeLayoutFromSnapshot(
            layoutValue.toObject(),
            schemas,
            resources,
            subjectMeshPaths,
            objectPackageRoot,
            snapshotPath,
            ordinal++);
        if (layout.layoutId.isEmpty()) {
            layout.layoutId = QStringLiteral("mesh_layout.%1").arg(ordinal);
        }
        if (seenLayoutIds.contains(layout.layoutId)) {
            layout.layoutId = QStringLiteral("%1#%2").arg(layout.layoutId).arg(ordinal);
        }
        seenLayoutIds.insert(layout.layoutId);
        if (layout.nodes.isEmpty()) {
            addIssue(catalog.issues,
                     QStringLiteral("warning"),
                     QStringLiteral("mesh_layout_nodes_empty"),
                     QStringLiteral("mesh layout 没有节点坐标"),
                     layout.layoutId);
        }
        if (layout.surfaceIndexPath.isEmpty()) {
            addIssue(catalog.issues,
                     QStringLiteral("warning"),
                     QStringLiteral("mesh_layout_surface_missing"),
                     QStringLiteral("mesh layout 未解析到面片索引或网格资源"),
                     layout.layoutId);
        }
        catalog.layouts.push_back(std::move(layout));
    }

    if (catalog.layouts.isEmpty()) {
        addIssue(catalog.issues,
                 QStringLiteral("error"),
                 QStringLiteral("mesh_layout_empty"),
                 QStringLiteral("未能从对象包解析任何 mesh layout"),
                 snapshotPath);
    }
    return catalog;
}

QString platformMeshLayoutSummary(const PlatformMeshLayoutView& layout) {
    return QStringLiteral("%1 / %2 / nodes=%3 / mesh=%4")
        .arg(layout.layoutId,
             layout.componentId.isEmpty() ? QStringLiteral("-") : layout.componentId)
        .arg(layout.nodeCount)
        .arg(layout.meshRef.isEmpty() ? QStringLiteral("-") : layout.meshRef);
}

} // namespace twin
