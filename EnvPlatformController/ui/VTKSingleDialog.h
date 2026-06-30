#ifndef VTKSINGLEDIALOG_H
#define VTKSINGLEDIALOG_H

#include <QWidget>
#include <QVBoxLayout>
#include <QResizeEvent>
#include <QTimer>
#include <QVTKOpenGLNativeWidget.h>

#include "EnvNodeSupport/ControllerViewModels.h"


#include <vtkStringArray.h>
#include <vtkLabelPlacementMapper.h>
#include <vtkActor2D.h>
#include <vtkVectorText.h>
#include <vtkBillboardTextActor3D.h>

#include <vtkTextProperty.h>
#include <vtkProperty.h>
#include <vtkActor.h>
#include <vtkCamera.h>
#include <vtkRenderer.h>
#include <vtkGenericOpenGLRenderWindow.h>

#include <vtkPointData.h>
#include <vtkPolyData.h>
#include <vtkPoints.h>
#include <vtkCellArray.h>
#include <vtkDoubleArray.h>
#include <vtkPolyDataMapper.h>

#include <vtkLookupTable.h>
#include <vtkScalarBarActor.h>

#include <vtkTriangle.h>
#include <vtkVertexGlyphFilter.h>
#include <vtkSphereSource.h>
#include <vtkGlyph3D.h>
#include <vtkScalarBarActor.h> 
#include <vtkLookupTable.h> 
#include <vtkPointPicker.h>
#include <vtkIdTypeArray.h>
#include <vtkUnsignedCharArray.h>

#include <vtkSTLReader.h>
#include <vtkTransform.h>
#include <vtkTransformPolyDataFilter.h>
#include <vtkTransformFilter.h>
#include <vtkAxesActor.h>
#include <vtkCubeAxesActor.h>
#include <vtkCenterOfMass.h>
#include <vtkOrientationMarkerWidget.h>
#include <vtkCaptionActor2D.h>
#include <vtkTextProperty.h>
#include <vtkTextActor.h>

#include <QMutex>
#include <QCheckBox>
#include <vector> 
#include <QScrollArea>
#include <vector>
#include <chrono>
#include <string>
#include <optional>
#include <unordered_map>

#include <QMouseEvent>

#include <QDialog>
#include <QQuaternion>

#include <vtkSmartPointer.h>
#include <vtkRenderWindow.h>

#include <QLabel>
#ifndef GLOG_NO_ABBREVIATED_SEVERITIES
#define GLOG_NO_ABBREVIATED_SEVERITIES
#endif
#define NOMINMAX
#include <Windows.h>
#undef ERROR
#undef WARNING

struct AppendedModelData {
    bool isLoaded = false;                          // 模型加载状态
    std::vector<int> cachedFaceIds;                 // 顶点索引缓存
    double opacity = 0.6;                           // 固定透明度

    // 数据容器
    vtkSmartPointer<vtkPolyData> surface = vtkSmartPointer<vtkPolyData>::New();
    vtkSmartPointer<vtkPoints> dynamicPoints = vtkSmartPointer<vtkPoints>::New();
    vtkSmartPointer<vtkCellArray> polys = vtkSmartPointer<vtkCellArray>::New();
    vtkSmartPointer<vtkDoubleArray> vertexValues = vtkSmartPointer<vtkDoubleArray>::New();

    // 渲染组件
    vtkSmartPointer<vtkPolyDataMapper> mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    vtkSmartPointer<vtkActor> actor = vtkSmartPointer<vtkActor>::New();

    AppendedModelData() {
        // 绑定数据与映射器
        mapper->SetInputData(surface);
        mapper->SetScalarModeToUsePointData();
        mapper->InterpolateScalarsBeforeMappingOn();
        // 绑定映射器与Actor
        actor->SetMapper(mapper);
        // 追加模型默认外观
        actor->GetProperty()->SetInterpolationToPhong();
        actor->GetProperty()->SetColor(0.0, 1.0, 0.0);
    }
};

class ScalarBarResizeCommand : public vtkCommand
{
public:
    // VTK 中创建对象的标准方法
    static ScalarBarResizeCommand* New()
    {
        return new ScalarBarResizeCommand;
    }

