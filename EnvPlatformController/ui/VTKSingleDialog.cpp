
#include "VTKSingleDialog.h"
#include <vtkTriangle.h>
#include <vtkNew.h>
#include <vtkObject.h>
#include <vtkOutputWindow.h>
#include <QSurfaceFormat>
#include <fstream>
#include <sstream>
#include <QThread>
#include <QMetaObject>
#include <cmath>
#include <QDebug>
#include <QLabel>
#include <QStyleOption>
#include <QPainter>
#include <QPointer>
#include <algorithm>

VTKSingleDialog::VTKSingleDialog(
    const std::string& windowTitle,
    const std::string& modelPath,
    QWidget* parent
) : QWidget(parent), windowTitle(windowTitle), modelPath(modelPath) {
    // 窗口基础配置
    this->setWindowTitle(QString::fromStdString(windowTitle));
    this->resize(parent->size());
    this->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // 先配置OpenGL格式
    QSurfaceFormat fmt = QVTKOpenGLNativeWidget::defaultFormat();
    fmt.setAlphaBufferSize(0);
    fmt.setSamples(0);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setVersion(3, 3);
    QSurfaceFormat::setDefaultFormat(fmt);

    // 初始化
    mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(0);

    // 创建背景
    QWidget* backgroundContainer = new QWidget(this);
    backgroundContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    backgroundContainer->setStyleSheet(R"(
        QWidget {
            background-color: #EEEAE3; /* 统一背景色 */
        }
    )");

    // 背景容器的子布局
    QVBoxLayout* vtkLayout = new QVBoxLayout(backgroundContainer);
    vtkLayout->setContentsMargins(15, 15, 15, 15);
    vtkLayout->setSpacing(0);

    // 创建控件
    vtkWidget = new QVTKOpenGLNativeWidget(this);
    vtkWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    vtkWidget->setMinimumSize(100, 80);
    // 设置背景
    vtkWidget->setStyleSheet("QWidget { background-color: transparent; }");

    // 将VTK控件添加到背景容器的子布局
    vtkLayout->addWidget(vtkWidget);
    // 将背景容器添加到主布局
    mainLayout->addWidget(backgroundContainer);

    // 初始化组件
    camera = vtkSmartPointer<vtkCamera>::New();
    initVTKComponents();
}

// 初始化VTK组件
void VTKSingleDialog::initVTKComponents() {
    vtkObject::GlobalWarningDisplayOff();


    // 颜色表
    colorTable = vtkSmartPointer<vtkLookupTable>::New();
    colorTable->SetHueRange(0.667, 0.0);
    colorTable->SetSaturationRange(1.0, 1.0);
    colorTable->SetValueRange(1.0, 1.0);
    colorTable->SetNumberOfTableValues(256);
    colorTable->SetTableRange(0.0, 1.0);
    colorTable->Build();

    // 模型数据容器
    surface = vtkSmartPointer<vtkPolyData>::New();
    dynamicPoints = vtkSmartPointer<vtkPoints>::New();
    polys = vtkSmartPointer<vtkCellArray>::New();

    vertexValues = vtkSmartPointer<vtkDoubleArray>::New();
    vertexValues->SetName("VertexValues");
    vertexValues->SetNumberOfComponents(1);

    // Mapper
    mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputData(surface);
    mapper->SetScalarModeToUsePointData();
    mapper->InterpolateScalarsBeforeMappingOn();
    mapper->SetLookupTable(colorTable);
    mapper->ScalarVisibilityOn();
    mapper->SetUseLookupTableScalarRange(true);

    // Actor
    actor = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);
    actor->GetProperty()->SetInterpolationToPhong();
    actor->GetProperty()->SetLineWidth(1.5);

    QVBoxLayout* mainLayout = qobject_cast<QVBoxLayout*>(this->layout());
    if (!mainLayout) {
        mainLayout = new QVBoxLayout(this);
        mainLayout->setContentsMargins(10, 10, 10, 10);
        mainLayout->setSpacing(0);
    }

    scalarBar = vtkSmartPointer<vtkScalarBarActor>::New();
    scalarBar->SetLookupTable(colorTable);
    scalarBar->SetNumberOfLabels(4);
    scalarBar->SetLabelFormat("%.3g");
    scalarBar->SetOrientationToVertical();
    scalarBar->UnconstrainedFontSizeOn();

    // 标题文字属性
    vtkSmartPointer<vtkTextProperty> titleProp = vtkSmartPointer<vtkTextProperty>::New();
    titleProp->SetFontFamilyToArial();
    titleProp->SetFontSize(10);
    titleProp->SetBold(true);
    titleProp->SetColor(0, 0, 0);
    scalarBar->SetTitleTextProperty(titleProp);

    // 标签文字属性
    vtkSmartPointer<vtkTextProperty> labelProp = vtkSmartPointer<vtkTextProperty>::New();
    labelProp->SetFontFamilyToArial();
    labelProp->SetFontSize(11);
    labelProp->SetBold(false);
    labelProp->SetColor(0, 0, 0);
    scalarBar->SetLabelTextProperty(labelProp);

    updateScalarBarLayout();

    renderer = vtkSmartPointer<vtkRenderer>::New();
    renderer->AddActor(actor);
    renderer->AddActor2D(scalarBar);

    renderer->SetBackground(0xEE / 255.0, 0xEA / 255.0, 0xE3 / 255.0);

    renderWindow = vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();
    renderWindow->AddRenderer(renderer);
    renderWindow->SetAlphaBitPlanes(false);
    renderWindow->SetMultiSamples(0);

    if (renderWindow)
        vtkWidget->setRenderWindow(renderWindow);
    else
        qCritical() << "vtkGenericOpenGLRenderWindow 创建失败，VTK 初始化终止";

    // 标记点组件初始化
    markerPoints = vtkSmartPointer<vtkPoints>::New();
    markerPolyData = vtkSmartPointer<vtkPolyData>::New();
    markerPolyData->SetPoints(markerPoints);

    markerSphere = vtkSmartPointer<vtkSphereSource>::New();
    markerSphere->SetRadius(0.06);
    markerSphere->SetPhiResolution(12);
    markerSphere->SetThetaResolution(12);

    markerGlyph = vtkSmartPointer<vtkGlyph3D>::New();
    markerGlyph->SetSourceConnection(markerSphere->GetOutputPort());
    markerGlyph->SetInputData(markerPolyData);
    markerGlyph->SetScaleFactor(1.0);

    markerMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    markerMapper->SetInputConnection(markerGlyph->GetOutputPort());
    markerMapper->SetScalarRange(0, 1);

    markerActor = vtkSmartPointer<vtkActor>::New();
    markerActor->SetMapper(markerMapper);
    markerActor->GetProperty()->SetColor(0.0, 1.0, 1.0);
    markerActor->GetProperty()->EdgeVisibilityOn();
    markerActor->GetProperty()->SetEdgeColor(0.0, 0.0, 0.0);
    markerActor->GetProperty()->SetLineWidth(4.0);
    markerActor->GetProperty()->SetOpacity(1.0);
    markerActor->GetProperty()->SetSpecular(0.8);
    markerActor->GetProperty()->SetSpecularPower(30);
    renderer->AddActor(markerActor);
}

