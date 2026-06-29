#pragma once
#define NOMINMAX
#include <Windows.h>
#undef byte 

#include <windowsx.h>

#include <QtWidgets/QMainWindow>
#include "ui_EnvPredictorUI.h"
#include <QMainWindow>
#include <QPointer>
#include <QPlainTextEdit>
#include <qdialog.h>
#include <QVector3D>
#include <QTimer>
#include <QElapsedTimer>

#include <thread>
#include <glog/logging.h>
#include <iostream>
#include "direct.h"
#include <QValueAxis>
#include <QLineSeries>
#include <vector>
#include <map>
#include <set>
#include <QVTKOpenGLNativeWidget.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkActor.h>
#include <vtkCamera.h>
#include <vtkNamedColors.h>
#include <vtkTriangleFilter.h>
#include <vtkNew.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderWindow.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkRenderer.h>
#include <vtkSTLReader.h>
#include <vtkLegendBoxActor.h>
#include <vtkTextProperty.h>
#include <vtkScalarBarActor.h> 
#include <vtkSmartPointer.h>
#include <vtkPolyData.h>      
#include <vtkDoubleArray.h>   
#include <vtkLookupTable.h>   
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkScalarBarActor.h>  
#include <vtkPointData.h>       
#include <vtkScalarsToColors.h> 
#include <vtkTriangle.h> 
#include <vtkCellData.h> 
#include <array>

#include <QMouseEvent>


#include "VTKSingleDialog.h"
#include "ChartSingleDialog.h"
#include "EnvNodeSupport/IControllerBackend.h"
#include "EnvNodeSupport/PlatformControllerBackend.h"
class QTableWidget; class QTreeView; class QStandardItemModel;
class QLineEdit; class QComboBox; class QPlainTextEdit; class QSpinBox; class QDoubleSpinBox;
class QLabel; class QCheckBox; class QPushButton; class QWidget; class QDialog;
class QGridLayout; class QGroupBox; class QProcess; class QScrollArea; class QVBoxLayout;
class QTabWidget; class QSlider;

namespace flightenv::ui::display {
class ScalarTrendWidget;
class TrajectoryPathWidget;
class WorkflowPathWidget;
}

namespace flightenv::ui::demo {
class VtkModelFieldWidget;
}

#define ONLINE

class SensorExtraConfigDialog : public QDialog {
    Q_OBJECT
public:
    explicit SensorExtraConfigDialog(QWidget* parent = nullptr);
};
class EnvPredictorUI : public QMainWindow
{
    Q_OBJECT

public:

    explicit EnvPredictorUI(QWidget* parent = nullptr);
    ~EnvPredictorUI();
    void prepareForShutdown();

protected://窗口缩放  拖动  ...
    void closeEvent(QCloseEvent* event) override;

