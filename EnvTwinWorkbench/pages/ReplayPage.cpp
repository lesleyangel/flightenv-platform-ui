#include "ReplayPage.h"

#include "../datahub/LegacyRunCatalogSource.h"
#include "../datahub/LiveDataHub.h"
#include "../widgets/BranchRunExplorerWidgets.h"
#include "../widgets/Panel.h"

#include <QAbstractItemView>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace twin {
namespace {

QTableWidget* makeTable(const QStringList& headers, QWidget* parent)
{
    auto* table = new QTableWidget(parent);
    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->verticalHeader()->hide();
    table->horizontalHeader()->setStretchLastSection(true);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setAlternatingRowColors(true);
    return table;
}

QScrollArea* makeColumnScroll(QWidget* content, QWidget* parent)
{
    auto* scroll = new QScrollArea(parent);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(content);
    return scroll;
}

QString normalizePath(QString path)
{
    path = QDir::fromNativeSeparators(path.trimmed());
    if (path.startsWith(QStringLiteral("file://"))) {
        path = path.mid(QStringLiteral("file://").size());
    }
    if (path.startsWith(QStringLiteral("//?/"))) {
        path = path.mid(QStringLiteral("//?/").size());
    }
    if (path.startsWith(QStringLiteral("\\\\?\\"))) {
        path = path.mid(4);
    }
    return QDir::cleanPath(path);
}

QString canonicalDir(QString path)
{
    path = normalizePath(std::move(path));
    if (path.isEmpty()) {
        return {};
    }
    const QFileInfo info(path);
    return info.exists() ? QDir::cleanPath(info.absoluteFilePath()) : path;
}

QString shortPath(QString path)
{
    path = normalizePath(std::move(path));
    path.replace(QLatin1Char('/'), QLatin1Char('\\'));
    if (path.size() <= 92) {
        return path;
    }
    return path.left(42) + QStringLiteral(" ... ") + path.right(42);
}

QJsonObject readJsonObject(const QString& path)
{
    QFile file(normalizePath(path));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    return doc.isObject() ? doc.object() : QJsonObject{};
}

bool looksLikeReplayRun(const QString& runDir)
{
    const QDir dir(normalizePath(runDir));
    return QFileInfo::exists(dir.filePath(QStringLiteral("run_timeline_index.json"))) ||
           QFileInfo::exists(dir.filePath(QStringLiteral("mainline_progress.json"))) ||
           QFileInfo::exists(dir.filePath(QStringLiteral("runtime_host_evidence.json"))) ||
           QFileInfo::exists(dir.filePath(QStringLiteral("graph_run_evidence.json"))) ||
           QFileInfo::exists(dir.filePath(QStringLiteral("workflow_timeline.json")));
}

QDir workspaceRootFromPath(QString seed)
{
    seed = normalizePath(std::move(seed));
    QDir dir(seed.isEmpty() ? QCoreApplication::applicationDirPath() : seed);
    if (QFileInfo(seed).isFile()) {
        dir = QFileInfo(seed).dir();
    }
    for (int i = 0; i < 10; ++i) {
        if (dir.exists(QStringLiteral("flightenv-controller-ui")) ||
            dir.exists(QStringLiteral("flightenv-platform-pdk")) ||
            dir.exists(QStringLiteral("_local_artifacts"))) {
            return dir;
        }
        if (!dir.cdUp()) {
            break;
        }
    }
    return QDir(QCoreApplication::applicationDirPath());
}

QString primaryBranchId(const QJsonObject& timeline)
{
    const QString primary = timeline.value(QStringLiteral("primary_branch_id")).toString();
    if (!primary.isEmpty()) {
        return primary;
    }
    const QJsonArray branches = timeline.value(QStringLiteral("branch_descriptors")).toArray(
        timeline.value(QStringLiteral("branches")).toArray());
    if (!branches.isEmpty()) {
        const QString branchId = branches.first().toObject().value(QStringLiteral("branch_id")).toString();
        if (!branchId.isEmpty()) {
            return branchId;
        }
    }
    return QStringLiteral("main.online");
}

QJsonArray timelinePoints(const QJsonObject& timeline)
{
    QJsonArray points = timeline.value(QStringLiteral("timeline_points")).toArray();
    if (!points.isEmpty()) {
        return points;
    }
    points = timeline.value(QStringLiteral("online_frames")).toArray();
    for (int i = 0; i < points.size(); ++i) {
        QJsonObject point = points.at(i).toObject();
        point.insert(QStringLiteral("branch_id"), QStringLiteral("main.online"));
        if (!point.contains(QStringLiteral("loop_iteration_index"))) {
            point.insert(QStringLiteral("loop_iteration_index"),
                         point.value(QStringLiteral("frame_index")).toInt(i));
        }
        points.replace(i, point);
    }
    return points;
}

int latestLoopForBranch(const QJsonObject& timeline, const QString& branchId)
{
    int latest = -1;
    for (const QJsonValue& value : timelinePoints(timeline)) {
        const QJsonObject point = value.toObject();
        if (point.value(QStringLiteral("branch_id")).toString(QStringLiteral("main.online")) != branchId) {
            continue;
        }
        latest = std::max(latest,
                          point.value(QStringLiteral("loop_iteration_index"))
                              .toInt(point.value(QStringLiteral("frame_index")).toInt(latest)));
    }
    return latest;
}

QString backendFromRunDir(const QDir& dir)
{
    const QJsonObject hostEvidence = readJsonObject(dir.filePath(QStringLiteral("runtime_host_evidence.json")));
    const QString hostBackend = hostEvidence.value(QStringLiteral("host")).toObject()
        .value(QStringLiteral("execution_backend")).toString();
    if (!hostBackend.isEmpty()) {
        return hostBackend;
    }
    const QJsonObject sessionSummary = readJsonObject(dir.filePath(QStringLiteral("adapter_session_summary.json")));
    const QString sessionBackend = sessionSummary.value(QStringLiteral("execution_backend")).toString();
    if (!sessionBackend.isEmpty()) {
        return sessionBackend;
    }
    const QJsonObject runtimeEvidence = readJsonObject(dir.filePath(QStringLiteral("runtime_evidence.json")));
    return runtimeEvidence.value(QStringLiteral("execution_backend")).toString(QStringLiteral("-"));
}

ReplayRunEntry entryFromRunDir(const QString& source, const QString& runDir)
{
    const QDir dir(canonicalDir(runDir));
    QJsonObject progress = readJsonObject(dir.filePath(QStringLiteral("mainline_progress.json")));
    const QJsonObject evidence = readJsonObject(dir.filePath(QStringLiteral("runtime_host_evidence.json")));
    if (progress.isEmpty()) {
        progress = readJsonObject(dir.filePath(QStringLiteral("graph_run_evidence.json")));
    }

    const QString runId = progress.value(QStringLiteral("run_id")).toString(
        evidence.value(QStringLiteral("run_id")).toString(dir.dirName()));
    const QString status = progress.value(QStringLiteral("status")).toString(
        evidence.value(QStringLiteral("status")).toString(
            looksLikeReplayRun(dir.absolutePath()) ? QStringLiteral("可回放") : QStringLiteral("缺少索引")));

    const int onlineFrames = progress.value(QStringLiteral("online")).toObject()
        .value(QStringLiteral("completed_frames")).toInt(
            progress.value(QStringLiteral("observed_frame_count")).toInt());
    const int predictionRuns = progress.value(QStringLiteral("prediction")).toObject()
        .value(QStringLiteral("completed_runs")).toInt(
            evidence.value(QStringLiteral("summary")).toObject()
                .value(QStringLiteral("completed_prediction_count")).toInt());
    const int branchCount = readJsonObject(dir.filePath(QStringLiteral("branch_registry.json")))
        .value(QStringLiteral("branches")).toArray().size();
    const bool hasTimeline = QFileInfo::exists(dir.filePath(QStringLiteral("run_timeline_index.json")));

    QString summary = QStringLiteral("帧 %1 / 预测分支 %2 / 分支登记 %3")
                          .arg(onlineFrames)
                          .arg(predictionRuns)
                          .arg(branchCount);
    if (hasTimeline) {
        summary += QStringLiteral(" / 有 timeline 索引");
    }
    return {source, runId, status, backendFromRunDir(dir), summary, dir.absolutePath()};
}

} // namespace

