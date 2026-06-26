#pragma once

#include <QHash>
#include <QMainWindow>
#include <QString>

class QJsonObject;
class QStackedWidget;

namespace twin {

class LegacyRunCatalogSource;
class ConfigPage;
class ContextBar;
class DataPlanePage;
class DiagnosticsPage;
class GraphPage;
class HealthPage;
class LiveDataHub;
class ModelsPage;
class NavRail;
class ObjectPage;
class OnlinePage;
class OperatorLibraryPage;
class OverviewPage;
class PlatformRunController;
class ReplayPage;
class RuntimeHostPage;

// 主窗口只装配平台对象包、workflow/operator spec 与 runtime evidence 的展示页。
// ROS2、CLI、DLL 等只作为 Runtime Host 的 adapter backend，不在 UI 进程里直接启动。
class TwinWorkbenchWindow : public QMainWindow {
    Q_OBJECT
public:
    TwinWorkbenchWindow(
        LiveDataHub* hub,
        LegacyRunCatalogSource* legacyRunCatalog,
        QString workspaceRoot,
        QString objectPackageRoot,
        QWidget* parent = nullptr);

private slots:
    void showPage(const QString& key);
    void onTimeline(const QJsonObject& timeline);
    void loadObjectPackageDirectory();
    void loadObjectPackageFile();

private:
    void addPage(const QString& key, QWidget* page);
    void setupMenus();
    void rebuildPages();
    void setObjectPackageRoot(const QString& objectPackageRoot);
    void updateObjectContext();

    LiveDataHub* hub_ = nullptr;
    LegacyRunCatalogSource* legacyRunCatalog_ = nullptr;
    QString workspaceRoot_;
    QString objectPackageRoot_;
    PlatformRunController* runController_ = nullptr;
    NavRail* nav_ = nullptr;
    ContextBar* ctx_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    OnlinePage* onlinePage_ = nullptr;
    GraphPage* graphPage_ = nullptr;
    ReplayPage* replayPage_ = nullptr;
    QHash<QString, int> pageIndex_;
    QString currentPageKey_ = QStringLiteral("overview");
};

} // namespace twin
