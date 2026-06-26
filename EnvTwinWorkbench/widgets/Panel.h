#pragma once

#include <QFrame>

class QLabel;
class QHBoxLayout;
class QVBoxLayout;

namespace twin {

// 卡片面板：等价 htmlUI 的 .panel —— 标题行（panel-h）+ 内容区（panel-b）。
// 通过 setObjectName 命中 twin.qss 的 #Panel / #PanelHeader / #PanelTitle 样式。
class Panel : public QFrame {
    Q_OBJECT
public:
    explicit Panel(const QString& title, QWidget* parent = nullptr);

    // 设置内容区的布局承载控件；调用方把自己的内容塞进返回的 body widget。
    QWidget* body() const { return body_; }
    // 内容区的垂直布局，调用方用它 addWidget（可带 stretch）。
    QVBoxLayout* bodyLayout() const { return bodyLayout_; }

    // 在标题右侧追加一个控件（例如徽章、按钮、字段切换 tab）。
    void addHeaderWidget(QWidget* widget);
    void setSubtitle(const QString& text);

private:
    QHBoxLayout* headerLayout_ = nullptr;
    QLabel* titleLabel_ = nullptr;
    QLabel* subLabel_ = nullptr;
    QWidget* body_ = nullptr;
    QVBoxLayout* bodyLayout_ = nullptr;
};

} // namespace twin
