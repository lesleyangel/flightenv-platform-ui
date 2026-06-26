#pragma once

#include <QJsonObject>
#include <QString>
#include <QWidget>

#include <vector>

class QLabel;
class QTableWidget;

namespace twin {

class BranchFieldPanel;
class BranchSeriesPanel;
class BranchStatePanel;
class BranchTimelineWidget;
class BranchTreeWidget;
class LegacyRunCatalogSource;
class LiveDataHub;

struct ReplayRunEntry {
    QString source;
    QString runId;
    QString status;
    QString backend;
    QString summary;
    QString runDir;
};

class ReplayPage final : public QWidget {
    Q_OBJECT
public:
    ReplayPage(LegacyRunCatalogSource* legacyRunCatalog,
               QString evidenceRoot,
               QString objectPackageRoot,
               QWidget* parent = nullptr);

public slots:
    void setCurrentEvidenceRoot(const QString& evidenceRoot);

signals:
    void navigateTo(const QString& page);

private slots:
    void onTimeline(const QJsonObject& timeline);

private:
    void rebuildRunEntries(const QString& preferredRunDir = {});
    void addRunEntry(const ReplayRunEntry& entry);
    void showRun(int runRow);
    void applyBranchSnapshot(const QJsonObject& timeline);
    void selectBranch(const QString& branchId);
    void selectTimelinePoint(const QString& branchId, int loopIterationIndex);
    QString runDirForRow(int runRow) const;

    LegacyRunCatalogSource* legacyRunCatalog_ = nullptr;
    QString fallbackEvidenceRoot_;
    QString objectPackageRoot_;
    LiveDataHub* replayHub_ = nullptr;
    QTableWidget* runTable_ = nullptr;
    QLabel* runTitle_ = nullptr;
    QLabel* runSummary_ = nullptr;
    BranchTreeWidget* branchTree_ = nullptr;
    BranchTimelineWidget* branchTimeline_ = nullptr;
    BranchStatePanel* branchState_ = nullptr;
    BranchFieldPanel* branchField_ = nullptr;
    BranchSeriesPanel* branchSeries_ = nullptr;
    QJsonObject latestTimeline_;
    QString currentBranchId_;
    int currentLoopIterationIndex_ = -1;
    std::vector<ReplayRunEntry> runEntries_;
};

} // namespace twin
