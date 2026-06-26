#include "VtkModelFieldWidget.h"

#include <EnvContracts/dto/FieldLayoutDTO.hpp>
#include <EnvContracts/dto/MeshMetaDTO.hpp>

#include <QDir>
#include <QFileInfo>

#include <vtkCamera.h>
#include <vtkPointData.h>
#include <vtkProperty.h>
#include <vtkTriangle.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <numeric>
#include <sstream>

namespace flightenv::ui::demo {

namespace {

bool containsSubject(const std::vector<contracts::SubjectType>& subjects, contracts::SubjectType subject)
{
    return std::find(subjects.begin(), subjects.end(), subject) != subjects.end();
}

std::string lowerAscii(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return text;
}

QString subjectText(contracts::SubjectType subject)
{
    return QString::fromStdString(contracts::to_string(subject));
}

} // namespace

VtkModelFieldWidget::VtkModelFieldWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(620, 420);
    layout_ = new QVBoxLayout(this);
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(6);

    statusLabel_ = new QLabel(QStringLiteral("等待 RuntimeSnapshotDTO 和真实场值"));
    statusLabel_->setWordWrap(true);
    statusLabel_->setStyleSheet(QStringLiteral(
        "background:#fff7ed;border:1px solid #fed7aa;border-radius:4px;padding:6px;color:#9a3412;"));
    layout_->addWidget(statusLabel_);

    vtkWidget_ = new QVTKOpenGLNativeWidget(this);
    layout_->addWidget(vtkWidget_, 1);

    lookup_ = vtkSmartPointer<vtkLookupTable>::New();
    lookup_->SetHueRange(0.667, 0.0);
    lookup_->SetSaturationRange(1.0, 1.0);
    lookup_->SetValueRange(1.0, 1.0);
    lookup_->SetNumberOfTableValues(256);
    lookup_->SetTableRange(0.0, 1.0);
    lookup_->Build();

    surface_ = vtkSmartPointer<vtkPolyData>::New();
    points_ = vtkSmartPointer<vtkPoints>::New();
    polys_ = vtkSmartPointer<vtkCellArray>::New();
    scalars_ = vtkSmartPointer<vtkDoubleArray>::New();
    scalars_->SetName("FlightEnvFieldValues");
    scalars_->SetNumberOfComponents(1);

    mapper_ = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper_->SetInputData(surface_);
    mapper_->SetLookupTable(lookup_);
    mapper_->SetScalarModeToUsePointData();
    mapper_->InterpolateScalarsBeforeMappingOn();
    mapper_->ScalarVisibilityOn();
    mapper_->SetUseLookupTableScalarRange(true);

    actor_ = vtkSmartPointer<vtkActor>::New();
    actor_->SetMapper(mapper_);
    actor_->GetProperty()->SetInterpolationToPhong();

    scalarBar_ = vtkSmartPointer<vtkScalarBarActor>::New();
    scalarBar_->SetLookupTable(lookup_);
    scalarBar_->SetNumberOfLabels(5);
    scalarBar_->SetLabelFormat("%.3g");
    scalarBar_->SetOrientationToVertical();
    scalarBar_->SetWidth(0.10);
    scalarBar_->SetHeight(0.62);
    scalarBar_->SetPosition(0.02, 0.28);

    renderer_ = vtkSmartPointer<vtkRenderer>::New();
    renderer_->SetBackground(0xEE / 255.0, 0xEA / 255.0, 0xE3 / 255.0);
    renderer_->AddActor(actor_);
    renderer_->AddActor2D(scalarBar_);

    renderWindow_ = vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();
    renderWindow_->SetMultiSamples(0);
    renderWindow_->AddRenderer(renderer_);
    vtkWidget_->setRenderWindow(renderWindow_);
}

void VtkModelFieldWidget::setAssetRoot(const QString& root)
{
    assetRoot_ = root.trimmed();
}

bool VtkModelFieldWidget::setRuntimeSnapshot(const contracts::RuntimeSnapshotDTO& snapshot)
{
    runtimeSnapshot_ = snapshot;
    hasLoadedSurface_ = false;
    vertexNodeIds_.clear();
    showMessage(QStringLiteral("RuntimeSnapshot 已接收: fields=%1, meshes=%2")
                    .arg(snapshot.field_layouts.size())
                    .arg(snapshot.meshes.size()));
    return !snapshot.field_layouts.empty();
}

