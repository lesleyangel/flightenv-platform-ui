#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>
#include <QWidget>

class QLabel;
class QTableWidget;
class QTreeWidget;

namespace flightenv::ui::display {
class ScalarTrendWidget;
}

namespace flightenv::ui::demo {
class VtkModelFieldWidget;
}

namespace twin {

struct LoadedFieldArtifact;
struct LoadedMeshLayoutCatalog;

// 分支运行浏览控件只消费 LiveDataHub 输出的 branch/timeline snapshot。
// 它们不读大场文件、不拼运行时路径，也不直接调用算子或 ROS2。

class BranchTreeWidget final : public QWidget {
    Q_OBJECT
public:
    explicit BranchTreeWidget(QWidget* parent = nullptr);

    void setSnapshot(const QJsonObject& snapshot);
    void setCurrentBranch(const QString& branchId);

signals:
    void branchSelected(const QString& branchId);

private:
    void rebuild();

    QTreeWidget* tree_ = nullptr;
    QJsonObject snapshot_;
    QString currentBranchId_;
};

class BranchTimelineWidget final : public QWidget {
    Q_OBJECT
public:
    explicit BranchTimelineWidget(QWidget* parent = nullptr);

    void setSnapshot(const QJsonObject& snapshot);
    void setCurrentBranch(const QString& branchId);
    void setCurrentTimelinePoint(const QString& branchId, int loopIterationIndex);

signals:
    void timelinePointSelected(const QString& branchId, int loopIterationIndex);

private:
    void rebuild();
    void selectCurrentRow();

    QTableWidget* table_ = nullptr;
    QJsonObject snapshot_;
    QString currentBranchId_;
    int currentLoopIterationIndex_ = -1;
};

class BranchStatePanel final : public QWidget {
    Q_OBJECT
public:
    explicit BranchStatePanel(QWidget* parent = nullptr);

    void setSnapshot(const QJsonObject& snapshot);
    void setCurrentTimelinePoint(const QString& branchId, int loopIterationIndex);

private:
    void rebuild();

    QLabel* titleLabel_ = nullptr;
    QTableWidget* stateTable_ = nullptr;
    QTableWidget* filterTable_ = nullptr;
    QJsonObject snapshot_;
    QString currentBranchId_;
    int currentLoopIterationIndex_ = -1;
};

class BranchFieldPanel final : public QWidget {
    Q_OBJECT
public:
    explicit BranchFieldPanel(QWidget* parent = nullptr);

    void setSnapshot(const QJsonObject& snapshot);
    void setCurrentTimelinePoint(const QString& branchId, int loopIterationIndex);
    void setAssetRoot(const QString& assetRoot);

signals:
    void fieldOptionSelected(const QString& optionId,
                             const QString& branchId,
                             int loopIterationIndex);

private:
    void rebuild();
    void renderSelectedField();
    void applyLoadedField(quint64 serial, LoadedFieldArtifact loaded);
    void renderGeometryPreview();
    void applyLoadedGeometryPreview(quint64 serial, LoadedMeshLayoutCatalog loaded);

    QLabel* titleLabel_ = nullptr;
    QLabel* renderStatusLabel_ = nullptr;
    QTableWidget* fieldTable_ = nullptr;
    QTableWidget* qoiTable_ = nullptr;
    flightenv::ui::demo::VtkModelFieldWidget* vtkWidget_ = nullptr;
    QJsonObject snapshot_;
    QString currentBranchId_;
    int currentLoopIterationIndex_ = -1;
    QVector<QJsonObject> currentFieldOptions_;
    QString assetRoot_;
    QString lastRenderedFieldKey_;
    QString inFlightFieldKey_;
    QString lastPreviewSnapshotKey_;
    QString inFlightPreviewSnapshotKey_;
    quint64 fieldLoadSerial_ = 0;
    quint64 previewLoadSerial_ = 0;
    bool fieldLoadInFlight_ = false;
    bool fieldLoadPending_ = false;
    bool previewLoadInFlight_ = false;
    bool showingGeometryPreview_ = false;
};

class BranchSeriesPanel final : public QWidget {
    Q_OBJECT
public:
    explicit BranchSeriesPanel(QWidget* parent = nullptr);

    void setSnapshot(const QJsonObject& snapshot);
    void setCurrentBranch(const QString& branchId);

private:
    void rebuild();

    QLabel* titleLabel_ = nullptr;
    flightenv::ui::display::ScalarTrendWidget* altitudeTrend_ = nullptr;
    flightenv::ui::display::ScalarTrendWidget* machTrend_ = nullptr;
    flightenv::ui::display::ScalarTrendWidget* remainingLifeTrend_ = nullptr;
    QTableWidget* seriesTable_ = nullptr;
    QJsonObject snapshot_;
    QString currentBranchId_;
};

} // namespace twin