ReplayPage::ReplayPage(LegacyRunCatalogSource* legacyRunCatalog,
                       QString evidenceRoot,
                       QString objectPackageRoot,
                       QWidget* parent)
    : QWidget(parent),
      legacyRunCatalog_(legacyRunCatalog),
      fallbackEvidenceRoot_(canonicalDir(std::move(evidenceRoot))),
      objectPackageRoot_(std::move(objectPackageRoot))
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    auto* mainSplitter = new QSplitter(Qt::Horizontal, this);
    mainSplitter->setChildrenCollapsible(false);
    root->addWidget(mainSplitter, 1);

    auto* leftColumn = new QWidget(mainSplitter);
    auto* leftLayout = new QVBoxLayout(leftColumn);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(12);

    auto* listPanel = new Panel(QStringLiteral("运行记录"), leftColumn);
    listPanel->setSubtitle(QStringLiteral("自动识别当前运行包和历史 evidence，可切换主线/预测分支回放"));
    runTable_ = makeTable({
        QStringLiteral("来源"),
        QStringLiteral("run_id"),
        QStringLiteral("状态"),
        QStringLiteral("backend"),
        QStringLiteral("摘要")
    }, listPanel->body());
    listPanel->bodyLayout()->addWidget(runTable_, 1);
    leftLayout->addWidget(listPanel, 3);

    auto* headerPanel = new Panel(QStringLiteral("回放上下文"), leftColumn);
    headerPanel->setSubtitle(QStringLiteral("回放页消费统一 Branch/Timeline snapshot"));
    runTitle_ = new QLabel(QStringLiteral("未选择运行记录"), headerPanel->body());
    runTitle_->setStyleSheet(QStringLiteral("font-weight:700;color:#0f172a;"));
    runTitle_->setWordWrap(true);
    runSummary_ = new QLabel(QStringLiteral("-"), headerPanel->body());
    runSummary_->setWordWrap(true);
    runSummary_->setProperty("muted", true);
    headerPanel->bodyLayout()->addWidget(runTitle_);
    headerPanel->bodyLayout()->addWidget(runSummary_);
    leftLayout->addWidget(headerPanel, 1);

    auto* middleColumn = new QWidget(mainSplitter);
    auto* middleLayout = new QVBoxLayout(middleColumn);
    middleLayout->setContentsMargins(0, 0, 0, 0);
    middleLayout->setSpacing(12);

    auto* treePanel = new Panel(QStringLiteral("分支树"), middleColumn);
    treePanel->setMinimumHeight(280);
    branchTree_ = new BranchTreeWidget(treePanel->body());
    branchTree_->setMinimumHeight(250);
    treePanel->bodyLayout()->addWidget(branchTree_, 1);
    middleLayout->addWidget(treePanel, 2);

    auto* timelinePanel = new Panel(QStringLiteral("时间线"), middleColumn);
    branchTimeline_ = new BranchTimelineWidget(timelinePanel->body());
    timelinePanel->bodyLayout()->addWidget(branchTimeline_, 1);
    middleLayout->addWidget(timelinePanel, 2);

    auto* rightSplitter = new QSplitter(Qt::Vertical, mainSplitter);
    rightSplitter->setChildrenCollapsible(false);

    auto* statePanel = new Panel(QStringLiteral("状态详情"), rightSplitter);
    branchState_ = new BranchStatePanel(statePanel->body());
    statePanel->bodyLayout()->addWidget(branchState_, 1);
    rightSplitter->addWidget(statePanel);

    auto* fieldPanel = new Panel(QStringLiteral("真实场云图"), rightSplitter);
    fieldPanel->setSubtitle(QStringLiteral("完整 field artifact + 对象包 mesh layout + VTK"));
    branchField_ = new BranchFieldPanel(fieldPanel->body());
    branchField_->setAssetRoot(objectPackageRoot_);
    branchField_->setMinimumHeight(520);
    fieldPanel->bodyLayout()->addWidget(branchField_, 1);
    rightSplitter->addWidget(fieldPanel);

    auto* seriesPanel = new Panel(QStringLiteral("分支曲线"), rightSplitter);
    branchSeries_ = new BranchSeriesPanel(seriesPanel->body());
    seriesPanel->bodyLayout()->addWidget(branchSeries_, 1);
    rightSplitter->addWidget(seriesPanel);

    // 右列做成"以云图为主"：状态详情/分支曲线压薄，VTK 吃满主区；同时整体加宽右列、收窄过空的左/中列。
    mainSplitter->addWidget(makeColumnScroll(leftColumn, mainSplitter));
    mainSplitter->addWidget(makeColumnScroll(middleColumn, mainSplitter));
    mainSplitter->addWidget(rightSplitter);
    mainSplitter->setStretchFactor(0, 2);
    mainSplitter->setStretchFactor(1, 2);
    mainSplitter->setStretchFactor(2, 5);
    mainSplitter->setSizes({320, 380, 900});
    rightSplitter->setStretchFactor(0, 1);
    rightSplitter->setStretchFactor(1, 6);
    rightSplitter->setStretchFactor(2, 1);
    rightSplitter->setSizes({150, 700, 160});

    replayHub_ = new LiveDataHub(fallbackEvidenceRoot_, this);
    connect(replayHub_, &LiveDataHub::timelineUpdated, this, &ReplayPage::onTimeline);
    replayHub_->start();

    connect(runTable_, &QTableWidget::currentCellChanged,
            this, [this](int row, int, int, int) { showRun(row); });
    connect(branchTree_, &BranchTreeWidget::branchSelected,
            this, &ReplayPage::selectBranch);
    connect(branchTimeline_, &BranchTimelineWidget::timelinePointSelected,
            this, &ReplayPage::selectTimelinePoint);

    rebuildRunEntries(fallbackEvidenceRoot_);
}