    // 当事件被触发时执行的代码
    virtual void Execute(vtkObject* caller, unsigned long eventId, void* callData)
    {
        (void)eventId;
        (void)callData;

        // 将调用者转换为 vtkRenderWindow
        vtkRenderWindow* renWin = vtkRenderWindow::SafeDownCast(caller);
        if (!renWin || !this->ScalarBarActor)
        {
            return;
        }

        // 获取当前窗口的像素大小
        int* size = renWin->GetSize();
        if (size[0] <= 0 || size[1] <= 0)
        {
            return;
        }

        int scalarBarWidth = 60;   // 颜色条宽度
        int scalarBarHeight = 100; // 颜色条高度
        int margin = 10;           // 颜色条与窗口边缘的距离

        // 计算颜色条在标准化视口坐标中的位置和大小
        // 我们希望它位于右下角
        double xPos = static_cast<double>(size[0] - scalarBarWidth - margin) / size[0];
        double yPos = static_cast<double>(margin) / size[1];
        double width = static_cast<double>(scalarBarWidth) / size[0];
        double height = static_cast<double>(scalarBarHeight) / size[1];

        // 应用新的位置和大小到颜色条
        this->ScalarBarActor->SetPosition(xPos, yPos);
        this->ScalarBarActor->SetWidth(width);
        this->ScalarBarActor->SetHeight(height);
    }

    // 用于存储指向颜色条actor的指针
    vtkScalarBarActor* ScalarBarActor;

protected:
    ScalarBarResizeCommand() : ScalarBarActor(nullptr) {}
    ~ScalarBarResizeCommand() override {}

private:
    // 防止默认的拷贝构造和赋值操作
    ScalarBarResizeCommand(const ScalarBarResizeCommand&) = delete;
    void operator=(const ScalarBarResizeCommand&) = delete;
};

class VTKSingleDialog : public QWidget {
    Q_OBJECT

public:
    // 构造函数：窗口标题 + 模型路径
    explicit VTKSingleDialog(
        const std::string& windowTitle,  // 窗口标题
        const std::string& modelPath,    // 模型文件路径
        QWidget* parent = nullptr
    );
    ~VTKSingleDialog() override
    {
        if (renderWindow && resizeCommand)
            renderWindow->RemoveObserver(resizeCommand);
    }
    // 业务数据与配置
    std::shared_ptr<const launchsupport::RuntimeViewModel> runtimeView;
    int flg = 0;                        // 业务字段索引
    int countFlg = 0;                   // 属性值向量索引
    bool isModelLoaded = false;         // 主模型是否加载完成
    std::vector<std::array<double, 3>> markerOriginalPoints;
    vtkSmartPointer<vtkRenderer> renderer = nullptr;
    // 主模型核心接口
    void initVTKComponents();           // 初始化VTK组件
    bool loadModelData();               // 加载主模型数据
    void setMarkerPoints(const std::vector<std::array<double, 3>>& points);  // 设置标记点
    bool appendModel(const std::string& modelName, const std::string& appendedModelPath);  // 加载追加模型
    bool RemoveActor();//传感器不需要色块范围
    void setRuntimeView(std::shared_ptr<const launchsupport::RuntimeViewModel> view);
private:
    // VTK点拾取器
    vtkSmartPointer<vtkPointPicker> markerPicker;
    // 点颜色数组
    vtkSmartPointer<vtkUnsignedCharArray> markerColors;
    vtkIdType hoveredMarkerId = -1;
    vtkIdType selectedMarkerId = -1;

public slots:
    void updateVTKData(const launchsupport::PredictionViewModel& DTO);  // 更新主模型数据

protected:
    void resizeEvent(QResizeEvent* event) override;  // 窗口大小调整事件
    void paintEvent(QPaintEvent* event) override;
public:
    // 窗口与路径配置
    std::string windowTitle;            // 窗口标题
    std::string modelPath;              // 主模型文件路径

private:
    // UI基础组件
    QVBoxLayout* mainLayout = nullptr;
    QVTKOpenGLNativeWidget* vtkWidget = nullptr;
    vtkSmartPointer<ScalarBarResizeCommand> resizeCommand;

    double currentXAngle = 0.0;
    double currentYAngle = 0.0;
    double currentZAngle = 0.0;

     double modelCenter[3]; // 缓存模型中心