    static constexpr int RESIZE_BORDER = 8;
    static constexpr int TITLE_HEIGHT = 30;

#ifdef Q_OS_WIN
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
    {
        if (eventType != "windows_generic_MSG")
            return false;

        MSG* msg = static_cast<MSG*>(message);

        switch (msg->message)
        {
        case WM_GETMINMAXINFO:
        {
            MINMAXINFO* mmi = reinterpret_cast<MINMAXINFO*>(msg->lParam);

            HMONITOR monitor = MonitorFromWindow(msg->hwnd, MONITOR_DEFAULTTONEAREST);
            if (monitor) {
                MONITORINFO mi;
                mi.cbSize = sizeof(mi);
                GetMonitorInfo(monitor, &mi);

                const RECT& work = mi.rcWork;
                const RECT& monitorRect = mi.rcMonitor;

                mmi->ptMaxPosition.x = work.left - monitorRect.left;
                mmi->ptMaxPosition.y = work.top - monitorRect.top;
                mmi->ptMaxSize.x = work.right - work.left;
                mmi->ptMaxSize.y = work.bottom - work.top;
            }

            *result = 0;
            return true;
        }

        case WM_NCHITTEST:
        {
            const LONG ret = DefWindowProc(msg->hwnd,
                msg->message,
                msg->wParam,
                msg->lParam);
            if (ret != HTCLIENT) {
                *result = ret;
                return true;
            }

            RECT winRect;
            GetWindowRect(msg->hwnd, &winRect);

            const int x = static_cast<short>(LOWORD(msg->lParam));
            const int y = static_cast<short>(HIWORD(msg->lParam));

            const bool left = x < winRect.left + RESIZE_BORDER;
            const bool right = x >= winRect.right - RESIZE_BORDER;
            const bool top = y < winRect.top + RESIZE_BORDER;
            const bool bottom = y >= winRect.bottom - RESIZE_BORDER;

            if (top && left) { *result = HTTOPLEFT;     return true; }
            if (top && right) { *result = HTTOPRIGHT;    return true; }
            if (bottom && left) { *result = HTBOTTOMLEFT;  return true; }
            if (bottom && right) { *result = HTBOTTOMRIGHT; return true; }

            if (left) { *result = HTLEFT;   return true; }
            if (right) { *result = HTRIGHT;  return true; }
            if (top) { *result = HTTOP;    return true; }
            if (bottom) { *result = HTBOTTOM; return true; }

            QPoint localPos = mapFromGlobal(QPoint(x, y));
            if (localPos.y() <= TITLE_HEIGHT) {
                *result = HTCAPTION;
                return true;
            }

            *result = HTCLIENT;
            return true;
        }
        }
        return false;
    }
#endif
private: //初始化相关
    void initWindow();
    void initLogging();
    void initTabs();
    void initTree();
    void initIcons();
    bool dragging_ = false;//鼠标拖动窗口标志位
    QPoint dragPosition_;//当前鼠标位置
private:
    Ui::EnvPredictorUIClass ui;
    QStringList subject_list;

private slots:
    void onItemClicked(QTreeWidgetItem* item, int column);
    QString toQStr(const std::string str) { return QString::fromUtf8(str.c_str()); }


private:
    void buildAcqAndConfigFlat_();   // 构建平铺布局
    void buildRuntimeChainPage_();
    void buildTrainingCliControls_(QVBoxLayout* layout, QWidget* parent);
    void hideLegacyInlineTrainingButtons_();
    void refreshRuntimeChainPage_();
    void applyPlatformRunConfigFromUi_();
    QString workspaceRootPath_() const;
    QString defaultTrainingProjectConfigPath_() const;
    QString defaultTrainingOutputRootPath_() const;
    QString currentTrainingProjectConfigPath_() const;
    QString currentTrainingOutputRootPath_() const;
    void setTrainingStatus_(const QString& text);
    void appendTrainingLog_(const QString& text);
    void initializeObjectModel_();
    bool loadTrainingProjectConfig_(const QString& path);
    bool saveTrainingProjectConfig_(const QString& path);
    void startTrainingCli_();
    void finishTrainingCli_(int exitCode, bool normalExit);
    void rebuildObjectSensorView_();
    void buildGraphRuntimePage_();
    void startGraphRuntimeRunner_();
    void stopGraphRuntimeRunner_();
    void refreshGraphRuntimePage_();
    void refreshGraphWorkflowEvidence_();
    void buildGraphWorkflowTabs_(QVBoxLayout* root);
    void clearGraphMultiFieldWidgets_();
    void layoutGraphMultiFieldWidgets_();
    void renderGraphPlatformFieldArtifacts_();
    void appendGraphRuntimeLog_(const QString& text);
    void setGraphRuntimeStatus_(const QString& text);
    void fillMockSensors_();

private:

