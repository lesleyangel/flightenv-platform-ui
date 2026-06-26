#pragma once

#include <EnvContracts/common/SubjectType.hpp>
#include <EnvContracts/dto/FieldBundleDTO.hpp>
#include <EnvContracts/dto/PredictionResultDTO.hpp>
#include <EnvContracts/dto/RuntimeSnapshotDTO.hpp>
#include <EnvNodeSupport/PlatformRunReader.h>

#include <QLabel>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>
#include <QVTKOpenGLNativeWidget.h>

#include <vtkActor.h>
#include <vtkCellArray.h>
#include <vtkDoubleArray.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkLookupTable.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkRenderer.h>
#include <vtkScalarBarActor.h>
#include <vtkSmartPointer.h>

#include <optional>
#include <vector>

namespace flightenv::ui::demo {

struct VtkFieldRenderStats {
    bool ok = false;
    QString message;
    int nodeCount = 0;
    int vertexCount = 0;
    int componentIndex = 0;
    double minValue = 0.0;
    double maxValue = 0.0;
    double meanValue = 0.0;
    int minNodeIndex = -1;
    int maxNodeIndex = -1;
};

class VtkModelFieldWidget final : public QWidget {
public:
    explicit VtkModelFieldWidget(QWidget* parent = nullptr);

    void setAssetRoot(const QString& root);
    bool setRuntimeSnapshot(const contracts::RuntimeSnapshotDTO& snapshot);
    void clearField(const QString& message);
    void setStatusMessage(const QString& message);
    bool hasRenderedValues() const;

    VtkFieldRenderStats renderPredictionResult(
        const contracts::PredictionResultDTO& prediction,
        contracts::SubjectType subject,
        int componentIndex,
        const QString& title);

    VtkFieldRenderStats renderFieldBundle(
        const contracts::FieldBundleDTO& bundle,
        contracts::SubjectType subject,
        int componentIndex,
        const QString& title,
        const QString& unitOverride = QString());

    VtkFieldRenderStats renderFlattenedValues(
        const std::vector<double>& flattenedValues,
        contracts::SubjectType subject,
        int valueDim,
        int componentIndex,
        const QString& title,
        const QString& unit);

    VtkFieldRenderStats renderPlatformFieldArtifact(
        const launchsupport::PlatformFieldArtifactView& field,
        const std::vector<double>& flattenedValues,
        int componentIndex,
        const QString& title = QString());

private:
    contracts::SubjectType inferSubject(const launchsupport::PlatformFieldArtifactView& field) const;
    const contracts::FieldLayoutDTO* findLayout(contracts::SubjectType subject) const;
    const contracts::MeshMetaDTO* findMesh(contracts::SubjectType subject) const;
    QString resolveMeshPath(const contracts::MeshMetaDTO* mesh) const;
    bool rebuildSurface(contracts::SubjectType subject, QString* error);
    bool loadIndexedSurface(const QString& path, const contracts::FieldLayoutDTO& layout, QString* error);
    VtkFieldRenderStats renderValues(
        const std::vector<double>& flattenedValues,
        int valueDim,
        int componentIndex,
        const QString& title,
        const QString& unit);
    void showMessage(const QString& message);

    QVBoxLayout* layout_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QVTKOpenGLNativeWidget* vtkWidget_ = nullptr;

    vtkSmartPointer<vtkRenderer> renderer_;
    vtkSmartPointer<vtkGenericOpenGLRenderWindow> renderWindow_;
    vtkSmartPointer<vtkPolyData> surface_;
    vtkSmartPointer<vtkPoints> points_;
    vtkSmartPointer<vtkCellArray> polys_;
    vtkSmartPointer<vtkDoubleArray> scalars_;
    vtkSmartPointer<vtkPolyDataMapper> mapper_;
    vtkSmartPointer<vtkActor> actor_;
    vtkSmartPointer<vtkLookupTable> lookup_;
    vtkSmartPointer<vtkScalarBarActor> scalarBar_;

    QString assetRoot_;
    std::optional<contracts::RuntimeSnapshotDTO> runtimeSnapshot_;
    contracts::SubjectType loadedSubject_ = contracts::SubjectType::P;
    bool hasLoadedSurface_ = false;
    bool hasRenderedValues_ = false;
    std::vector<int> vertexNodeIds_;
};

} // namespace flightenv::ui::demo