    QLabel* angleDisplayLabel = nullptr;
    vtkSmartPointer<vtkSTLReader> stlReader;
    vtkSmartPointer<vtkPolyDataMapper> stlMapper;
    vtkSmartPointer<vtkActor> stlActor;
    vtkSmartPointer<vtkCamera> camera;

    // 主模型VTK核心组件
    vtkSmartPointer<vtkPolyData> surface = nullptr;               // 主模型数据容器
    vtkSmartPointer<vtkPoints> dynamicPoints = nullptr;           // 主模型顶点坐标
    vtkSmartPointer<vtkCellArray> polys = nullptr;                 // 主模型面片数据
    vtkSmartPointer<vtkDoubleArray> vertexValues = nullptr;        // 主模型顶点属性值
    vtkSmartPointer<vtkPolyDataMapper> mapper = nullptr;           // 主模型映射器
    vtkSmartPointer<vtkActor> actor = nullptr;                     // 主模型渲染实体
    vtkSmartPointer<vtkOrientationMarkerWidget> orientationWidget;



    // 复选框相关
    std::vector<QCheckBox*> markerCheckBoxes;  // 存储所有点的复选框
    QWidget* checkBoxOverlay = NULL;                // 复选框容器
    QGridLayout* checkBoxGrid;               // 复选框布局
    std::vector<bool> pointVisibility;         // 记录每个点的可见性状态
    std::vector<vtkSmartPointer<vtkBillboardTextActor3D>> labelActors;
    // 颜色与标记点组件
    vtkSmartPointer<vtkLookupTable> colorTable = nullptr;          // 颜色表
    vtkSmartPointer<vtkScalarBarActor> scalarBar = nullptr;        // 颜色条
    vtkSmartPointer<vtkPoints> markerPoints = nullptr;             // 标记点坐标
    vtkSmartPointer<vtkPolyData> markerPolyData = nullptr;         // 标记点数据容器
    vtkSmartPointer<vtkSphereSource> markerSphere = nullptr;       // 标记点形状
    vtkSmartPointer<vtkGlyph3D> markerGlyph = nullptr;             // 标记点渲染过滤器
    vtkSmartPointer<vtkPolyDataMapper> markerMapper = nullptr;     // 标记点映射器
    vtkSmartPointer<vtkActor> markerActor = nullptr;               // 标记点渲染实体
    bool isCameraInited = false;

    // 主模型缓存与辅助参数
    std::vector<int> cachedFaceIds;                                // 主模型顶点索引缓存
    std::chrono::high_resolution_clock::time_point lastRenderTime; // 帧率控制时间戳
    double lastMin = 0.0, lastMax = 1.0;                           // 主模型颜色范围缓存

    // 新增：多追加模型核心容器
    std::vector<AppendedModelData> appendedModels;                 // 存储所有追加模型
    std::unordered_map<std::string, int> modelNameMap;

    // 主模型私有辅助函数
    void renderVTK(const std::vector<double>& pointValues);        // 渲染主模型
    void updateScalarBarLayout();                                  // 统一云图标尺尺寸与字体

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
signals:
    void allMarkersVisibilityUpdated(const QVector<bool>& visibilityList);

public:
    QVector<bool> m_markerVisibility;
    vtkSmartPointer<vtkGenericOpenGLRenderWindow> renderWindow = nullptr;  // 渲染窗口
    void initAngleDisplay();
    bool loadSTLModel(const std::string& stlFilePath);                  // 加载STL模型
    void setInitialViewAngle(double xAngle, double yAngle, double zAngle);  // 初始视角
    void setModelRotation(double xAngle, double yAngle, double zAngle);    // 调整角度
    void updateAngleDisplay();                                          //更新角度显示


    void setRotationAnimationTimer();
    QTimer* rotationAnimationTimer = nullptr; // 动画定时器
    QQuaternion currentQ = QQuaternion(1, 0, 0, 0);   // 当前姿态
    QQuaternion startQ;     // 动画起点
    QQuaternion targetQ;    // 动画目标

    bool   isAnimating = false;
    double animProgress = 0.0;
    double animSpeed = 0.1;
    void updateRotationFrame();
    void applyRotation(const QQuaternion& q);
};

#endif // VTKSINGLEDIALOG_H