    QWidget* pageAcqConfig_ = nullptr; // 挂容器
    QWidget* pageRuntimeChain_ = nullptr;
    QLabel* lbObjectSummary_ = nullptr;
    QLabel* lbObjectFlow_ = nullptr;
    QWidget* objectSensorVtkHost_ = nullptr;
    VTKSingleDialog* objectSensorVtk_ = nullptr;
    QComboBox* objectMeshCombo_ = nullptr;
    QComboBox* objectSensorLayoutCombo_ = nullptr;
    QTableWidget* tblObjectBasic_ = nullptr;
    QTableWidget* tblObjectMeshes_ = nullptr;
    QTableWidget* tblObjectSensors_ = nullptr;
    QTableWidget* tblObjectRuntimeConfig_ = nullptr;
    QSpinBox* objectOnlineFramesSpin_ = nullptr;
    QSpinBox* objectPredictionEveryFramesSpin_ = nullptr;
    QSpinBox* objectFutureMaxIterationsSpin_ = nullptr;
    QSpinBox* objectBranchChunkIterationsSpin_ = nullptr;
    QSpinBox* objectBranchCountSpin_ = nullptr;
    QSpinBox* objectMaxConcurrentBranchesSpin_ = nullptr;
    QSpinBox* objectRefreshIntervalMsSpin_ = nullptr;
    QDoubleSpinBox* objectReplayTimeScaleSpin_ = nullptr;
    QLineEdit* objectSensorStreamEdit_ = nullptr;
    QLineEdit* objectTrajectoryInputEdit_ = nullptr;
    QLabel* objectRunConfigHint_ = nullptr;
    QPushButton* objectTrainButton_ = nullptr;
    QLineEdit* trainingProjectConfigEdit_ = nullptr;
    QLineEdit* trainingOutputRootEdit_ = nullptr;
    QLineEdit* trainingDbPathEdit_ = nullptr;
    QLineEdit* trainingTaskIdsEdit_ = nullptr;
    QLineEdit* trainingFieldIdsEdit_ = nullptr;
    QLineEdit* trainingSensorIdsEdit_ = nullptr;
    QLineEdit* trainingBaseIdsEdit_ = nullptr;
    QCheckBox* trainingUsePodDatabaseCheck_ = nullptr;
    QLabel* trainingStatusLabel_ = nullptr;
    QPlainTextEdit* trainingLogEdit_ = nullptr;
    QPushButton* trainingStartButton_ = nullptr;
    QPushButton* trainingLoadConfigButton_ = nullptr;
    QPushButton* trainingSaveConfigButton_ = nullptr;
    QPushButton* trainingSaveAsConfigButton_ = nullptr;
    QPushButton* trainingDbBrowseButton_ = nullptr;
    QPushButton* trainingOutputBrowseButton_ = nullptr;
    QPushButton* trainingClearLogButton_ = nullptr;
    QProcess* trainingProcess_ = nullptr;
    QString trainingLastResultPath_;
    QTableWidget* tblRuntimeChain_ = nullptr;
    QLabel* lbHealthLedger_ = nullptr;
    bool platformRunConfigApplyOk_ = true;
    QWidget* pageGraphRuntime_ = nullptr;
    QLineEdit* graphCatalogPathEdit_ = nullptr;
    QLineEdit* graphOutputDirEdit_ = nullptr;
    QSpinBox* graphMaxSamplesSpin_ = nullptr;
    QSpinBox* graphReplayFramesSpin_ = nullptr;
    QSpinBox* graphPredictionIntervalSpin_ = nullptr;
    QSpinBox* graphFrameDelayMsSpin_ = nullptr;
    QCheckBox* graphPredictToLandingCheck_ = nullptr;
    QCheckBox* graphPredictionOffCheck_ = nullptr;
    QLabel* lbGraphRuntimeStatus_ = nullptr;
    QPlainTextEdit* graphRuntimeLog_ = nullptr;
    QScrollArea* graphOperatorScroll_ = nullptr;
    QWidget* graphOperatorContainer_ = nullptr;
    QGridLayout* graphOperatorGrid_ = nullptr;
    QPushButton* graphStartButton_ = nullptr;
    QPushButton* graphStopButton_ = nullptr;
    QProcess* graphRunnerProcess_ = nullptr;
    QTimer* graphRuntimePollTimer_ = nullptr;
    QTabWidget* graphWorkflowTabs_ = nullptr;
    QLabel* lbGraphObjectSummary_ = nullptr;
    QLabel* lbGraphWorkflowSummary_ = nullptr;
    QLabel* lbGraphTrajectorySummary_ = nullptr;
    QLabel* lbGraphFieldSummary_ = nullptr;
    QLabel* lbGraphDamageSummary_ = nullptr;
    QLabel* lbGraphLifeSummary_ = nullptr;
    QLabel* lbGraphThroughputSummary_ = nullptr;
    flightenv::ui::display::WorkflowPathWidget* graphWorkflowPathWidget_ = nullptr;
    flightenv::ui::display::TrajectoryPathWidget* graphTrajectoryPlot_ = nullptr;
    flightenv::ui::display::ScalarTrendWidget* graphDamageTrend_ = nullptr;
    flightenv::ui::display::ScalarTrendWidget* graphLifeTrend_ = nullptr;
    flightenv::ui::demo::VtkModelFieldWidget* graphFieldVtk_ = nullptr;
    flightenv::ui::demo::VtkModelFieldWidget* graphDamageVtk_ = nullptr;
    flightenv::ui::demo::VtkModelFieldWidget* graphLifeVtk_ = nullptr;
    QScrollArea* graphMultiFieldScroll_ = nullptr;
    QWidget* graphMultiFieldContainer_ = nullptr;
    QGridLayout* graphMultiFieldGrid_ = nullptr;
    std::map<QString, flightenv::ui::demo::VtkModelFieldWidget*> graphMultiFieldWidgets_;
    std::vector<QString> graphMultiFieldOrder_;
    QComboBox* graphFieldSubjectCombo_ = nullptr;
    QComboBox* graphDamageSubjectCombo_ = nullptr;
    QComboBox* graphLifeSubjectCombo_ = nullptr;
    QTableWidget* graphObjectTable_ = nullptr;
    QTableWidget* graphBindingTable_ = nullptr;
    QTableWidget* graphEquationTable_ = nullptr;
    QTableWidget* graphCatalogModelTable_ = nullptr;
    QTableWidget* graphPathTable_ = nullptr;
    QTableWidget* graphThroughputTable_ = nullptr;
    QTableWidget* graphAssetTable_ = nullptr;
    QTableWidget* graphFusionTable_ = nullptr;
    QTableWidget* graphTrajectoryTable_ = nullptr;
    QTableWidget* graphFieldTable_ = nullptr;
    QTableWidget* graphDamageTable_ = nullptr;
    QTableWidget* graphLifeTable_ = nullptr;
    QPlainTextEdit* graphEvidenceText_ = nullptr;
    std::map<QString, QGroupBox*> graphOperatorCards_;