void VtkModelFieldWidget::clearField(const QString& message)
{
    scalars_->Reset();
    surface_->GetPointData()->SetScalars(nullptr);
    surface_->Modified();
    hasRenderedValues_ = false;
    showMessage(message);
    renderWindow_->Render();
}

void VtkModelFieldWidget::setStatusMessage(const QString& message)
{
    showMessage(message);
}

bool VtkModelFieldWidget::hasRenderedValues() const
{
    return hasRenderedValues_;
}

VtkFieldRenderStats VtkModelFieldWidget::renderPredictionResult(
    const contracts::PredictionResultDTO& prediction,
    contracts::SubjectType subject,
    int componentIndex,
    const QString& title)
{
    return renderFieldBundle(prediction.fields, subject, componentIndex, title);
}

VtkFieldRenderStats VtkModelFieldWidget::renderFieldBundle(
    const contracts::FieldBundleDTO& bundle,
    contracts::SubjectType subject,
    int componentIndex,
    const QString& title,
    const QString& unitOverride)
{
    VtkFieldRenderStats stats;
    stats.componentIndex = componentIndex;
    if (!runtimeSnapshot_.has_value()) {
        stats.message = QStringLiteral("缺少 RuntimeSnapshotDTO，无法把场值映射到飞船模型节点");
        clearField(stats.message);
        return stats;
    }

    const auto* layout = findLayout(subject);
    if (layout == nullptr) {
        stats.message = QStringLiteral("RuntimeSnapshot 中没有 subject=%1 的 FieldLayoutDTO").arg(subjectText(subject));
        clearField(stats.message);
        return stats;
    }

    const auto itemIt = std::find_if(bundle.items.begin(), bundle.items.end(), [&](const contracts::SubjectFieldDTO& item) {
        return item.subject == subject;
    });
    if (itemIt == bundle.items.end()) {
        stats.message = QStringLiteral("FieldBundleDTO 中没有 subject=%1 的场值").arg(subjectText(subject));
        clearField(stats.message);
        return stats;
    }

    QString error;
    if (!hasLoadedSurface_ || loadedSubject_ != subject) {
        if (!rebuildSurface(subject, &error)) {
            stats.message = error;
            clearField(stats.message);
            return stats;
        }
    }

    const QString unit = unitOverride.isEmpty() ? QString::fromStdString(layout->unit) : unitOverride;
    return renderValues(itemIt->values, layout->value_dim, componentIndex, title, unit);
}

VtkFieldRenderStats VtkModelFieldWidget::renderFlattenedValues(
    const std::vector<double>& flattenedValues,
    contracts::SubjectType subject,
    int valueDim,
    int componentIndex,
    const QString& title,
    const QString& unit)
{
    VtkFieldRenderStats stats;
    stats.componentIndex = componentIndex;
    if (!runtimeSnapshot_.has_value()) {
        stats.message = QStringLiteral("缺少 RuntimeSnapshotDTO，无法把 artifact 场值映射到飞船模型节点");
        clearField(stats.message);
        return stats;
    }

    QString error;
    if (!hasLoadedSurface_ || loadedSubject_ != subject) {
        if (!rebuildSurface(subject, &error)) {
            stats.message = error;
            clearField(stats.message);
            return stats;
        }
    }

    return renderValues(flattenedValues, valueDim, componentIndex, title, unit);
}

VtkFieldRenderStats VtkModelFieldWidget::renderPlatformFieldArtifact(
    const launchsupport::PlatformFieldArtifactView& field,
    const std::vector<double>& flattenedValues,
    int componentIndex,
    const QString& title)
{
    const auto subject = inferSubject(field);
    int valueDim = 1;
    if (field.node_count > 0 &&
        flattenedValues.size() >= static_cast<std::size_t>(field.node_count) &&
        flattenedValues.size() % static_cast<std::size_t>(field.node_count) == 0) {
        valueDim = static_cast<int>(flattenedValues.size() / static_cast<std::size_t>(field.node_count));
    }

    QString displayTitle = title;
    if (displayTitle.isEmpty()) {
        displayTitle = QString::fromStdString(field.field_name.empty() ? field.port_id : field.field_name);
        if (!field.component_id.empty()) {
            displayTitle += QStringLiteral(" / ") + QString::fromStdString(field.component_id);
        }
    }
    return renderFlattenedValues(
        flattenedValues,
        subject,
        valueDim,
        componentIndex,
        displayTitle,
        QString::fromStdString(field.unit));
}

