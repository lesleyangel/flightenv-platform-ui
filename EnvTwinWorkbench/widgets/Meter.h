#pragma once

#include <QColor>
#include <QWidget>

namespace twin {

// 进度/损伤条：等价 htmlUI .meter —— 背景槽 + 着色填充。
// 健康账本损伤值（0–1）、overview 占用率等用它。
class Meter : public QWidget {
    Q_OBJECT
public:
    explicit Meter(QWidget* parent = nullptr);

    void setValue(double value01);              // 0..1
    void setColor(const QColor& color);

protected:
    void paintEvent(QPaintEvent* event) override;
    QSize sizeHint() const override;

private:
    double value_ = 0.0;
    QColor color_;
};

} // namespace twin