    // 顶部：传感器配置 & 通道映射
    QTableWidget* tblSensorMap_ = nullptr;    // 通道表：名称/字段/单位/采样频率/缩放/偏置

    // 中左：系统对齐配置
    QSpinBox* spBucketMs_ = nullptr;          // 时间桶宽度
    QSpinBox* spLingerMs_ = nullptr;          // 迟到等待上限
    QComboBox* cbPolicy_ = nullptr;           // 策略：等全/等够/到时发
    QSpinBox* spAtLeastM_ = nullptr;          // AtLeastM 的 M

    // 中右：传感器面板（含虚拟/数据库）
    QTreeView* treeSensors_ = nullptr;        // 左树：硬件/虚拟/数据库
    QStandardItemModel* modelSensors_ = nullptr;
    QLabel* lbSensorInfo_ = nullptr;          // 右侧顶部：基本状态
    QTableWidget* tblSensorParams_ = nullptr; // 右侧：可选配置键值表

    // 底部：日志
    QPlainTextEdit* logView_ = nullptr;

//点云图相关
private slots:
    void onTimerTimeout();           // 颜色映射切换
    void updateFrameRateDisplay();   // 帧率更新
    void on_startBtn_clicked();    //开始
    void on_pauseBtn_clicked();    //暂停
    void on_resetBtn_clicked();    //复位
private:
    void initSensorMarkerWidget();//初始化传感器标记窗口
    void initFieldWidget();//初始化物理场
    void initFIeldControlPanel();//初始化物理场控制
    void initPlatformFieldWidget();
    void initPlatformFieldControlPanel();
    void bindPlatformSnapshotCallback();
    void handlePlatformSnapshot(const launchsupport::PlatformRunSnapshotView& snapshot);
    void refreshPlatformFieldNavigation();
    void selectPlatformLatestFrame();
    void renderPlatformCurrentFrame();
    void updatePlatformFrameProgressLabel();
    void schedulePlatformCurrentFrameRender();
    void updatePlatformCoreParameterTable();
    void layoutPlatformFieldWidgets();
    void rebuildPlatformFieldVisibilityControls();
    void requestPlatformFieldArtifactValuesAsync(
        const std::filesystem::path& artifactPath,
        const QString& cachePath,
        qint64 mtimeMs,
        qint64 sizeBytes);
    void loadPlatformSensorResources();
    void loadPlatformSensorLayouts();
    void loadPlatformSensorFrames();
    void updatePlatformSensorDisplayForLoop(int loop);
    std::vector<double> cachedPlatformFieldArtifactValues(
        const launchsupport::PlatformFieldArtifactView& field,
        QString* error,
        bool* loading = nullptr);
    std::vector<launchsupport::PlatformFieldArtifactView> platformFieldsForCurrentFrame() const;
    std::vector<int> platformLoopsForBranch(const QString& branchId) const;
    QString platformBestBranchId() const;
    int platformLatestLoopForBranch(const QString& branchId) const;
    void initParameterTable();//初始化ROS信息参数表格
    void initScatterChart();//初始化图表
    void initFlightAttitudeWidget();//初始化飞行状态窗口
    void updateColorMapping();       // 更新点云颜色映射
    void renderByPointValues(const std::vector<double>& pointValues);
    int interactionData();
    bool initializeRuntime();//初始化Runtime
    void bindControllerCallbacks();
    void handlePrediction(const launchsupport::PredictionViewModel& d);
    void handleSensorView(const launchsupport::SensorViewModel& d);
    void bindPredictionCallback();
    void bindSensorCallback();
    void bindStateCallback();
    void bindRuntimeCallback();



