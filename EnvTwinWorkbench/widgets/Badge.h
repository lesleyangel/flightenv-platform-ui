#pragma once

#include <QWidget>

class QLabel;

namespace twin {

// 状态徽章：等价 htmlUI 的 .badge —— 圆点 + 文本，按状态着色。
// 自绘背景/边框（QSS 对动态着色不便），颜色取自 Palette。
class Badge : public QWidget {
    Q_OBJECT
public:
    enum class Kind { Ok, Warn, Fail, Unknown, Accent, Running };

    explicit Badge(QWidget* parent = nullptr);

    void setText(const QString& text);
    void setKind(Kind kind);

protected:
    void paintEvent(QPaintEvent* event) override;
    QSize sizeHint() const override;

private:
    QString text_;
    Kind kind_ = Kind::Unknown;
};

} // namespace twin
