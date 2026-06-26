#pragma once

#include <QString>
#include <QWidget>

#include "../datahub/PdkUiReaders.h"

class QListWidget;
class QLabel;
class QTableWidget;
class QVBoxLayout;

namespace flightenv::ui::display { class WorkflowDagWidget; }

namespace twin {

class Panel;

// Workflow 编排页：左侧来自对象包 workflows/*.json，
// 右侧展示该 workflow 中的 AtomicOperator 节点、family、端口和资源。
class GraphPage : public QWidget {
    Q_OBJECT
public:
    explicit GraphPage(QString objectPackageRoot, QWidget* parent = nullptr);

    // 主 run 目录（含 online_filter 子目录）；不同模板读不同子目录的 evidence。
    void setEvidenceRoot(const QString& runDir);

public slots:
    // timeline 刷新即重读 evidence（同一 run 推进时画布跟随更新）。
    void refresh();

private:
    void selectRow(int index);
    void showWorkflow(int workflowIndex);
    void showNodeRenderer(int row);
    void showRuntimeEvidenceForWorkflow(const QString& workflowId, QString* summarySuffix);

    PdkObjectPackageView objectPackage_;
    QString evidenceRoot_;
    QListWidget* templateList_ = nullptr;
    flightenv::ui::display::WorkflowDagWidget* canvas_ = nullptr;
    QLabel* canvasHint_ = nullptr;
    QLabel* evidenceText_ = nullptr;
    QTableWidget* nodeTable_ = nullptr;
    QTableWidget* edgeTable_ = nullptr;
    QLabel* rendererMatch_ = nullptr;
    QWidget* rendererHost_ = nullptr;
    QVBoxLayout* rendererHostLayout_ = nullptr;
    QWidget* currentRenderer_ = nullptr;
    int currentRow_ = 0;
    int currentNodeRow_ = 0;
};

} // namespace twin