contracts::SubjectType VtkModelFieldWidget::inferSubject(
    const launchsupport::PlatformFieldArtifactView& field) const
{
    const std::string text = lowerAscii(
        field.field_name + " " +
        field.field_role + " " +
        field.port_id + " " +
        field.contract_id + " " +
        field.component_id + " " +
        field.mesh_ref + " " +
        field.layout_ref);

    if (text.find("heatflux") != std::string::npos ||
        text.find("heat_flux") != std::string::npos ||
        text.find("ablation") != std::string::npos) {
        return contracts::SubjectType::K;
    }
    if (text.find("temperature") != std::string::npos ||
        text.find("thermal") != std::string::npos) {
        return contracts::SubjectType::T;
    }
    if (text.find("strain") != std::string::npos ||
        text.find("mises") != std::string::npos ||
        text.find("damage") != std::string::npos ||
        text.find("life") != std::string::npos ||
        text.find("rul") != std::string::npos ||
        text.find("structure") != std::string::npos) {
        return contracts::SubjectType::S;
    }
    if (text.find("pressure") != std::string::npos ||
        text.find("shell") != std::string::npos) {
        return contracts::SubjectType::P;
    }

    if (runtimeSnapshot_.has_value() && field.node_count > 0) {
        for (const auto& layout : runtimeSnapshot_->field_layouts) {
            const auto layoutNodeCount = layout.node_count > 0 ? layout.node_count : layout.nodes.size();
            if (static_cast<std::int64_t>(layoutNodeCount) == field.node_count) {
                return layout.subject;
            }
        }
    }
    return contracts::SubjectType::P;
}

const contracts::FieldLayoutDTO* VtkModelFieldWidget::findLayout(contracts::SubjectType subject) const
{
    if (!runtimeSnapshot_.has_value()) {
        return nullptr;
    }
    const auto& layouts = runtimeSnapshot_->field_layouts;
    const auto it = std::find_if(layouts.begin(), layouts.end(), [&](const contracts::FieldLayoutDTO& layout) {
        return layout.subject == subject;
    });
    return it == layouts.end() ? nullptr : &(*it);
}

const contracts::MeshMetaDTO* VtkModelFieldWidget::findMesh(contracts::SubjectType subject) const
{
    if (!runtimeSnapshot_.has_value()) {
        return nullptr;
    }
    const auto& meshes = runtimeSnapshot_->meshes;
    const auto it = std::find_if(meshes.begin(), meshes.end(), [&](const contracts::MeshMetaDTO& mesh) {
        return containsSubject(mesh.subjects, subject);
    });
    if (it != meshes.end()) {
        return &(*it);
    }
    return meshes.empty() ? nullptr : &meshes.front();
}

QString VtkModelFieldWidget::resolveMeshPath(const contracts::MeshMetaDTO* mesh) const
{
    if (mesh == nullptr || mesh->path.empty()) {
        return {};
    }

    const QString path = QString::fromStdString(mesh->path);
    const QFileInfo direct(path);
    if (direct.isAbsolute() && direct.exists()) {
        return direct.absoluteFilePath();
    }

    const QStringList roots{
        assetRoot_,
        QDir::current().absoluteFilePath(QStringLiteral("_deps/example")),
        QDir::current().absoluteFilePath(QStringLiteral("_deps/data")),
        QDir::currentPath()
    };
    for (const QString& root : roots) {
        if (root.isEmpty()) {
            continue;
        }
        const QFileInfo candidate(QDir(root).absoluteFilePath(path));
        if (candidate.exists()) {
            return candidate.absoluteFilePath();
        }
    }
    return {};
}

