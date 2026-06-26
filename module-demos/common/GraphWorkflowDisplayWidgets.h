#pragma once

#include <EnvContracts/dto/TrajectoryPredictionFrame.hpp>

#include <QColor>
#include <QSizePolicy>
#include <QString>
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
