#pragma once

#include "../datahub/PdkUiReaders.h"

#include <QWidget>

class QLabel;
class QTableWidget;
class QVBoxLayout;

namespace twin {

class KvList;

// 算子库页：对象包 AtomicOperator 的平台视图。
// UI 显示 operator_id/family/ports/resources/backend/display descriptor，
// 不把 DLL/EXE/ROS2 节点名当作算子身份。
class OperatorLibraryPage : public QWidget {
    Q_OBJECT
public:
    OperatorLibraryPage(QString objectPackageRoot, QWidget* parent = nullptr);

    // 绑定当前 run evidence，使算子详情的 renderer 能填充真实场/序列数据。
    void setEvidenceRoot(const QString& evidenceRoot);

private:
    void showDetail(int row);
    void rebuildRenderer();

    PdkObjectPackageView objectPackage_;
    QString objectPackageRoot_;
    QString evidenceRoot_;
    QTableWidget* table_ = nullptr;
    QLabel* detailTitle_ = nullptr;
    KvList* detailKv_ = nullptr;
    QLabel* rendererMatch_ = nullptr;
    QWidget* rendererHost_ = nullptr;
    QVBoxLayout* rendererHostLayout_ = nullptr;
    QWidget* currentRenderer_ = nullptr;
    int currentRow_ = -1;
};

} // namespace twin