// 重写paintEvent
void VTKSingleDialog::paintEvent(QPaintEvent* event)
{
    QStyleOption opt;
    opt.initFrom(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
    QWidget::paintEvent(event);
}

bool VTKSingleDialog::RemoveActor() {
    renderer->RemoveActor2D(scalarBar);
    return 0;
}

void VTKSingleDialog::updateScalarBarLayout()
{
    if (!scalarBar) {
        return;
    }

    int widthPx = vtkWidget ? vtkWidget->width() : 0;
    int heightPx = vtkWidget ? vtkWidget->height() : 0;
    if ((widthPx <= 0 || heightPx <= 0) && renderWindow) {
        int* size = renderWindow->GetSize();
        widthPx = size ? size[0] : widthPx;
        heightPx = size ? size[1] : heightPx;
    }
    if (widthPx <= 0 || heightPx <= 0) {
        widthPx = 360;
        heightPx = 240;
    }

    const int barWidthPx = std::clamp(widthPx / 9, 58, 82);
    const int barHeightPx = std::clamp(heightPx * 3 / 5, 118, std::max(118, heightPx - 30));
    const int marginPx = 12;
    const int yPx = std::max(8, (heightPx - barHeightPx) / 2);

    scalarBar->SetPosition(
        static_cast<double>(marginPx) / static_cast<double>(widthPx),
        static_cast<double>(yPx) / static_cast<double>(heightPx));
    scalarBar->SetWidth(static_cast<double>(barWidthPx) / static_cast<double>(widthPx));
    scalarBar->SetHeight(static_cast<double>(barHeightPx) / static_cast<double>(heightPx));
    scalarBar->SetNumberOfLabels(4);
    scalarBar->SetLabelFormat("%.3g");

    if (auto* title = scalarBar->GetTitleTextProperty()) {
        title->SetFontFamilyToArial();
        title->SetFontSize(10);
        title->SetBold(true);
    }
    if (auto* label = scalarBar->GetLabelTextProperty()) {
        label->SetFontFamilyToArial();
        label->SetFontSize(11);
        label->SetBold(false);
    }
    scalarBar->Modified();
}

void VTKSingleDialog::setRuntimeView(std::shared_ptr<const launchsupport::RuntimeViewModel> view) {
    runtimeView = std::move(view);
}

bool VTKSingleDialog::appendModel(const std::string& modelName, const std::string& appendedModelPath) {

    if (modelNameMap.count(modelName) > 0) {
        qWarning() << "追加模型[" << modelName.c_str() << "]已存在，无需重复加载";
        return true;
    }
    if (!runtimeView) {
        qWarning() << "runtime view未初始化，无法加载追加模型[" << modelName.c_str() << "]";
        return false;
    }

    // 创建新的追加模型实例
    AppendedModelData newModel;
    auto& modelData = newModel;
    bool useAppendedPointFallback = false;

    // 打开并解析模型文件
    std::ifstream file(appendedModelPath);
    if (!file.is_open()) {
        useAppendedPointFallback = true;
    }

    if (useAppendedPointFallback) {
        if (runtimeView->fields.size() <= 2) {
            qWarning() << "无法打开追加模型文件，且字段视图不足：" << appendedModelPath.c_str();
            return false;
        }
        const auto& nodeLs = runtimeView->fields[2].nodes;
        if (nodeLs.empty()) {
            qWarning() << "无法打开追加模型文件，且节点数据为空：" << appendedModelPath.c_str();
            return false;
        }

        vtkSmartPointer<vtkCellArray> verts = vtkSmartPointer<vtkCellArray>::New();
        modelData.cachedFaceIds.clear();
        modelData.dynamicPoints->Reset();
        modelData.polys->Reset();

        for (size_t i = 0; i < nodeLs.size(); ++i) {
            const auto& node = nodeLs[i];
            vtkIdType id = modelData.dynamicPoints->InsertNextPoint(node.x, node.y, node.z);
            verts->InsertNextCell(1);
            verts->InsertCellPoint(id);
            modelData.cachedFaceIds.push_back(static_cast<int>(i));
        }

        modelData.surface->SetPoints(modelData.dynamicPoints);
        modelData.surface->SetVerts(verts);
        modelData.surface->SetPolys(modelData.polys);
        modelData.surface->Modified();
        modelData.isLoaded = (modelData.dynamicPoints->GetNumberOfPoints() > 0);

        modelData.mapper->SetLookupTable(colorTable);
        modelData.mapper->SetScalarRange(lastMin, lastMax);
        modelData.actor->GetProperty()->SetOpacity(modelData.opacity);
        modelData.actor->GetProperty()->SetRepresentationToPoints();
        modelData.actor->GetProperty()->SetPointSize(2.0);
        renderer->AddActor(modelData.actor);

        appendedModels.push_back(newModel);
        modelNameMap[modelName] = appendedModels.size() - 1;

        renderer->ResetCamera();
        renderWindow->Render();

        qInfo() << "追加模型面片文件不存在，已使用节点点云占位：" << appendedModelPath.c_str();
        return modelData.isLoaded;
    }

    if (!file.is_open()) {
        qWarning() << "无法打开追加模型文件：" << appendedModelPath.c_str();
        return false;
    }

    // 解析面片数据
    std::string line;
    modelData.cachedFaceIds.clear();
    modelData.dynamicPoints->Reset();
    modelData.polys->Reset();

    while (std::getline(file, line)) {
        std::istringstream iss(line);
        int faceIndex;
        vtkIdType p1, p2, p3;
        if (!(iss >> faceIndex >> p1 >> p2 >> p3)) continue;

        // 顶点索引转换
        size_t idx1 = static_cast<size_t>(p1 - 1);
        size_t idx2 = static_cast<size_t>(p2 - 1);
        size_t idx3 = static_cast<size_t>(p3 - 1);
        if (runtimeView->fields.size() <= 2) {
            qWarning() << "追加模型[" << modelName.c_str() << "]字段视图不足，跳过";
            continue;
        }
        const auto& nodeLs = runtimeView->fields[2].nodes;

        if (idx1 >= nodeLs.size() || idx2 >= nodeLs.size() || idx3 >= nodeLs.size()) {
            qWarning() << "追加模型[" << modelName.c_str() << "]面" << faceIndex << "顶点索引超出范围，跳过";
            continue;
        }

        // 插入顶点坐标
        auto& node1 = nodeLs[idx1];
        auto& node2 = nodeLs[idx2];
        auto& node3 = nodeLs[idx3];
        modelData.dynamicPoints->InsertNextPoint(node1.x, node1.y, node1.z);
        modelData.dynamicPoints->InsertNextPoint(node2.x, node2.y, node2.z);
        modelData.dynamicPoints->InsertNextPoint(node3.x, node3.y, node3.z);

        // 创建三角形面片
        vtkNew<vtkTriangle> triangle;
        triangle->GetPointIds()->SetId(0, modelData.dynamicPoints->GetNumberOfPoints() - 3);
        triangle->GetPointIds()->SetId(1, modelData.dynamicPoints->GetNumberOfPoints() - 2);
        triangle->GetPointIds()->SetId(2, modelData.dynamicPoints->GetNumberOfPoints() - 1);
        modelData.polys->InsertNextCell(triangle);

        // 缓存顶点索引
        modelData.cachedFaceIds.push_back(static_cast<int>(idx1));
        modelData.cachedFaceIds.push_back(static_cast<int>(idx2));
        modelData.cachedFaceIds.push_back(static_cast<int>(idx3));
    }
    file.close();

    // 绑定数据并标记加载状态
    modelData.surface->SetPoints(modelData.dynamicPoints);
    modelData.surface->SetPolys(modelData.polys);
    modelData.isLoaded = (modelData.dynamicPoints->GetNumberOfPoints() > 0);

    modelData.mapper->SetLookupTable(colorTable); // 复用主模型的颜色表
    modelData.mapper->SetScalarRange(lastMin, lastMax); // 同步颜色范围
    modelData.actor->GetProperty()->SetOpacity(modelData.opacity); // 应用透明度
    renderer->AddActor(modelData.actor);

    // 更新映射
    appendedModels.push_back(newModel);
    modelNameMap[modelName] = appendedModels.size() - 1;

    renderer->ResetCamera();
    renderWindow->Render();


    qDebug() << "追加模型[" << modelName.c_str() << "]加载完成并显示，面数：" << modelData.polys->GetNumberOfCells();
    return true;
}

// 加载模型数据
bool VTKSingleDialog::loadModelData() {

    if (!runtimeView) {
        qWarning() << windowTitle.c_str() << "：runtime view未初始化，无法加载模型";
        return false;
    }

    bool usePointFallback = false;
    std::ifstream file(modelPath);
    if (!file.is_open()) {
        usePointFallback = true;
    }

    if (usePointFallback) {
        if (flg < 0 || static_cast<size_t>(flg) >= runtimeView->fields.size()) {
            qWarning() << windowTitle.c_str() << "：模型面片文件不存在，且字段索引无效：" << QString::fromStdString(modelPath);
            return false;
        }

        const auto& nodeLs = runtimeView->fields[flg].nodes;
        if (nodeLs.empty()) {
            qWarning() << windowTitle.c_str() << "：模型面片文件不存在，且节点数据为空：" << QString::fromStdString(modelPath);
            return false;
        }

        vtkSmartPointer<vtkCellArray> verts = vtkSmartPointer<vtkCellArray>::New();
        cachedFaceIds.clear();
        dynamicPoints->Reset();
        polys->Reset();

        for (size_t i = 0; i < nodeLs.size(); ++i) {
            const auto& node = nodeLs[i];
            vtkIdType id = dynamicPoints->InsertNextPoint(node.x, node.y, node.z);
            verts->InsertNextCell(1);
            verts->InsertCellPoint(id);
            cachedFaceIds.push_back(static_cast<int>(i));
        }

        surface->SetPoints(dynamicPoints);
        surface->SetVerts(verts);
        surface->SetPolys(polys);
        surface->Modified();
        actor->GetProperty()->SetRepresentationToPoints();
        actor->GetProperty()->SetPointSize(2.0);

        renderer->ResetCamera();
        vtkCamera* camera = renderer->GetActiveCamera();
        camera->Zoom(1.5);
        renderWindow->Render();

        qInfo() << windowTitle.c_str() << "：模型面片文件不存在，已使用节点点云占位：" << QString::fromStdString(modelPath);
        return true;
    }

    if (!file.is_open()) {
        qWarning() << windowTitle.c_str() << "：无法打开模型文件：" << QString::fromStdString(modelPath);
        return false;
    }

    std::string line;
    cachedFaceIds.clear();
    dynamicPoints->Reset();
    polys->Reset();
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        int faceIndex;
        vtkIdType p1, p2, p3;
        if (!(iss >> faceIndex >> p1 >> p2 >> p3)) continue;

        // 转换
        size_t idx1 = static_cast<size_t>(p1 - 1);
        size_t idx2 = static_cast<size_t>(p2 - 1);
        size_t idx3 = static_cast<size_t>(p3 - 1);
        const auto& nodeLs = runtimeView->fields[flg].nodes;

        // 校验顶点索引有效性
        if (idx1 >= nodeLs.size() || idx2 >= nodeLs.size() || idx3 >= nodeLs.size()) {
            qWarning() << windowTitle.c_str() << "：面" << faceIndex << "顶点索引超出范围，跳过";
            continue;
        }

        auto& node1 = nodeLs[idx1];
        auto& node2 = nodeLs[idx2];
        auto& node3 = nodeLs[idx3];
        vtkIdType id1 = dynamicPoints->InsertNextPoint(node1.x, node1.y, node1.z);
        vtkIdType id2 = dynamicPoints->InsertNextPoint(node2.x, node2.y, node2.z);
        vtkIdType id3 = dynamicPoints->InsertNextPoint(node3.x, node3.y, node3.z);

        // 创建三角形面
        vtkNew<vtkTriangle> triangle;
        triangle->GetPointIds()->SetId(0, id1);
        triangle->GetPointIds()->SetId(1, id2);
        triangle->GetPointIds()->SetId(2, id3);
        polys->InsertNextCell(triangle);

        // 缓存索引
        cachedFaceIds.push_back(static_cast<int>(idx1));
        cachedFaceIds.push_back(static_cast<int>(idx2));
        cachedFaceIds.push_back(static_cast<int>(idx3));
    }
    file.close();

    // 调整相机
    surface->SetPoints(dynamicPoints);
    surface->SetPolys(polys);
    actor->GetProperty()->SetRepresentationToSurface();
    if (dynamicPoints->GetNumberOfPoints() > 0) {
        renderer->ResetCamera();
        vtkCamera* camera = renderer->GetActiveCamera();
        camera->Zoom(1.5);
    }

    qDebug() << windowTitle.c_str() << "：模型加载完成，面数：" << polys->GetNumberOfCells();
    return true;
}

