#pragma once

#include "../datahub/PdkUiReaders.h"

#include <QString>
#include <QWidget>

class QLabel;
class QTableWidget;

namespace flightenv::ui::demo {
class VtkModelFieldWidget;
}

namespace twin {

class KvList;
struct LoadedFieldArtifact;

class DataPlanePage final : public QWidget {
    Q_OBJECT
public:
    DataPlanePage(QString evidenceRoot, QString objectPackageRoot, QWidget* parent = nullptr);

public slots:
    void setEvidenceRoot(const QString& evidenceRoot);

private:
    void rebuildRunTable(const QString& preferredRunDir = QString());
    void populateSummary();
    void loadSelectedRun(int row);
    void showEntry(int row);
    void renderEntryField(const PdkDataPlaneEntryView& entry);
    void applyFieldLoad(quint64 serial, LoadedFieldArtifact loaded);

    QString evidenceRoot_;
    QString objectPackageRoot_;
    QString currentRunDir_;
    QStringList runDirs_;
    PdkDataPlaneView currentDataPlane_;
    quint64 fieldLoadSerial_ = 0;
    KvList* summaryKv_ = nullptr;
    QTableWidget* runTable_ = nullptr;
    QTableWidget* entryTable_ = nullptr;
    QLabel* detailTitle_ = nullptr;
    KvList* detailKv_ = nullptr;
    flightenv::ui::demo::VtkModelFieldWidget* fieldWidget_ = nullptr;
};

} // namespace twin
