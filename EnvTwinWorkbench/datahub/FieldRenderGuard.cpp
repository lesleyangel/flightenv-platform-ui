#include "FieldRenderGuard.h"

#include <algorithm>

namespace twin {
namespace {

qint64 layoutNodeCount(const PlatformMeshLayoutView& layout) {
    return layout.nodeCount > 0 ? layout.nodeCount : layout.nodes.size();
}

QString normalized(QString value) {
    return value.trimmed().toLower().replace(QLatin1Char('\\'), QLatin1Char('/'));
}

void addToken(QStringList& tokens, const QString& value) {
    const QString token = normalized(value);
    if (!token.isEmpty() && !tokens.contains(token)) {
        tokens.push_back(token);
    }
}

QStringList artifactTokens(const PdkFieldArtifactView& artifact, const FieldRenderHint& hint) {
    QStringList tokens;
    addToken(tokens, artifact.fieldName);
    addToken(tokens, artifact.contractId);
    addToken(tokens, artifact.layoutRef);
    addToken(tokens, artifact.meshRef);
    addToken(tokens, artifact.componentId);
    addToken(tokens, hint.fieldName);
    addToken(tokens, hint.contractId);
    addToken(tokens, hint.layoutRef);
    addToken(tokens, hint.meshRef);
    addToken(tokens, hint.componentId);
    return tokens;
}

bool layoutNameMatches(const PlatformMeshLayoutView& layout, const QStringList& tokens) {
    QStringList aliases = layout.aliases;
    addToken(aliases, layout.layoutId);
    addToken(aliases, layout.layoutName);
    addToken(aliases, layout.layoutRef);
    addToken(aliases, layout.meshRef);
    addToken(aliases, layout.componentId);
    for (const QString& alias : aliases) {
        if (alias.isEmpty()) {
            continue;
        }
        for (const QString& token : tokens) {
            if (token == alias || token.contains(alias) || alias.contains(token)) {
                return true;
            }
        }
    }
    return false;
}

FieldRenderBinding failBinding(
    const QString& message,
    const PdkFieldArtifactView& artifact,
    const FieldRenderHint& hint) {
    FieldRenderBinding binding;
    binding.message = message;
    binding.artifactNodeCount = artifact.nodeCount > 0 ? artifact.nodeCount : hint.nodeCount;
    binding.valueCount = artifact.values.size();
    binding.layoutRef = artifact.layoutRef.isEmpty() ? hint.layoutRef : artifact.layoutRef;
    binding.meshRef = artifact.meshRef.isEmpty() ? hint.meshRef : artifact.meshRef;
    return binding;
}

} // namespace

FieldRenderHint fieldRenderHintFromEntry(const PdkDataPlaneEntryView& entry) {
    FieldRenderHint hint;
    hint.fieldName = entry.fieldName;
    hint.contractId = entry.contractId;
    hint.layoutRef = entry.layoutRef;
    hint.meshRef = entry.meshRef;
    hint.componentId = entry.componentId;
    hint.nodeCount = entry.nodeCount;
    return hint;
}

FieldRenderHint fieldRenderHintFromJson(const QJsonObject& option) {
    FieldRenderHint hint;
    hint.fieldName = option.value(QStringLiteral("field_name")).toString();
    hint.contractId = option.value(QStringLiteral("contract_id")).toString();
    hint.layoutRef = option.value(QStringLiteral("layout_ref")).toString();
    hint.meshRef = option.value(QStringLiteral("mesh_ref")).toString();
    hint.componentId = option.value(QStringLiteral("component_id")).toString();
    const QJsonValue nodeCount = option.value(QStringLiteral("node_count"));
    if (nodeCount.isDouble()) {
        hint.nodeCount = static_cast<qint64>(nodeCount.toDouble());
    }
    return hint;
}

FieldRenderBinding bindFieldArtifactForRendering(
    const PdkFieldArtifactView& artifact,
    const PlatformMeshLayoutCatalog& catalog,
    const FieldRenderHint& hint) {
    if (!artifact.ok()) {
        return failBinding(QStringLiteral("field artifact 读取失败，不能渲染真实场"), artifact, hint);
    }

    const qint64 artifactNodeCount = artifact.nodeCount > 0 ? artifact.nodeCount : hint.nodeCount;
    if (artifactNodeCount <= 0) {
        return failBinding(QStringLiteral("field artifact 缺少 node_count，拒绝绘制伪场"), artifact, hint);
    }
    if (artifact.values.size() != artifactNodeCount) {
        return failBinding(
            QStringLiteral("field artifact values 数量与 node_count 不一致：values=%1 node_count=%2")
                .arg(artifact.values.size())
                .arg(artifactNodeCount),
            artifact,
            hint);
    }
    if (catalog.layouts.empty()) {
        return failBinding(QStringLiteral("对象包 mesh layout catalog 为空，无法映射真实网格"), artifact, hint);
    }

    const QStringList tokens = artifactTokens(artifact, hint);
    QVector<const PlatformMeshLayoutView*> nodeCountMatches;
    QVector<const PlatformMeshLayoutView*> namedMatches;
    for (const PlatformMeshLayoutView& layout : catalog.layouts) {
        if (layoutNodeCount(layout) != artifactNodeCount) {
            continue;
        }
        nodeCountMatches.push_back(&layout);
        if (layoutNameMatches(layout, tokens)) {
            namedMatches.push_back(&layout);
        }
    }

    if (nodeCountMatches.empty()) {
        return failBinding(
            QStringLiteral("场节点数与对象包 mesh layout 不匹配：artifact=%1，catalog 中无对应 layout")
                .arg(artifactNodeCount),
            artifact,
            hint);
    }

    const PlatformMeshLayoutView* selected = nullptr;
    bool ambiguousGeometry = false;
    if (!namedMatches.empty()) {
        selected = namedMatches.front();
        ambiguousGeometry = namedMatches.size() > 1;
    } else {
        selected = nodeCountMatches.front();
        ambiguousGeometry = nodeCountMatches.size() > 1;
    }

    FieldRenderBinding binding;
    binding.ok = true;
    binding.ambiguousGeometry = ambiguousGeometry;
    binding.layoutId = selected->layoutId;
    binding.layoutName = selected->layoutName.isEmpty() ? selected->layoutId : selected->layoutName;
    binding.layoutRef = artifact.layoutRef.isEmpty() ? hint.layoutRef : artifact.layoutRef;
    binding.meshRef = artifact.meshRef.isEmpty()
        ? (hint.meshRef.isEmpty() ? selected->meshRef : hint.meshRef)
        : artifact.meshRef;
    binding.artifactNodeCount = artifactNodeCount;
    binding.layoutNodeCount = layoutNodeCount(*selected);
    binding.valueCount = artifact.values.size();

    if (binding.layoutNodeCount != artifactNodeCount) {
        binding.ok = false;
        binding.message =
            QStringLiteral("场节点数与真实网格不一致：artifact=%1，layout=%2")
                .arg(artifactNodeCount)
                .arg(binding.layoutNodeCount);
        return binding;
    }

    binding.message = ambiguousGeometry
        ? QStringLiteral("按 node_count 选择共享几何 layout=%1；如需区分语义，请在对象包/field artifact 中补充 layout_ref/mesh_ref")
              .arg(binding.layoutName)
        : QStringLiteral("绑定 layout=%1，mesh=%2，nodes=%3")
              .arg(binding.layoutName, selected->meshRef)
              .arg(binding.layoutNodeCount);
    return binding;
}

} // namespace twin
