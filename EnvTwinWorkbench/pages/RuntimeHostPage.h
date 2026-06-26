#pragma once

#include "../datahub/PdkUiReaders.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QWidget>

class QLabel;
class QTableWidget;

namespace twin {

class KvList;

// Runtime Host 页：展示 workflow 编译产物、统一时钟/调度/状态存储计划，
// 以及当前 evidence 中 Runtime Host 的运行节点、调度和 checkpoint 状态。
class RuntimeHostPage : public QWidget {
    Q_OBJECT
public:
    RuntimeHostPage(QString evidenceRoot, QString objectPackageRoot, QWidget* parent = nullptr);

public slots:
    void setEvidenceRoot(const QString& evidenceRoot);

private:
    void loadEvidenceFiles();
    void rebuildRuntimeTables();
    void populateHostSummary();
    void showCompiledWorkflow(int row);
    void showRun(int row);
    void showBranch(int row);
    void showEvent(int row);
    void showSession(int row);

    QString evidenceRoot_;
    QString workspaceRoot_;
    PdkObjectPackageView objectPackage_;
    QVector<PdkCompiledWorkflowView> compiledWorkflows_;
    QStringList runDirs_;
    QJsonObject progressJson_;
    QJsonObject hostEvidenceJson_;
    QJsonObject branchRegistryJson_;
    QJsonObject runtimeEventsJson_;
    QJsonObject initializationJson_;
    QJsonObject timelineIndexJson_;
    QJsonArray branches_;
    QJsonArray events_;
    QJsonArray sessions_;
    KvList* hostKv_ = nullptr;
    QTableWidget* compiledTable_ = nullptr;
    QTableWidget* runTable_ = nullptr;
    QTableWidget* branchTable_ = nullptr;
    QTableWidget* eventTable_ = nullptr;
    QTableWidget* sessionTable_ = nullptr;
    QLabel* detailTitle_ = nullptr;
    KvList* detailKv_ = nullptr;
};

} // namespace twin