void ReplayPage::setCurrentEvidenceRoot(const QString& evidenceRoot)
{
    const QString cleanRoot = canonicalDir(evidenceRoot);
    if (cleanRoot.isEmpty() || cleanRoot == fallbackEvidenceRoot_) {
        return;
    }
    fallbackEvidenceRoot_ = cleanRoot;
    rebuildRunEntries(cleanRoot);
}

void ReplayPage::addRunEntry(const ReplayRunEntry& entry)
{
    const QString cleanDir = canonicalDir(entry.runDir);
    if (cleanDir.isEmpty()) {
        return;
    }
    const auto sameDir = [&cleanDir](const ReplayRunEntry& existing) {
        return canonicalDir(existing.runDir).compare(cleanDir, Qt::CaseInsensitive) == 0;
    };
    if (std::any_of(runEntries_.begin(), runEntries_.end(), sameDir)) {
        return;
    }
    ReplayRunEntry copy = entry;
    copy.runDir = cleanDir;
    runEntries_.push_back(copy);
}

void ReplayPage::rebuildRunEntries(const QString& preferredRunDir)
{
    const QString preferred = canonicalDir(preferredRunDir);
    const QString previous = runTable_ && runTable_->currentRow() >= 0
        ? runDirForRow(runTable_->currentRow())
        : QString();

    runEntries_.clear();
    if (!fallbackEvidenceRoot_.isEmpty()) {
        addRunEntry(entryFromRunDir(QStringLiteral("当前"), fallbackEvidenceRoot_));
    }

    const QDir workspace = workspaceRootFromPath(
        fallbackEvidenceRoot_.isEmpty() ? QCoreApplication::applicationDirPath() : fallbackEvidenceRoot_);
    const QStringList scanRoots = {
        workspace.filePath(QStringLiteral("_local_artifacts/platform-runtime/mainline-runs")),
        workspace.filePath(QStringLiteral("_local_artifacts/platform-pdk/mainline-runs"))
    };
    for (const QString& scanRoot : scanRoots) {
        QDir dir(scanRoot);
        if (!dir.exists()) {
            continue;
        }
        const QFileInfoList children = dir.entryInfoList(
            QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time | QDir::Reversed);
        for (const QFileInfo& child : children) {
            const QString runDir = child.absoluteFilePath();
            if (looksLikeReplayRun(runDir)) {
                addRunEntry(entryFromRunDir(QStringLiteral("历史"), runDir));
            }
        }
    }

    if (legacyRunCatalog_ && legacyRunCatalog_->ok()) {
        for (const auto& run : legacyRunCatalog_->runs()) {
            const QString runDir = canonicalDir(QString::fromStdString(run.run_dir));
            if (runDir.isEmpty()) {
                continue;
            }
            addRunEntry({
                QStringLiteral("Catalog"),
                QString::fromStdString(run.run_id),
                QString::fromStdString(run.status.empty() ? "登记" : run.status),
                QStringLiteral("legacy_catalog"),
                QString::fromStdString(run.model_asset_summary),
                runDir
            });
        }
    }

    {
        const QSignalBlocker blocker(runTable_);
        runTable_->setRowCount(0);
        for (const ReplayRunEntry& entry : runEntries_) {
            const int row = runTable_->rowCount();
            runTable_->insertRow(row);
            runTable_->setItem(row, 0, new QTableWidgetItem(entry.source));
            runTable_->setItem(row, 1, new QTableWidgetItem(entry.runId));
            runTable_->setItem(row, 2, new QTableWidgetItem(entry.status));
            runTable_->setItem(row, 3, new QTableWidgetItem(entry.backend));
            runTable_->setItem(row, 4, new QTableWidgetItem(entry.summary));
        }
        runTable_->resizeColumnsToContents();
    }

    int selectedRow = -1;
    const QString target = !preferred.isEmpty() ? preferred : previous;
    for (int row = 0; row < static_cast<int>(runEntries_.size()); ++row) {
        if (canonicalDir(runEntries_[static_cast<std::size_t>(row)].runDir)
                .compare(target, Qt::CaseInsensitive) == 0) {
            selectedRow = row;
            break;
        }
    }
    if (selectedRow < 0 && !runEntries_.empty()) {
        selectedRow = 0;
    }
    if (selectedRow >= 0) {
        runTable_->setCurrentCell(selectedRow, 0);
        showRun(selectedRow);
    } else if (runSummary_) {
        runSummary_->setText(QStringLiteral("尚未发现可回放的运行包。请先完成一次在线运行，或指定包含 run_timeline_index.json 的 evidence。"));
    }
}

