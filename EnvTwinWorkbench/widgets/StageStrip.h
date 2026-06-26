#pragma once

#include <QString>
#include <QWidget>

#include <vector>

namespace twin {

// 运行链路状态条：等价 htmlUI overview 的 .stage-row —— 一排带状态色条的算子卡片 + 箭头。
// 每个 stage 显示名称/类型/主数值/副说明，顶部按状态着色。
class StageStrip : public QWidget {
    Q_OBJECT
public:
    enum class Status { Ok, Warn, Fail, Unknown, Running };

    struct Stage {
        QString name;
        QString type;
        QString value;
        QString unit;
        QString sub;
        Status status = Status::Unknown;
    };

    explicit StageStrip(QWidget* parent = nullptr);
    void setStages(std::vector<Stage> stages);

protected:
    void paintEvent(QPaintEvent* event) override;
    QSize sizeHint() const override;

private:
    std::vector<Stage> stages_;
};

} // namespace twin