bool VtkModelFieldWidget::rebuildSurface(contracts::SubjectType subject, QString* error)
{
    const auto* layout = findLayout(subject);
    if (layout == nullptr) {
        if (error) {
            *error = QStringLiteral("缺少 subject=%1 的场布局").arg(subjectText(subject));
        }
        return false;
    }
    if (layout->nodes.empty()) {
        if (error) {
            *error = QStringLiteral("subject=%1 的场布局没有节点坐标").arg(subjectText(subject));
        }
        return false;
    }

    const auto* mesh = findMesh(subject);
    const QString meshPath = resolveMeshPath(mesh);
    if (meshPath.isEmpty()) {
        if (error) {
            *error = QStringLiteral("缺少 subject=%1 的真实飞船模型面片索引文件，不能绘制场云图").arg(subjectText(subject));
        }
        return false;
    }

    if (!loadIndexedSurface(meshPath, *layout, error)) {
        if (error && error->isEmpty()) {
            *error = QStringLiteral("无法加载真实飞船模型面片索引文件: %1").arg(meshPath);
        }
        return false;
    }

    loadedSubject_ = subject;
    hasLoadedSurface_ = true;
    return true;
}

bool VtkModelFieldWidget::loadIndexedSurface(const QString& path, const contracts::FieldLayoutDTO& layout, QString* error)
{
    std::ifstream file(path.toStdString());
    if (!file.is_open()) {
        if (error) {
            *error = QStringLiteral("无法打开模型面片索引文件: %1").arg(path);
        }
        return false;
    }

    points_->Reset();
    polys_->Reset();
    vertexNodeIds_.clear();

    std::string line;
    int validFaces = 0;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        int faceIndex = 0;
        vtkIdType p1 = 0;
        vtkIdType p2 = 0;
        vtkIdType p3 = 0;
        if (!(iss >> faceIndex >> p1 >> p2 >> p3)) {
            continue;
        }

        const std::size_t idx1 = static_cast<std::size_t>(p1 - 1);
        const std::size_t idx2 = static_cast<std::size_t>(p2 - 1);
        const std::size_t idx3 = static_cast<std::size_t>(p3 - 1);
        if (idx1 >= layout.nodes.size() || idx2 >= layout.nodes.size() || idx3 >= layout.nodes.size()) {
            continue;
        }

        const auto& n1 = layout.nodes[idx1];
        const auto& n2 = layout.nodes[idx2];
        const auto& n3 = layout.nodes[idx3];
        const vtkIdType id1 = points_->InsertNextPoint(n1.x, n1.y, n1.z);
        const vtkIdType id2 = points_->InsertNextPoint(n2.x, n2.y, n2.z);
        const vtkIdType id3 = points_->InsertNextPoint(n3.x, n3.y, n3.z);

        vtkSmartPointer<vtkTriangle> triangle = vtkSmartPointer<vtkTriangle>::New();
        triangle->GetPointIds()->SetId(0, id1);
        triangle->GetPointIds()->SetId(1, id2);
        triangle->GetPointIds()->SetId(2, id3);
        polys_->InsertNextCell(triangle);
        vertexNodeIds_.push_back(static_cast<int>(idx1));
        vertexNodeIds_.push_back(static_cast<int>(idx2));
        vertexNodeIds_.push_back(static_cast<int>(idx3));
        ++validFaces;
    }

    if (validFaces <= 0 || vertexNodeIds_.empty()) {
        if (error) {
            *error = QStringLiteral("模型面片索引文件没有可用三角面: %1").arg(path);
        }
        return false;
    }

    surface_->SetPoints(points_);
    surface_->SetPolys(polys_);
    surface_->SetVerts(nullptr);
    surface_->Modified();
    actor_->GetProperty()->SetRepresentationToSurface();
    renderer_->ResetCamera();
    renderer_->GetActiveCamera()->Zoom(1.5);
    return true;
}