    QValueAxis* axisX = nullptr;
    QValueAxis* axisY = nullptr;
    int loadCountSTL = 0;
    
    // 3D模型数据
    QVector<QVector3D> stlVertices;  // STL顶点数据

    // 面信息结构
    struct FaceInfo {
        vtkIdType p1;  // 第一个顶点索引
        vtkIdType p2;  // 第二个顶点索引
        vtkIdType p3;  // 第三个顶点索引
    };
    std::vector<FaceInfo> faces;  // 存储所有面的信息

    // VTK核心组件
    vtkSmartPointer<vtkPolyData> surface;  // 全局模型数据
    vtkSmartPointer<vtkPoints> dynamicPoints;  // 动态更新的顶点坐标
    vtkSmartPointer<vtkCellArray> polys;  // 面拓扑结构
    vtkSmartPointer<vtkDoubleArray> faceColorValues;  // 面颜色值数组
    vtkSmartPointer<vtkDoubleArray> vertexValues;  // 顶点属性值数组
    vtkSmartPointer<vtkLookupTable> colorTable;  // 颜色映射表
    vtkSmartPointer<vtkGenericOpenGLRenderWindow> renderWindow;
    vtkSmartPointer<vtkPolyDataMapper> mapper;  // 映射器
    vtkSmartPointer<vtkActor> actor;  // 演员
    vtkSmartPointer<vtkRenderer> renderer;  // 渲染器
    vtkSmartPointer<vtkScalarBarActor> scalarBar;
    std::vector<double> pointValues;  // 顶点属性值缓存
    QTimer* vtkSizeTimer = nullptr;
    std::vector < VTKSingleDialog*> vtkField;//场窗口序列
    std::vector <QCheckBox*>     checkBoxes;//控制显示

    VTKSingleDialog* vtkMarker = nullptr;//传感器三维窗口
    std::vector < ChartSingleDialog*> chartMarkers;//传感器曲线图
    ChartSingleDialog* chartInif = nullptr;
    std::vector < std::vector<std::array<double, 3>>> markerPoints;//传感器标记点序列
    std::vector < launchsupport::SensorViewModel> sensorPkts;//传感器数据
    std::vector < std::vector <double>> sensorss;
    std::vector < std::vector <std::vector < std::vector <double>>>> sensorsss;

    VTKSingleDialog* vtkXYZDlg = nullptr;//三维状态窗口

    QTimer* vtkXYZSizeTimer = nullptr;
    QVector<bool> visibilitys;
    QVector<bool> visibilityVtks;
    int sensorflg = 0;
    QStringList sensorNames;
    std::vector<int> cachedFaceIds; // 预加载的面索引缓存
    bool isFaceDataCached = false;  // 缓存是否初始化完成
    std::chrono::high_resolution_clock::time_point lastRenderTime; // 帧率控制
    double lastMin = -1.0, lastMax = -1.0; // 记录上一次颜色范围

    int currentAxis;                 // 当前映射轴
    qint64 frameCount;               // 帧数计数
    double currentFps;               // 当前帧率

    QElapsedTimer* frameTimer;       // 计时器
    QTimer* frameRateTimer = nullptr;          // 帧率刷新定时器
    
    QVector<float> axisValues;
    void initModel();
    void initModelTest();
    void redrawAllMarkers(QVector<bool> visibility);
    void layoutVtk(QVector<bool> visibility);
    void updateCharts(const  QVector<bool>& visibility,std::vector<std::vector<double>> yData);
    void rebuildSensorCurveCharts_(int index, bool replayHistory);
    void refreshPlatformCoreParameterChart_(const QJsonObject& timelineRoot);
    void updateChartData(const launchsupport::PredictionViewModel& DTO);
    QIcon createFixedIcon(const QString& iconPath, const QSize& iconSize);
private slots:
    void onCheckBoxToggled();
    void comboBoxInfCurrentIndexChanged(int index);
    void onPlatformBranchChanged(int index);
    void onPlatformFrameSliderChanged(int value);
    void onPlatformFollowLatestToggled(bool checked);
    void onPlatformFieldVisibilityChanged();

private:
    std::shared_ptr<const launchsupport::RuntimeViewModel> runtime_view_;
    double currentX = 0;
    double currentY = 0.0;
    double currentZ = 0.0;
signals:
    void dataChanged(launchsupport::PredictionViewModel DTO);
    void SensorChanged(launchsupport::SensorViewModel SEP);
private slots:
    void syncVTKSize();
    void setAngle();
    void change3DValue(const launchsupport::PredictionViewModel& DTO);
    void change3DRanges(QVector<double> rMin, QVector<double> rMax);

