#include "OperatorRenderer.h"

#include "../datahub/FieldRenderGuard.h"
#include "../datahub/PlatformMeshLayoutReader.h"
#include "GraphWorkflowDisplayWidgets.h"
#include "KvList.h"
#include "VtkModelFieldWidget.h"

#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QVBoxLayout>

#include <optional>
#include <vector>

namespace twin {
namespace {

constexpr const char* kGenericRenderer = "generic_operator_ports.v1";

std::optional<PdkDataPlaneEntryView> resolveEntryForPort(
    const OperatorRenderContext& ctx,
    const QString& portId) {
    for (const PdkDataPlaneEntryView& e : ctx.dataPlane) {
        if (e.operatorId == ctx.op.operatorId &&
            e.direction == QStringLiteral("output") &&
            e.portId == portId &&
            !e.artifactUri.isEmpty()) {
            return e;
        }
    }
    return std::nullopt;
}

QString resolveArtifactForEntry(const OperatorRenderContext& ctx, const PdkDataPlaneEntryView& entry) {
    const QString uri = QDir::fromNativeSeparators(entry.artifactUri.isEmpty() ? entry.ref : entry.artifactUri);
    if (QFileInfo(uri).isAbsolute() && QFileInfo::exists(uri)) {
        return uri;
    }
    const QString joined = QDir(ctx.evidenceRoot).filePath(uri);
    return QFileInfo::exists(joined) ? joined : QString();
}

QLabel* placeholder(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setAlignment(Qt::AlignCenter);
    label->setWordWrap(true);
    label->setProperty("muted", true);
    return label;
}

class GenericOperatorPortsRenderer final : public IOperatorRenderer {
public:
    QString rendererId() const override { return QString::fromLatin1(kGenericRenderer); }

    QWidget* createWidget(const OperatorRenderContext& ctx, QWidget* parent) const override {
        auto* host = new QWidget(parent);
        auto* layout = new QVBoxLayout(host);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(8);

        auto* kv = new KvList(host);
        kv->addRow(QStringLiteral("算子族"), ctx.op.operatorFamily, false);
        kv->addRow(QStringLiteral("执行后端"), ctx.op.executionKind, false);
        kv->addRow(QStringLiteral("适配器"), ctx.op.adapterId);
        kv->addRow(QStringLiteral("声明 renderer"),
                   ctx.op.display.rendererId.isEmpty() ? QStringLiteral("未声明") : ctx.op.display.rendererId);
        layout->addWidget(kv);

        QStringList lines;
        lines << QStringLiteral("输入端口:");
        for (const PdkPortView& p : ctx.op.inputs) {
            lines << QStringLiteral("  - %1 [%2]").arg(p.portId, p.contractId);
        }
        lines << QStringLiteral("输出端口:");
        for (const PdkPortView& p : ctx.op.outputs) {
            lines << QStringLiteral("  - %1 [%2]").arg(p.portId, p.contractId);
        }
        if (!ctx.op.resourceRefs.isEmpty()) {
            lines << QStringLiteral("资源引用:");
            for (const QString& r : ctx.op.resourceRefs) {
                lines << QStringLiteral("  - %1").arg(r);
            }
        }
        auto* portText = new QLabel(lines.join(QStringLiteral("\n")), host);
        portText->setProperty("mono", true);
        portText->setWordWrap(true);
        portText->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(portText);
        layout->addStretch(1);
        return host;
    }
};

class VtkScalarFieldRenderer final : public IOperatorRenderer {
public:
    QString rendererId() const override { return QStringLiteral("field.vtk.scalar.v1"); }

