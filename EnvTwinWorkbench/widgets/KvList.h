#pragma once

#include <QWidget>

class QGridLayout;
class QLabel;

namespace twin {

// key-value 行列表：等价 htmlUI 的 .kv —— 左键名（灰）+ 右值（深，可 mono）。
// 在线运行页的"实时输入""滤波状态"卡用它逐行展示标量。
class KvList : public QWidget {
    Q_OBJECT
public:
    explicit KvList(QWidget* parent = nullptr);

    // 追加一行；mono=true 时值用等宽字体（数值列）。返回值标签以便后续刷新。
    QLabel* addRow(const QString& key, const QString& value, bool mono = true);
    void clear();

private:
    QGridLayout* grid_ = nullptr;
    int row_ = 0;
};

} // namespace twin
