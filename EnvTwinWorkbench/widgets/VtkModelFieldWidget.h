#pragma once

#include "../datahub/PlatformMeshLayoutReader.h"

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
    bool setMeshLayoutCatalog(const twin::PlatformMeshLayoutCatalog& catalog);
    void clearField(const QString& message);
    VtkFieldRenderStats renderGeometryPreview(
        const QString& layoutId,
        const QString& title);

    VtkFieldRenderStats renderFlattenedValues(
        const std::vector<double>& flattenedValues,
        const QString& layoutId,
        int valueDim,
        int componentIndex,
        const QString& title,
        const QString& unit);

private:
    const twin::PlatformMeshLayoutView* findLayout(const QString& layoutId) const;
    bool rebuildSurface(const QString& layoutId, QString* error);
    bool loadIndexedSurface(const QString& path, const twin::PlatformMeshLayoutView& layout, QString* error);
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
    std::optional<twin::PlatformMeshLayoutCatalog> meshCatalog_;
    QString loadedLayoutId_;
    bool hasLoadedSurface_ = false;
    std::vector<int> vertexNodeIds_;
};

} // namespace flightenv::ui::demo