    QWidget* createWidget(const OperatorRenderContext& ctx, QWidget* parent) const override {
        using flightenv::ui::demo::VtkModelFieldWidget;
        auto* host = new QWidget(parent);
        auto* layout = new QVBoxLayout(host);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(6);

        auto* note = new QLabel(host);
        note->setProperty("mono", true);
        note->setProperty("tiny", true);
        note->setWordWrap(true);
        const auto showPlaceholder = [layout, note, host](const QString& message, const QString& detail) {
            auto* placeholder = new QLabel(message, host);
            placeholder->setWordWrap(true);
            placeholder->setAlignment(Qt::AlignCenter);
            placeholder->setMinimumHeight(260);
            placeholder->setStyleSheet(QStringLiteral(
                "QLabel { background:#f5f7fa; border:1px dashed #c7d0dc; "
                "color:#7a8798; padding:18px; }"));
            layout->addWidget(placeholder, 1);
            note->setText(detail);
            layout->addWidget(note);
        };

        const QString port = ctx.op.display.valueRefPort.isEmpty()
            ? (ctx.op.display.primaryOutputs.isEmpty() ? QString() : ctx.op.display.primaryOutputs.front())
            : ctx.op.display.valueRefPort;
        const QString snapshotPath = QFileInfo::exists(QDir(ctx.evidenceRoot).filePath(QStringLiteral("runtime_snapshot.json")))
            ? QDir(ctx.evidenceRoot).filePath(QStringLiteral("runtime_snapshot.json"))
            : QString();
        const PlatformMeshLayoutCatalog meshCatalog =
            PlatformMeshLayoutReader().read(ctx.objectPackageRoot, snapshotPath);
        const std::optional<PdkDataPlaneEntryView> entry = port.isEmpty()
            ? std::nullopt
            : resolveEntryForPort(ctx, port);
        const QString artifactPath = entry ? resolveArtifactForEntry(ctx, *entry) : QString();

        if (!meshCatalog.ok() || !entry || artifactPath.isEmpty()) {
            showPlaceholder(
                QStringLiteral("场展示待绑定：需要对象包 mesh layout 和 field artifact"),
                QStringLiteral("port=%1, mesh_layout=%2, artifact=%3")
                    .arg(port.isEmpty() ? QStringLiteral("未声明") : port,
                         meshCatalog.ok() ? QStringLiteral("已加载") : QStringLiteral("缺失"),
                         artifactPath.isEmpty() ? QStringLiteral("缺失") : artifactPath));
            return host;
        }

        const PdkFieldArtifactView artifact = PdkFieldArtifactReader().read(artifactPath);
        const FieldRenderBinding binding =
            bindFieldArtifactForRendering(artifact, meshCatalog, fieldRenderHintFromEntry(*entry));
        if (!binding.ok) {
            showPlaceholder(
                QStringLiteral("场/网格绑定失败"),
                QStringLiteral("%1\nartifact=%2").arg(binding.message, artifactPath));
            return host;
        }

        auto* vtk = new VtkModelFieldWidget(host);
        vtk->setMinimumHeight(260);
        layout->addWidget(vtk, 1);
        layout->addWidget(note);
        vtk->setMeshLayoutCatalog(meshCatalog);
        std::vector<double> values(artifact.values.begin(), artifact.values.end());
        const auto stats = vtk->renderFlattenedValues(
            values,
            binding.layoutId,
            1,
            0,
            ctx.op.display.displayTitle.isEmpty() ? ctx.op.operatorId : ctx.op.display.displayTitle,
            artifact.unit);
        if (!stats.ok) {
            note->setText(QStringLiteral("场渲染失败：%1").arg(stats.message));
        } else {
            note->setText(QStringLiteral("%1 · %2 · nodes=%3 · min=%4 max=%5 · unit=%6")
                              .arg(artifact.fieldName,
                                   binding.message)
                              .arg(stats.nodeCount)
                              .arg(stats.minValue, 0, 'g', 4)
                              .arg(stats.maxValue, 0, 'g', 4)
                              .arg(artifact.unit.isEmpty() ? QStringLiteral("-") : artifact.unit));
        }
        return host;
    }
};

class ScalarSeriesRenderer final : public IOperatorRenderer {
public:
    explicit ScalarSeriesRenderer(QString id) : id_(std::move(id)) {}
    QString rendererId() const override { return id_; }

