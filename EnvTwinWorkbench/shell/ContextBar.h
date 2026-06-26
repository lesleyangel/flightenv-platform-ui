#pragma once

#include <QWidget>

class QHBoxLayout;
class QLabel;
class QPushButton;

namespace twin {

class RunStatusPill;

// 顶部上下文栏：等价 htmlUI 顶栏 —— 对象 / 阶段 / 模式 / RUN / GRAPH 字段竖排 k/v，
// 右侧运行状态药丸。字段值由窗口在拿到 runtime snapshot / evidence 后刷新。
class ContextBar : public QWidget {
    Q_OBJECT
public:
    explicit ContextBar(QWidget* parent = nullptr);

    void setObjectField(const QString& title, const QString& id);
    void setPhase(const QString& phase);
    void setMode(const QString& mode);
    void setRun(const QString& runId);
    void setGraph(const QString& graphId);
    void setWorkflow(const QString& workflowId);
    void setRunProfile(const QString& profileId);
    void setClock(const QString& clockText);
    void setRuntimeStatus(const QString& statusText);
    void setEvidenceRoot(const QString& evidenceRoot);
    QPushButton* addActionButton(const QString& text);
    RunStatusPill* statusPill() const { return pill_; }

private:
    // 在栏内追加一个竖排 k/v 字段，返回值标签以便刷新。
    QLabel* addField(const QString& key, const QString& value, int minWidth = 0);

    QHBoxLayout* layout_ = nullptr;
    QLabel* objectVal_ = nullptr;
    QLabel* phaseVal_ = nullptr;
    QLabel* modeVal_ = nullptr;
    QLabel* runVal_ = nullptr;
    QLabel* graphVal_ = nullptr;
    QLabel* workflowVal_ = nullptr;
    QLabel* profileVal_ = nullptr;
    QLabel* clockVal_ = nullptr;
    QLabel* runtimeVal_ = nullptr;
    QLabel* evidenceVal_ = nullptr;
    RunStatusPill* pill_ = nullptr;
};

} // namespace twin