QString ReplayPage::runDirForRow(const int runRow) const
{
    if (runRow >= 0 && runRow < static_cast<int>(runEntries_.size())) {
        return runEntries_[static_cast<std::size_t>(runRow)].runDir;
    }
    return fallbackEvidenceRoot_;
}

void ReplayPage::showRun(const int runRow)
{
    const QString runDir = runDirForRow(runRow);
    const ReplayRunEntry entry = (runRow >= 0 && runRow < static_cast<int>(runEntries_.size()))
        ? runEntries_[static_cast<std::size_t>(runRow)]
        : ReplayRunEntry{QStringLiteral("当前"), QStringLiteral("当前 evidence"), QStringLiteral("-"), QStringLiteral("-"), QString(), runDir};

    if (runTitle_) {
        runTitle_->setText(QStringLiteral("%1  ·  %2").arg(entry.source, entry.runId));
    }
    if (runSummary_) {
        runSummary_->setText(QFileInfo::exists(runDir)
            ? QStringLiteral("backend：%1\n%2\n路径：%3").arg(entry.backend, entry.summary, shortPath(runDir))
            : QStringLiteral("运行包不存在或尚未落盘：%1").arg(shortPath(runDir)));
    }

    currentBranchId_.clear();
    currentLoopIterationIndex_ = -1;
    if (replayHub_ && !runDir.isEmpty()) {
        replayHub_->setEvidenceRoot(runDir);
    }
}