    void on_comboBox_4_currentIndexChanged(int index);
    void on_spinBox_valueChanged(int arg1);
    void on_spinBox_2_valueChanged(int arg1);
    //end
private:
    QList<QLineSeries*> seriesList; // 存储所有曲线系列
    int count;                      // 数据点计数器
    QVector<QVector<double>> ranges;//0 min 1 max
private slots:
    void on_trainBtn_clicked();//反演模型训练
    void on_pushButton_37_clicked();//反演模型保存
private:
    std::shared_ptr<launchsupport::IControllerBackend> controller_backend_;
    bool runtime_initialized_ = false;
    bool shutdown_prepared_ = false;

    struct PlatformSensorLayoutView {
        QString resource_id;
        QString display_name;
        QString channel_prefix;
        contracts::SubjectType subject = contracts::SubjectType::P;
        int value_dim = 1;
        std::vector<std::array<double, 3>> nodes;
    };

    struct PlatformSensorFrameView {
        int loop = -1;
        qint64 stamp_ns = 0;
        std::map<QString, std::vector<double>> values_by_resource;
    };

    struct PlatformFieldValueCacheEntry {
        qint64 mtime_ms = -1;
        qint64 size_bytes = -1;
        std::vector<double> values;
        QString error;
    };

    launchsupport::PlatformRunSnapshotView platform_snapshot_;
    QComboBox* platformBranchCombo_ = nullptr;
    QSlider* platformFrameSlider_ = nullptr;
    QCheckBox* platformFollowLatestCheck_ = nullptr;
    QLabel* platformFrameProgressLabel_ = nullptr;
    QLabel* platformFrameStatusLabel_ = nullptr;
    QWidget* platformFieldCheckContainer_ = nullptr;
    QGridLayout* platformFieldCheckLayout_ = nullptr;
    QGridLayout* platformFieldGridLayout_ = nullptr;
    std::map<QString, flightenv::ui::demo::VtkModelFieldWidget*> platformFieldWidgets_;
    std::map<QString, bool> platformFieldVisibility_;
    std::vector<QString> platformFieldOrder_;
    std::vector<int> platformFrameLoops_;
    QString platformCurrentBranchId_;
    int platformCurrentLoop_ = -1;
    bool platformStreamingPaused_ = false;
    bool platformUpdatingUi_ = false;
    bool platformSensorLayoutsLoaded_ = false;
    bool platformSensorFramesLoaded_ = false;
    int platformSensorAppliedLoop_ = -1;
    int platformSensorDisplayIndex_ = -1;
    QTimer* platformRenderTimer_ = nullptr;
    QString platformLastRenderedFrameSignature_;
    QString platformFieldControlSignature_;
    QString platformLastSeriesManifestPath_;
    qint64 platformLastSeriesManifestMtimeMs_ = -1;
    qint64 platformLastSeriesManifestSize_ = -1;
    QString platformLastRunTimelinePath_;
    qint64 platformLastRunTimelineMtimeMs_ = -1;
    qint64 platformLastRunTimelineSize_ = -1;
    int platformLastCoreParameterLoop_ = -1000000000;
    std::map<QString, PlatformFieldValueCacheEntry> platformFieldValueCache_;
    std::set<QString> platformFieldValuePending_;
    std::vector<PlatformSensorLayoutView> platformSensorLayouts_;
    std::vector<PlatformSensorFrameView> platformSensorFrames_;

private:
     QScrollArea* chartScrollArea = nullptr;
     QWidget* chartContainerWidget = nullptr;
     QGridLayout* chartGridLayout = nullptr;
     QTableWidget* platformSensorTable_ = nullptr;
protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
private:
    QScrollArea* fieldScrollArea = nullptr; //物理场集群区域
    QWidget* vtkContainer = nullptr;
    std::vector < std::vector <QString>> tableItemContents;


    std::vector<double>  historyMax;
};