VtkFieldRenderStats VtkModelFieldWidget::renderValues(
    const std::vector<double>& flattenedValues,
    int valueDim,
    int componentIndex,
    const QString& title,
    const QString& unit)
{
    VtkFieldRenderStats stats;
    stats.componentIndex = componentIndex;
    stats.vertexCount = static_cast<int>(vertexNodeIds_.size());
    if (vertexNodeIds_.empty()) {
        stats.message = QStringLiteral("模型表面未加载，不能渲染场");
        return stats;
    }

    const auto* layout = findLayout(loadedSubject_);
    const int nodeCount = layout ? static_cast<int>(layout->nodes.size()) : 0;
    stats.nodeCount = nodeCount;
    if (nodeCount <= 0) {
        stats.message = QStringLiteral("RuntimeSnapshot 节点数为 0");
        clearField(stats.message);
        return stats;
    }

    int dim = valueDim;
    if (dim <= 0 && flattenedValues.size() % static_cast<std::size_t>(nodeCount) == 0) {
        dim = static_cast<int>(flattenedValues.size() / static_cast<std::size_t>(nodeCount));
    }
    if (dim <= 0) {
        stats.message = QStringLiteral("无法从场值长度推断 value_dim: values=%1 nodes=%2")
                            .arg(flattenedValues.size())
                            .arg(nodeCount);
        clearField(stats.message);
        return stats;
    }
    if (componentIndex < 0 || componentIndex >= dim) {
        stats.message = QStringLiteral("componentIndex 越界: component=%1 value_dim=%2").arg(componentIndex).arg(dim);
        clearField(stats.message);
        return stats;
    }
    if (flattenedValues.size() < static_cast<std::size_t>(nodeCount * dim)) {
        stats.message = QStringLiteral("场值数量不足: values=%1 nodes=%2 value_dim=%3")
                            .arg(flattenedValues.size())
                            .arg(nodeCount)
                            .arg(dim);
        clearField(stats.message);
        return stats;
    }

    std::vector<double> nodeValues(static_cast<std::size_t>(nodeCount), 0.0);
    double minValue = std::numeric_limits<double>::max();
    double maxValue = std::numeric_limits<double>::lowest();
    double sum = 0.0;
    for (int node = 0; node < nodeCount; ++node) {
        const auto offset = static_cast<std::size_t>(node * dim + componentIndex);
        const double value = std::isfinite(flattenedValues[offset]) ? flattenedValues[offset] : 0.0;
        nodeValues[static_cast<std::size_t>(node)] = value;
        sum += value;
        if (value < minValue) {
            minValue = value;
            stats.minNodeIndex = node;
        }
        if (value > maxValue) {
            maxValue = value;
            stats.maxNodeIndex = node;
        }
    }
    if (minValue >= maxValue) {
        minValue -= 0.5;
        maxValue += 0.5;
    }

    scalars_->SetNumberOfComponents(1);
    scalars_->SetNumberOfTuples(static_cast<vtkIdType>(vertexNodeIds_.size()));
    for (vtkIdType i = 0; i < static_cast<vtkIdType>(vertexNodeIds_.size()); ++i) {
        const int nodeId = vertexNodeIds_[static_cast<std::size_t>(i)];
        scalars_->SetValue(i, nodeValues[static_cast<std::size_t>(nodeId)]);
    }
    scalars_->Modified();
    surface_->GetPointData()->SetScalars(scalars_);
    surface_->Modified();

    lookup_->SetRange(minValue, maxValue);
    lookup_->SetTableRange(minValue, maxValue);
    lookup_->Build();
    mapper_->SetScalarRange(minValue, maxValue);
    mapper_->Modified();
    scalarBar_->SetTitle((title + (unit.isEmpty() ? QString() : QStringLiteral(" / ") + unit)).toUtf8().constData());
    scalarBar_->SetLookupTable(lookup_);
    scalarBar_->Modified();
    renderWindow_->Render();

    hasRenderedValues_ = true;
    stats.ok = true;
    stats.minValue = minValue;
    stats.maxValue = maxValue;
    stats.meanValue = sum / std::max(1, nodeCount);
    stats.message = QStringLiteral("VTK 场已渲染: %1, subject=%2, nodes=%3, vertices=%4")
                        .arg(title, subjectText(loadedSubject_))
                        .arg(nodeCount)
                        .arg(vertexNodeIds_.size());
    showMessage(stats.message);
    return stats;
}

void VtkModelFieldWidget::showMessage(const QString& message)
{
    statusLabel_->setText(message);
}

} // namespace flightenv::ui::demo
