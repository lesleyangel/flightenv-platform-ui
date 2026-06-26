#pragma once

#include <EnvContracts/dto/TrajectoryPredictionFrame.hpp>

#include <QColor>
#include <QSizePolicy>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace flightenv::ui::display {

struct WorkflowStepView {
    QString node_id;
    QString operator_type;
    QString status;
    QString inputs;
    QString outputs;
    std::int64_t duration_ms = 0;
    std::size_t output_bytes = 0;
};

class WorkflowPathWidget final : public QWidget {
public:
    explicit WorkflowPathWidget(QWidget* parent = nullptr);

    void setSteps(std::vector<WorkflowStepView> steps);
    void clear();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    std::vector<WorkflowStepView> steps_;
};

// 工作流 DAG 节点/边：按 stage 分列，列内并行节点纵向堆叠，画真实依赖边。
struct WorkflowDagNode {
    QString id;       // 唯一键：stageId + "." + nodeId
    QString label;    // 展示用 node_id
    QString family;   // 算子族（决定配色）
    QString status;   // ok / missing
    QString stageId;  // 所属 stage
};

struct WorkflowDagEdge {
    QString fromId;
    QString toId;
};

class WorkflowDagWidget final : public QWidget {
public:
    explicit WorkflowDagWidget(QWidget* parent = nullptr);

    // stageOrder 给出列的从左到右顺序；nodes/edges 用 stageId.nodeId 作唯一键。
    void setGraph(QStringList stageOrder,
                  std::vector<WorkflowDagNode> nodes,
                  std::vector<WorkflowDagEdge> edges);
    void clear();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QStringList stageOrder_;
    std::vector<WorkflowDagNode> nodes_;
    std::vector<WorkflowDagEdge> edges_;
};

class TrajectoryPathWidget final : public QWidget {
public:
    explicit TrajectoryPathWidget(QWidget* parent = nullptr);

    void setTrajectory(const contracts::TrajectoryPredictionFrame& trajectory);
    void clear();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    contracts::TrajectoryPredictionFrame trajectory_;
    bool hasTrajectory_ = false;
};

class ScalarTrendWidget final : public QWidget {
public:
    explicit ScalarTrendWidget(QWidget* parent = nullptr);

    void setTitle(const QString& title, const QString& unit = QString());
    void setSamples(const std::vector<double>& samples);
    void setFixedRange(double minValue, double maxValue);
    void clearFixedRange();
    void clear();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString title_;
    QString unit_;
    std::vector<double> samples_;
    std::optional<std::pair<double, double>> fixedRange_;
};

} // namespace flightenv::ui::display