void VTKSingleDialog::updateVTKData(const launchsupport::PredictionViewModel& DTO) {

    if (!isModelLoaded || cachedFaceIds.empty()) {
        qWarning() << windowTitle.c_str() << "：模型未加载，跳过数据更新";
        return;
    }

    if (!runtimeView || flg < 0 || static_cast<size_t>(flg) >= DTO.fields.size()) {
        qWarning() << windowTitle.c_str() << "：预测数据没有对应字段，跳过VTK更新";
        return;
    }

    const auto& fieldValues = DTO.fields[flg].values_by_node;

    size_t nodeCount = cachedFaceIds.size();
    for (size_t i = 0; i < nodeCount; ++i) {
        int dtoIdx = cachedFaceIds[i];
        if (dtoIdx < 0 || static_cast<size_t>(dtoIdx) >= fieldValues.size()) {
            qWarning() << windowTitle.c_str() << "：预测字段节点数量不足，跳过VTK更新";
            return;
        }
        const auto& node = runtimeView->fields[flg].nodes[dtoIdx];
        dynamicPoints->SetPoint(i, node.x, node.y, node.z);
    }
    dynamicPoints->Modified();

    //渲染
    std::vector<double> pointValues(nodeCount);
    for (size_t i = 0; i < nodeCount; ++i) {
        int dtoIdx = cachedFaceIds[i];
        const auto& values = fieldValues[dtoIdx];
        if (countFlg < 0 || static_cast<size_t>(countFlg) >= values.size()) {
            qWarning() << windowTitle.c_str() << "：预测字段分量数量不足，跳过VTK更新";
            return;
        }
        pointValues[i] = values[countFlg];
    }
    //DTO.res_coef[0]->task_ptr->taskpoint_ls[0].h
    renderVTK(pointValues);
}