    QWidget* createWidget(const OperatorRenderContext& ctx, QWidget* parent) const override {
        using flightenv::ui::display::ScalarTrendWidget;
        auto* host = new QWidget(parent);
        auto* layout = new QVBoxLayout(host);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(6);

        auto* trend = new ScalarTrendWidget(host);
        trend->setMinimumHeight(140);
        const QString seriesName = ctx.op.display.series.isEmpty()
            ? ctx.op.operatorId
            : ctx.op.display.series.front();
        trend->setTitle(seriesName, QString());
        layout->addWidget(trend, 1);

        auto* note = new QLabel(host);
        note->setProperty("muted", true);
        note->setProperty("tiny", true);
        note->setWordWrap(true);
        const QString port = ctx.op.display.primaryOutputs.isEmpty()
            ? QString()
            : ctx.op.display.primaryOutputs.front();
        note->setText(QStringLiteral("序列: %1 · 主输出: %2 · 运行后由 DataPlane/health_trend 填充")
                          .arg(ctx.op.display.series.join(QStringLiteral(", ")),
                               port.isEmpty() ? QStringLiteral("未声明") : port));
        layout->addWidget(note);
        return host;
    }

private:
    QString id_;
};

} // namespace

RendererRegistry& RendererRegistry::instance() {
    static RendererRegistry registry;
    return registry;
}

RendererRegistry::RendererRegistry() {
    ensureDefaults();
}

void RendererRegistry::ensureDefaults() {
    registerRenderer(std::make_unique<GenericOperatorPortsRenderer>());
    registerRenderer(std::make_unique<VtkScalarFieldRenderer>());
    registerRenderer(std::make_unique<ScalarSeriesRenderer>(QStringLiteral("field.coefficient_series.v1")));
    registerRenderer(std::make_unique<ScalarSeriesRenderer>(QStringLiteral("health.accumulation.v1")));
    registerRenderer(std::make_unique<ScalarSeriesRenderer>(QStringLiteral("trajectory.timeline.v1")));
    registerRenderer(std::make_unique<ScalarSeriesRenderer>(QStringLiteral("sensor.projection.v1")));
    registerRenderer(std::make_unique<ScalarSeriesRenderer>(QStringLiteral("sensor.stream.v1")));
    registerRenderer(std::make_unique<ScalarSeriesRenderer>(QStringLiteral("filter.particle_summary.v1")));
    registerRenderer(std::make_unique<ScalarSeriesRenderer>(QStringLiteral("qoi.decision.v1")));
}

void RendererRegistry::registerRenderer(std::unique_ptr<IOperatorRenderer> renderer) {
    if (!renderer) {
        return;
    }
    const QString id = renderer->rendererId();
    renderers_.insert(id, std::shared_ptr<IOperatorRenderer>(std::move(renderer)));
}

bool RendererRegistry::hasRenderer(const QString& rendererId) const {
    return renderers_.contains(rendererId);
}

RendererMatch RendererRegistry::resolve(const PdkOperatorView& op) const {
    RendererMatch match;
    match.requestedRendererId = op.display.rendererId;
    if (!op.display.rendererId.isEmpty() && renderers_.contains(op.display.rendererId)) {
        match.resolvedRendererId = op.display.rendererId;
        match.usedGeneric = (op.display.rendererId == QString::fromLatin1(kGenericRenderer));
        return match;
    }
    if (!op.display.fallbackRenderer.isEmpty() && renderers_.contains(op.display.fallbackRenderer)) {
        match.resolvedRendererId = op.display.fallbackRenderer;
        match.usedFallback = true;
        match.usedGeneric = (op.display.fallbackRenderer == QString::fromLatin1(kGenericRenderer));
        return match;
    }
    match.resolvedRendererId = QString::fromLatin1(kGenericRenderer);
    match.usedFallback = !op.display.rendererId.isEmpty();
    match.usedGeneric = true;
    return match;
}

QWidget* RendererRegistry::render(const OperatorRenderContext& ctx, QWidget* parent) const {
    const RendererMatch match = resolve(ctx.op);
    const auto it = renderers_.find(match.resolvedRendererId);
    if (it != renderers_.end()) {
        if (QWidget* w = it.value()->createWidget(ctx, parent)) {
            return w;
        }
    }
    return renderers_.value(QString::fromLatin1(kGenericRenderer))->createWidget(ctx, parent);
}

} // namespace twin