void ReplayPage::onTimeline(const QJsonObject& timeline)
{
    latestTimeline_ = timeline;
    applyBranchSnapshot(timeline);
    if (runSummary_) {
        runSummary_->setText(QStringLiteral("run=%1 / object=%2 / 分支=%3 / 时间点=%4 / 可显示场=%5\n路径：%6")
            .arg(timeline.value(QStringLiteral("run_id")).toString(QStringLiteral("-")),
                 timeline.value(QStringLiteral("object_id")).toString(QStringLiteral("-")))
            .arg(timeline.value(QStringLiteral("branch_descriptors")).toArray(
                     timeline.value(QStringLiteral("branches")).toArray()).size())
            .arg(timelinePoints(timeline).size())
            .arg(timeline.value(QStringLiteral("field_artifact_options")).toArray().size())
            .arg(shortPath(runDirForRow(runTable_->currentRow()))));
    }
}

void ReplayPage::applyBranchSnapshot(const QJsonObject& timeline)
{
    if (currentBranchId_.isEmpty()) {
        currentBranchId_ = primaryBranchId(timeline);
    }
    if (currentLoopIterationIndex_ < 0) {
        currentLoopIterationIndex_ = latestLoopForBranch(timeline, currentBranchId_);
    }

    branchTree_->setSnapshot(timeline);
    branchTree_->setCurrentBranch(currentBranchId_);
    branchTimeline_->setSnapshot(timeline);
    branchTimeline_->setCurrentTimelinePoint(currentBranchId_, currentLoopIterationIndex_);
    branchState_->setSnapshot(timeline);
    branchState_->setCurrentTimelinePoint(currentBranchId_, currentLoopIterationIndex_);
    branchField_->setSnapshot(timeline);
    branchField_->setCurrentTimelinePoint(currentBranchId_, currentLoopIterationIndex_);
    branchSeries_->setSnapshot(timeline);
    branchSeries_->setCurrentBranch(currentBranchId_);
}

void ReplayPage::selectBranch(const QString& branchId)
{
    if (branchId.isEmpty()) {
        return;
    }
    currentBranchId_ = branchId;
    currentLoopIterationIndex_ = latestLoopForBranch(latestTimeline_, currentBranchId_);
    branchTimeline_->setCurrentBranch(currentBranchId_);
    branchTimeline_->setCurrentTimelinePoint(currentBranchId_, currentLoopIterationIndex_);
    branchState_->setCurrentTimelinePoint(currentBranchId_, currentLoopIterationIndex_);
    branchField_->setCurrentTimelinePoint(currentBranchId_, currentLoopIterationIndex_);
    branchSeries_->setCurrentBranch(currentBranchId_);
}

void ReplayPage::selectTimelinePoint(const QString& branchId, const int loopIterationIndex)
{
    currentBranchId_ = branchId;
    currentLoopIterationIndex_ = loopIterationIndex;
    branchTree_->setCurrentBranch(currentBranchId_);
    branchState_->setCurrentTimelinePoint(currentBranchId_, currentLoopIterationIndex_);
    branchField_->setCurrentTimelinePoint(currentBranchId_, currentLoopIterationIndex_);
    branchSeries_->setCurrentBranch(currentBranchId_);
}

} // namespace twin
