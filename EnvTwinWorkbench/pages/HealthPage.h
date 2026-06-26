#pragma once

#include <QString>
#include <QWidget>

#include "../datahub/PdkUiReaders.h"

class QLabel;
class QTableWidget;

namespace flightenv::ui::demo { class VtkModelFieldWidget; }
namespace flightenv::ui::display { class ScalarTrendWidget; }

namespace twin {

class KvList;

// 健康账本页：等价 htmlUI health.png —— 左侧对象/区域累计损伤，中间寿命场(VTK)+损伤趋势，
// 右侧当前评估(RUL/首超)。区域来自 catalog objects；损伤/RUL/趋势来自最近 run 的 graph_outputs.json。
class HealthPage : public QWidget {
    Q_OBJECT
public:
    HealthPage(QString objectPackageRoot, QString evidenceRoot, QWidget* parent = nullptr);

signals:
    void navigateTo(const QString& page);

private:
    void loadEvidenceAssessment();

    PdkObjectPackageView objectPackage_;
    QString objectPackageRoot_;
    QString evidenceRoot_;
    flightenv::ui::demo::VtkModelFieldWidget* fieldWidget_ = nullptr;
    flightenv::ui::display::ScalarTrendWidget* damageTrend_ = nullptr;
    flightenv::ui::display::ScalarTrendWidget* rulTrend_ = nullptr;
    KvList* assessKv_ = nullptr;
    QLabel* assessValue_ = nullptr;
    QTableWidget* fieldTable_ = nullptr;
};

} // namespace twin