void VTKSingleDialog::renderVTK(const std::vector<double>& pointValues) {
    if (QThread::currentThread() != this->thread()) {
        if (!this->isVisible()) return;
        if (this->parent() == nullptr) return;

        QPointer<VTKSingleDialog> safeThis = this;
        QMetaObject::invokeMethod(this, [safeThis, pointValues]() {
            if (!safeThis) return;
            safeThis->renderVTK(pointValues);
            }, Qt::QueuedConnection);
        return;
    }


    size_t vertexCount = dynamicPoints->GetNumberOfPoints();
    if (pointValues.size() != vertexCount || !surface || !mapper || !renderWindow) {
        qCritical() << windowTitle.c_str() << "：VTK渲染对象无效，中止渲染";
        return;
    }

    // 计算顶点值范围
    double minVal = VTK_DOUBLE_MAX, maxVal = VTK_DOUBLE_MIN;
    for (double val : pointValues) {
        if (std::isnan(val) || std::isinf(val)) val = 0.0;
        minVal = std::min(minVal, val);
        maxVal = std::max(maxVal, val);
    }
    if (minVal >= maxVal) {
        minVal = std::max(0.0, minVal - 0.1);
        maxVal = maxVal + 0.1;
    }

    // 更新顶点属性
    vertexValues->SetNumberOfTuples(vertexCount);
    for (vtkIdType i = 0; i < static_cast<vtkIdType>(vertexCount); ++i) {
        double val = pointValues[i];
        vertexValues->SetValue(i, std::isnan(val) ? (minVal + maxVal) / 2 : val);
    }
    vertexValues->Modified();
    surface->GetPointData()->SetScalars(vertexValues);
    surface->Modified();

    // 更新颜色表
    if (std::abs(minVal - lastMin) > 1e-6 || std::abs(maxVal - lastMax) > 1e-6) {
        colorTable->SetRange(minVal, maxVal);
        colorTable->SetTableRange(minVal, maxVal);
        colorTable->Build();
        colorTable->Modified();
        mapper->SetScalarRange(minVal, maxVal);
        scalarBar->SetLookupTable(colorTable);
        updateScalarBarLayout();
        scalarBar->Modified();
        lastMin = minVal;
        lastMax = maxVal;
    }
    mapper->Modified();

    // 渲染
    renderWindow->Render();
}

