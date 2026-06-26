#pragma once

#include <QWidget>

class QTimer;

namespace twin {

// 运行状态药丸：等价 htmlUI 顶栏右侧 .runpill。
// running 态圆点带脉冲动画，其余态静态着色。
class RunStatusPill : public QWidget {
    Q_OBJECT
public:
    enum class State { Idle, Running, Degraded, Failed, Replaying };

    explicit RunStatusPill(QWidget* parent = nullptr);

    void setState(State state, const QString& text);

protected:
    void paintEvent(QPaintEvent* event) override;
    QSize sizeHint() const override;

private:
    State state_ = State::Idle;
    QString text_ = QStringLiteral("空闲");
    QTimer* pulseTimer_ = nullptr;
    double pulsePhase_ = 0.0;
};

} // namespace twin