bool VTKSingleDialog::eventFilter(QObject* watched, QEvent* event){

    if (event->type() == QEvent::Wheel)
    {
        if (watched == checkBoxOverlay)
        {
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void VTKSingleDialog::setMarkerPoints(const std::vector<std::array<double, 3>>& points) {
    m_markerVisibility.clear();

    // 清除旧标签
    for (auto& actor : labelActors) {
        if (actor && renderer) {
            renderer->RemoveActor(actor);
        }
    }
    labelActors.clear();

    // 清除复选框容器
    if (checkBoxOverlay) {
        checkBoxOverlay->disconnect();
        checkBoxOverlay->close();
        checkBoxOverlay->deleteLater();
        checkBoxOverlay = nullptr;
    }

    // 清除旧布局
    QLayout* oldLayout = vtkWidget->layout();
    if (oldLayout) {
        QLayoutItem* item;
        while ((item = oldLayout->takeAt(0)) != nullptr) {
            if (item->widget()) item->widget()->deleteLater();
            delete item;
        }
        delete oldLayout;
    }

    // 重置标记点数据
    markerPoints->Reset();
    for (const auto& pt : points) {
        markerPoints->InsertNextPoint(pt[0], pt[1], pt[2]);
    }
    markerPolyData->SetPoints(markerPoints);
    markerPolyData->Modified();

    // 创建可见性控制数组
    vtkSmartPointer<vtkIntArray> visibilityArray = vtkSmartPointer<vtkIntArray>::New();
    visibilityArray->SetName("Visibility");
    visibilityArray->SetNumberOfComponents(1);
    visibilityArray->SetNumberOfTuples(points.size());

    // 初始化可见性列表
    m_markerVisibility.resize(points.size(), true);
    for (size_t i = 0; i < points.size(); ++i) {
        visibilityArray->SetTuple1(i, 1);
    }
    markerPolyData->GetPointData()->RemoveArray("Visibility");
    markerPolyData->GetPointData()->AddArray(visibilityArray);
    markerPolyData->GetPointData()->SetActiveScalars("Visibility");

    markerGlyph->SetInputData(markerPolyData);
    markerGlyph->SetInputArrayToProcess(
        0, 0, 0, vtkDataObject::FIELD_ASSOCIATION_POINTS, "Visibility"
    );

    // 添加点序号标注
    vtkSmartPointer<vtkTextProperty> textProp = vtkSmartPointer<vtkTextProperty>::New();
    textProp->SetFontSize(14);
    textProp->SetColor(1.0, 0.0, 0.0); // 红色标签
    textProp->BoldOn();
    textProp->SetBackgroundColor(1.0, 1.0, 1.0); // 白色背景
    textProp->SetBackgroundOpacity(0.6); // 半透明

    // 为每个点创建标签
    for (size_t i = 0; i < points.size(); ++i) {
        vtkSmartPointer<vtkBillboardTextActor3D> textActor =
            vtkSmartPointer<vtkBillboardTextActor3D>::New();
        textActor->SetInput(std::to_string(i + 1).c_str()); // 序号从1开始
        textActor->SetTextProperty(textProp);

        // 标签位置（在点坐标基础上偏移，避免重叠）
        double pt[3] = { points[i][0], points[i][1], points[i][2] };
        pt[0] += 0.1; // 偏移量可根据实际数据尺度调整
        pt[1] += 0.1;
        textActor->SetPosition(pt);
        textActor->SetVisibility(1); // 初始可见

        // 添加到渲染器并保存
        renderer->AddActor(textActor);
        labelActors.push_back(textActor);
    }

    // 添加滚动区域
    checkBoxOverlay = new QWidget(vtkWidget);
    checkBoxOverlay->setWindowFlags(Qt::FramelessWindowHint);
    checkBoxOverlay->setAttribute(Qt::WA_NoSystemBackground, false);
    checkBoxOverlay->setAttribute(Qt::WA_TranslucentBackground, false);
    checkBoxOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    checkBoxOverlay->setStyleSheet(R"(
        background-color: transparent; /* 仅 overlay 背景透明 */
        border-radius: 6px; 
        padding: 0px 10px 0px 0px; /* 右侧留滚动条空间 */
        border: 1px solid rgba(0, 0, 0, 0.1); /* 可选：淡边框 */
    )");
    checkBoxOverlay->setMaximumWidth(250);
    checkBoxOverlay->installEventFilter(this);
    // 创建滚动区域
    QScrollArea* scrollArea = new QScrollArea(checkBoxOverlay);
    scrollArea->setWidgetResizable(true);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setStyleSheet(
        "QScrollArea { border: none; }"
        "QScrollArea::viewport { background-color: transparent; }"
    );
    // 创建复选框容器 widget
    QWidget* checkBoxContainer = new QWidget(scrollArea);

    // 自动确定列数
    int COLS = 3;

    // 创建网格布局放复选框
    QVBoxLayout* mainBox = new QVBoxLayout(checkBoxContainer);
    mainBox->setContentsMargins(2, 2, 2, 2);
    mainBox->setSpacing(1);

    QGridLayout* checkBoxGrid = new QGridLayout();
    checkBoxGrid->setContentsMargins(0, 0, 0, 0);
    checkBoxGrid->setSpacing(3);

    // 创建复选框并关联信号槽
    for (size_t i = 0; i < points.size(); ++i) {
        QCheckBox* checkBox = new QCheckBox(QString::number(i + 1), checkBoxContainer);
        checkBox->setChecked(true);
        checkBox->setStyleSheet(R"(
            QCheckBox {
                background-color: transparent;
                color: #333333;          /* 设置文字颜色为深灰色 */
                border: none;           /* 去掉边框 */
                padding: 2px 0px;       /* 上下内边距，左右为0 */
            }
            QCheckBox::indicator {
                width: 13px;            /* 复选框指示器宽度 */
                height: 13px;           /* 复选框指示器高度 */
            }
        )");
        checkBox->setMinimumSize(0, 0);
        checkBox->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);

        int row = static_cast<int>(i) / COLS;
        int col = static_cast<int>(i) % COLS;
        checkBoxGrid->addWidget(checkBox, row, col);

        // 连接信号槽
        connect(checkBox, &QCheckBox::toggled, this,
            [this, i, visibilityArray](bool checked) {
                m_markerVisibility[i] = checked;

                // 更新点可见性
                visibilityArray->SetTuple1(i, checked ? 1 : 0);
                visibilityArray->Modified();
                markerPolyData->Modified();

                // 更新标签可见性
                if (i < labelActors.size()) {
                    labelActors[i]->SetVisibility(checked ? 1 : 0);
                }

                // 刷新渲染
                markerGlyph->Update();
                renderWindow->Render();

                emit allMarkersVisibilityUpdated(m_markerVisibility);

                if (checkBoxOverlay) checkBoxOverlay->raise();
            }
        );
    }

    mainBox->addLayout(checkBoxGrid);
    checkBoxContainer->setStyleSheet(R"(
    QCheckBox {
            background-color: white; /* 还原复选框背景为白色 */
            color: #333; /* 文字颜色 */
        }
    )");
    // 设置滚动区域的 widget
    scrollArea->setWidget(checkBoxContainer);

    // 设置 checkBoxOverlay 的布局
    QVBoxLayout* overlayLayout = new QVBoxLayout(checkBoxOverlay);
    overlayLayout->setContentsMargins(0, 0, 0, 0);
    overlayLayout->addWidget(scrollArea);

    // 设置vtkWidget布局
    QHBoxLayout* vtkLayout = new QHBoxLayout(vtkWidget);
    vtkLayout->setContentsMargins(0, 0, 0, 0);
    vtkLayout->addStretch();
    vtkLayout->addWidget(checkBoxOverlay);
    vtkLayout->setAlignment(checkBoxOverlay, Qt::AlignRight | Qt::AlignTop);
    checkBoxOverlay->setContentsMargins(0, 5, 5, 0);

    vtkWidget->setLayout(vtkLayout);

    // 计算最大高度
    int rowHeight = 20; // 每行大约高度
    int maxHeight = rowHeight * 5;
    checkBoxOverlay->setMaximumHeight(maxHeight);

    // 最终刷新
    checkBoxOverlay->adjustSize();
    markerGlyph->Update();
    renderWindow->Render();
    checkBoxOverlay->raise();

    emit allMarkersVisibilityUpdated(m_markerVisibility);
}
void VTKSingleDialog::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (vtkWidget) {
        vtkWidget->resize(event->size());
        updateScalarBarLayout();
        if (renderWindow) renderWindow->Render();
    }
}

void VTKSingleDialog::initAngleDisplay() {
    if (angleDisplayLabel) return;

    angleDisplayLabel = new QLabel(this);
    angleDisplayLabel->setStyleSheet(
        "background-color: rgba(0, 0, 0, 180);"
        "color: white;"
        "font: bold 12px;"
        "padding: 3px 6px;"
        "border-radius: 4px;"
    );
    angleDisplayLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    angleDisplayLabel->setText("X: 0.0°  Y: 0.0°  Z: 0.0°");

    if (mainLayout) {
        mainLayout->addWidget(angleDisplayLabel);
        mainLayout->setAlignment(angleDisplayLabel, Qt::AlignLeft | Qt::AlignTop);
        mainLayout->setSpacing(0);
    }
    else {
        qWarning() << "主布局未初始化，无法添加角度显示标签";
    }
}

void VTKSingleDialog::updateAngleDisplay() {
    if (!angleDisplayLabel) return;

    double xDeg = (currentXAngle);
    double yDeg = (currentYAngle);
    double zDeg = (currentZAngle);

    // 保留一位小数
    QString text = QString("X: %1°  Y: %2°  Z: %3°")
        .arg(xDeg, 0, 'f', 1)
        .arg(yDeg, 0, 'f', 1)
        .arg(zDeg, 0, 'f', 1);
    angleDisplayLabel->setText(text);
}

bool VTKSingleDialog::loadSTLModel(const std::string& stlFilePath) {
    if (stlFilePath.empty()) {
        qWarning() << "STL文件路径为空，加载失败";
        return false;
    }

    stlReader = vtkSmartPointer<vtkSTLReader>::New();
    stlReader->SetFileName(stlFilePath.c_str());
    stlReader->Update();

    vtkPolyData* poly = stlReader->GetOutput();
    if (!poly) {
        qWarning() << "无法读取STL文件：" << stlFilePath.c_str();
        return false;
    }

    vtkSmartPointer<vtkCenterOfMass> centerOfMass =
        vtkSmartPointer<vtkCenterOfMass>::New();
    centerOfMass->SetInputData(poly);
    centerOfMass->SetUseScalarsAsWeights(false);
    centerOfMass->Update();

    double com[3];
    centerOfMass->GetCenter(com);
    modelCenter[0] = com[0];
    modelCenter[1] = com[1];
    modelCenter[2] = com[2];

    vtkSmartPointer<vtkTransform> centerTransform =
        vtkSmartPointer<vtkTransform>::New();
    centerTransform->Translate(-com[0], -com[1], -com[2]);

    vtkSmartPointer<vtkTransformFilter> transformFilter =
        vtkSmartPointer<vtkTransformFilter>::New();
    transformFilter->SetTransform(centerTransform);
    transformFilter->SetInputConnection(stlReader->GetOutputPort());
    transformFilter->Update();

    stlMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    stlMapper->SetInputConnection(transformFilter->GetOutputPort());

    stlActor = vtkSmartPointer<vtkActor>::New();
    stlActor->SetMapper(stlMapper);
    stlActor->GetProperty()->SetColor(0.8, 0.5, 0.8);
    stlActor->GetProperty()->SetOpacity(1.0);

    // 清除残留变换
    stlActor->SetUserTransform(nullptr);
    stlActor->SetPosition(0, 0, 0);
    stlActor->SetScale(1, 1, 1);
    stlActor->SetOrientation(0, 0, 0);
    renderer->AddActor(stlActor);
    return true;
}

void VTKSingleDialog::setInitialViewAngle(double xAngle, double yAngle, double zAngle) {
    if (!stlActor || !renderer) {
        qWarning() << "模型或渲染器未初始化，无法设置相机视角";
        return;
    }

    vtkCamera* cam = renderer->GetActiveCamera();
    if (!cam) {
        qWarning() << "相机未初始化";
        return;
    }

    double bounds[6];
    stlActor->GetBounds(bounds);
    double center[3] = {
        (bounds[0] + bounds[1]) * 0.5,
        (bounds[2] + bounds[3]) * 0.5,
        (bounds[4] + bounds[5]) * 0.5
    };
    cam->SetFocalPoint(center);

    double maxDim = std::max({ bounds[1] - bounds[0], bounds[3] - bounds[2], bounds[5] - bounds[4] });
    double distance = 2.0 * maxDim;
    cam->SetPosition(center[0], center[1], center[2] + distance);
    cam->SetViewUp(0, 1, 0);

    cam->Elevation(xAngle);
    cam->Azimuth(yAngle);
    cam->Roll(zAngle);

    currentXAngle = 0.0;
    currentYAngle = 0.0;
    currentZAngle = 0.0;

    renderer->ResetCameraClippingRange();

    vtkSmartPointer<vtkAxesActor> axes = vtkSmartPointer<vtkAxesActor>::New();
    axes->SetTotalLength(5.0, 5.0, 5.0);      // 相对比例，不随世界坐标变
    axes->SetShaftTypeToCylinder();
    axes->SetCylinderRadius(0.2);             // 屏幕空间粗细
    axes->SetConeRadius(0.1);                  // 箭头大小

// 创建变换对象
    vtkSmartPointer<vtkTransform> worldTransform = vtkSmartPointer<vtkTransform>::New();
    // 初始旋转角度
    worldTransform->RotateX(0.0);
    worldTransform->RotateY(-45.0);
    worldTransform->RotateZ(0.0);
    axes->SetUserTransform(worldTransform);

    vtkRenderWindowInteractor* interactor = renderWindow->GetInteractor();
    if (!interactor) {
        renderWindow->SetInteractor(vtkSmartPointer<vtkRenderWindowInteractor>::New());
        interactor = renderWindow->GetInteractor();
        interactor->Initialize();
    }

    vtkSmartPointer<vtkOrientationMarkerWidget> orientationWidget =
        vtkSmartPointer<vtkOrientationMarkerWidget>::New();
    orientationWidget->SetOrientationMarker(axes);
    orientationWidget->SetInteractor(interactor);
    orientationWidget->SetViewport(0.65, 0.65, 0.98, 0.98);
    orientationWidget->SetOutlineColor(0.7, 0.7, 0.7);
    orientationWidget->SetEnabled(true);
    orientationWidget->InteractiveOff();
    this->orientationWidget = orientationWidget;

    renderWindow->Render();
    updateAngleDisplay();
}

void VTKSingleDialog::setModelRotation(double xAngle,
    double yAngle,
    double zAngle)
{
    if (!stlActor || !renderer) {
        qWarning() << "模型或渲染器未初始化";
        return;
    }

    currentXAngle = xAngle;
    currentYAngle = yAngle;
    currentZAngle = xAngle;

    updateAngleDisplay();
    startQ = currentQ;

    // 欧拉角to四元数
    targetQ = QQuaternion::fromEulerAngles(
        xAngle, yAngle, zAngle
    );

    animProgress = 0.0;

    if (!isAnimating) {
        isAnimating = true;
        rotationAnimationTimer->start();
    }
}

void VTKSingleDialog::setRotationAnimationTimer() {

    rotationAnimationTimer = new QTimer(this);
    rotationAnimationTimer->setInterval(50);
    connect(rotationAnimationTimer, &QTimer::timeout, this, &VTKSingleDialog::updateRotationFrame);
}
// 更新动画帧
void VTKSingleDialog::updateRotationFrame()
{
    if (!isAnimating) {
        rotationAnimationTimer->stop();
        return;
    }

    animProgress += animSpeed;

    if (animProgress >= 1.0) {
        animProgress = 1.0;
        isAnimating = false;
        rotationAnimationTimer->stop();
    }

    currentQ = QQuaternion::slerp(startQ, targetQ, animProgress);

    applyRotation(currentQ); 
}

void VTKSingleDialog::applyRotation(const QQuaternion& q)
{
    QQuaternion nq = q.normalized();

    double w = nq.scalar();//弧度
    double angleRad = 2.0 * std::acos(w);

    double s = std::sqrt(1.0 - w * w);

    double ax, ay, az;
    if (s < 1e-6) {//轴
        ax = 1.0; ay = 0.0; az = 0.0;
    }
    else {
        ax = nq.x() / s;
        ay = nq.y() / s;
        az = nq.z() / s;
    }
    //角度
    double angleDeg = vtkMath::DegreesFromRadians(angleRad);
    //模型中心
    const double cx = modelCenter[0];
    const double cy = modelCenter[1];
    const double cz = modelCenter[2];

    vtkSmartPointer<vtkTransform> transform =
        vtkSmartPointer<vtkTransform>::New();

    transform->PostMultiply();
    transform->Identity();
    transform->Translate(-cx, -cy, -cz);
    transform->RotateWXYZ(angleDeg, ax, ay, az);
    transform->Translate(cx, cy, cz);

    stlActor->SetUserTransform(transform);

    if (renderer) {
        vtkCamera* cam = renderer->GetActiveCamera();
        if (cam) {
            cam->SetFocalPoint(cx, cy, cz);
            renderer->ResetCameraClippingRange();
        }
    }

    if (renderWindow) {
        renderWindow->Render();
    }
}
