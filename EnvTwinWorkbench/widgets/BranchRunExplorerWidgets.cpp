#include "BranchRunExplorerWidgets.h"

#include "../datahub/AsyncFieldArtifactLoader.h"
#include "../datahub/FieldRenderGuard.h"
#include "GraphWorkflowDisplayWidgets.h"
#include "VtkModelFieldWidget.h"

#include <QAbstractItemView>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QHeaderView>
#include <QJsonDocument>
#include <QLabel>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSplitter>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace twin {
namespace {

constexpr int kBranchRole = Qt::UserRole + 1;
constexpr int kLoopRole = Qt::UserRole + 2;
constexpr int kOptionRole = Qt::UserRole + 3;

QString compactJson(const QJsonValue& value)
{
    if (value.isObject()) {
        return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
    }
    if (value.isArray()) {
        return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
    }
    return {};
}

QString textOf(const QJsonValue& value, const QString& fallback = QStringLiteral("-"))
{
    if (value.isUndefined() || value.isNull()) {
        return fallback;
    }
    if (value.isString()) {
        const QString text = value.toString();
        return text.isEmpty() ? fallback : text;
    }
    if (value.isBool()) {
        return value.toBool() ? QStringLiteral("是") : QStringLiteral("否");
    }
    if (value.isDouble()) {
        const double number = value.toDouble();
        return std::isfinite(number) ? QString::number(number, 'g', 8) : fallback;
    }
    const QString json = compactJson(value);
    return json.isEmpty() ? fallback : json;
}

QString stringValue(const QJsonObject& obj, const QString& key, const QString& fallback = {})
{
    const QString value = obj.value(key).toString();
    return value.isEmpty() ? fallback : value;
}

int intValue(const QJsonObject& obj, const QString& key, const int fallback = -1)
{
    return obj.value(key).isDouble() ? obj.value(key).toInt(fallback) : fallback;
}

double doubleValue(const QJsonObject& obj, const QString& key,
                   const double fallback = std::numeric_limits<double>::quiet_NaN())
{
    const QJsonValue value = obj.value(key);
    if (!value.isDouble()) {
        return fallback;
    }
    const double number = value.toDouble();
    return std::isfinite(number) ? number : fallback;
}

QString shortPath(QString path)
{
    path.replace(QLatin1Char('/'), QLatin1Char('\\'));
    if (path.size() <= 86) {
        return path;
    }
    return path.left(38) + QStringLiteral(" ... ") + path.right(40);
}

QString normalizeEvidencePath(QString path)
{
    path = path.trimmed();
    if (path.startsWith(QStringLiteral("file://"))) {
        path = path.mid(QStringLiteral("file://").size());
    }
    if (path.startsWith(QStringLiteral("//?/")) || path.startsWith(QStringLiteral("\\\\?\\"))) {
        path = path.mid(4);
    }
    return QDir::fromNativeSeparators(path);
}

QString fieldLabel(const QJsonObject& option)
{
    const QString explicitLabel = stringValue(option, QStringLiteral("display_label"),
        stringValue(option, QStringLiteral("label"),
        stringValue(option, QStringLiteral("field_label"))));
    if (!explicitLabel.isEmpty()) {
        return explicitLabel;
    }
    const QString fieldName = stringValue(option, QStringLiteral("field_name"));
    return fieldName.isEmpty() ? QStringLiteral("-") : fieldName;
}

QString renderTitleForField(const QJsonObject& option)
{
    const QString fieldName = stringValue(option, QStringLiteral("field_name"));
    const QString nodeId = stringValue(option, QStringLiteral("node_id"));
    const int step = intValue(option, QStringLiteral("loop_iteration_index"),
                              intValue(option, QStringLiteral("step_index")));
    return QStringLiteral("%1 · %2 · step %3")
        .arg(fieldLabel(option),
             nodeId.isEmpty() ? QStringLiteral("unknown_operator") : nodeId)
        .arg(step);
}

QString artifactPathOf(const QJsonObject& option)
{
    return normalizeEvidencePath(stringValue(option, QStringLiteral("artifact_path"),
                                             stringValue(option, QStringLiteral("artifact_uri"))));
}

QString runtimeSnapshotPathOf(const QJsonObject& snapshot, const QJsonObject& option)
{
    const QString explicitPath = normalizeEvidencePath(
        stringValue(option, QStringLiteral("runtime_snapshot_path"),
                    stringValue(snapshot, QStringLiteral("runtime_snapshot_path"))));
    if (!explicitPath.isEmpty()) {
        return explicitPath;
    }
    return {};
}

bool isRenderableFieldOption(const QJsonObject& option)
{
    if (stringValue(option, QStringLiteral("representation")) != QStringLiteral("artifact_ref")) {
        return false;
    }
    if (stringValue(option, QStringLiteral("field_name")).isEmpty()) {
        return false;
    }
    if (intValue(option, QStringLiteral("node_count"), 0) <= 0) {
        return false;
    }
    const QString artifactPath = artifactPathOf(option);
    return !artifactPath.isEmpty() && QFileInfo::exists(artifactPath);
}

QString branchIdOf(const QJsonObject& obj)
{
    return stringValue(obj, QStringLiteral("branch_id"), QStringLiteral("main.online"));
}

QString branchKindLabel(const QJsonObject& branch)
{
    const QString label = stringValue(branch, QStringLiteral("kind_label"));
    if (!label.isEmpty()) {
        return label;
    }
    const QString kind = stringValue(branch, QStringLiteral("branch_kind"));
    if (kind == QStringLiteral("online_mainline")) {
        return QStringLiteral("在线主分支");
    }
    if (kind == QStringLiteral("future_prediction")) {
        return QStringLiteral("预测分支");
    }
    return kind.isEmpty() ? QStringLiteral("-") : kind;
}

QJsonArray branchArray(const QJsonObject& snapshot)
{
    QJsonArray branches = snapshot.value(QStringLiteral("branch_descriptors")).toArray();
    if (branches.isEmpty()) {
        branches = snapshot.value(QStringLiteral("branches")).toArray();
    }
    if (!branches.isEmpty()) {
        return branches;
    }

    QJsonObject branch;
    branch.insert(QStringLiteral("branch_id"), stringValue(snapshot, QStringLiteral("primary_branch_id"),
                                                         QStringLiteral("main.online")));
    branch.insert(QStringLiteral("display_name"), QStringLiteral("在线主分支"));
    branch.insert(QStringLiteral("branch_kind"), QStringLiteral("online_mainline"));
    branch.insert(QStringLiteral("status"), stringValue(snapshot, QStringLiteral("status")));
    branch.insert(QStringLiteral("run_id"), stringValue(snapshot, QStringLiteral("run_id")));
    branch.insert(QStringLiteral("run_dir"), stringValue(snapshot, QStringLiteral("run_dir")));
    QJsonArray fallback;
    fallback.push_back(branch);
    return fallback;
}

QString runtimeSnapshotPathForBranch(const QJsonObject& snapshot, const QString& branchId)
{
    for (const QJsonValue& value : branchArray(snapshot)) {
        const QJsonObject branch = value.toObject();
        if (branchIdOf(branch) != branchId) {
            continue;
        }
        const QString path = normalizeEvidencePath(stringValue(branch, QStringLiteral("runtime_snapshot_path")));
        if (!path.isEmpty()) {
            return path;
        }
    }
    const QString topLevel = normalizeEvidencePath(stringValue(snapshot, QStringLiteral("runtime_snapshot_path")));
    if (!topLevel.isEmpty()) {
        return topLevel;
    }
    return {};
}

QJsonArray timelineArray(const QJsonObject& snapshot)
{
    QJsonArray points = snapshot.value(QStringLiteral("timeline_points")).toArray();
    if (!points.isEmpty()) {
        return points;
    }
    points = snapshot.value(QStringLiteral("online_frames")).toArray();
    for (int i = 0; i < points.size(); ++i) {
        QJsonObject point = points.at(i).toObject();
        if (!point.contains(QStringLiteral("branch_id"))) {
            point.insert(QStringLiteral("branch_id"), QStringLiteral("main.online"));
        }
        if (!point.contains(QStringLiteral("point_kind"))) {
            point.insert(QStringLiteral("point_kind"), QStringLiteral("在线融合帧"));
        }
        if (!point.contains(QStringLiteral("loop_iteration_index"))) {
            point.insert(QStringLiteral("loop_iteration_index"),
                         point.value(QStringLiteral("frame_index")).toInt(i));
        }
        points.replace(i, point);
    }
    return points;
}

QJsonObject selectedStateOf(const QJsonObject& point)
{
    const QJsonObject selected = point.value(QStringLiteral("selected_state")).toObject();
    if (!selected.isEmpty()) {
        return selected;
    }
    const QJsonObject state = point.value(QStringLiteral("state")).toObject();
    if (!state.isEmpty()) {
        return state;
    }
    return point.value(QStringLiteral("posterior")).toObject();
}

QJsonObject ballisticStateOf(const QJsonObject& point)
{
    const QJsonObject state = selectedStateOf(point);
    const QJsonObject components = state.value(QStringLiteral("components")).toObject();
    const QJsonObject ballistic = components.value(QStringLiteral("ballistic")).toObject();
    return ballistic.isEmpty() ? state.value(QStringLiteral("ballistic")).toObject() : ballistic;
}

double pointNumber(const QJsonObject& point, const QString& key,
                   const QString& ballisticKey = {},
                   const double fallback = std::numeric_limits<double>::quiet_NaN())
{
    const double direct = doubleValue(point, key, fallback);
    if (std::isfinite(direct)) {
        return direct;
    }
    if (!ballisticKey.isEmpty()) {
        const double nested = doubleValue(ballisticStateOf(point), ballisticKey, fallback);
        if (std::isfinite(nested)) {
            return nested;
        }
    }
    return fallback;
}

double publicTimeOf(const QJsonObject& obj)
{
    const double explicitTime = doubleValue(obj, QStringLiteral("public_time_s"));
    if (std::isfinite(explicitTime)) {
        return explicitTime;
    }
    const double publicOutputTime = doubleValue(obj, QStringLiteral("public_output_time_s"));
    if (std::isfinite(publicOutputTime)) {
        return publicOutputTime;
    }
    const QJsonObject timePoint = obj.value(QStringLiteral("time_point")).toObject();
    const double timePointRunTime = doubleValue(timePoint, QStringLiteral("run_time_s"));
    if (std::isfinite(timePointRunTime)) {
        return timePointRunTime;
    }
    const double runTime = doubleValue(obj, QStringLiteral("run_time_s"));
    if (std::isfinite(runTime)) {
        return runTime;
    }
    return pointNumber(obj, QStringLiteral("sample_time_s"),
                       QStringLiteral("time_s"),
                       doubleValue(obj, QStringLiteral("time")));
}

double observationTimeOf(const QJsonObject& point)
{
    const double sampleTime = pointNumber(point, QStringLiteral("sample_time_s"),
                                         QStringLiteral("time_s"));
    if (std::isfinite(sampleTime)) {
        return sampleTime;
    }
    const double sourceTime = doubleValue(point, QStringLiteral("source_time_s"));
    if (std::isfinite(sourceTime)) {
        return sourceTime;
    }
    return doubleValue(point.value(QStringLiteral("time_point")).toObject(),
                       QStringLiteral("source_time_s"));
}

bool samePublicTime(const double left, const double right)
{
    return std::isfinite(left) && std::isfinite(right) && std::abs(left - right) <= 1.0e-6;
}

QString numberText(const double value, const QString& fallback = QStringLiteral("-"))
{
    return std::isfinite(value) ? QString::number(value, 'g', 8) : fallback;
}

QString timelineKindText(const QJsonObject& point)
{
    const QString label = stringValue(point, QStringLiteral("point_kind"));
    if (!label.isEmpty()) {
        return label;
    }
    const QString id = stringValue(point, QStringLiteral("point_kind_id"));
    if (id == QStringLiteral("prediction_step")) {
        return QStringLiteral("预测步");
    }
    if (id == QStringLiteral("realtime_prediction_frame")) {
        return QStringLiteral("在线后验场帧");
    }
    if (id == QStringLiteral("online_fusion_frame")) {
        return QStringLiteral("在线滤波帧");
    }
    if (id == QStringLiteral("online_runtime_step")) {
        return QStringLiteral("在线运行步");
    }
    return QStringLiteral("-");
}

QJsonObject findTimelinePoint(const QJsonObject& snapshot,
                              const QString& branchId,
                              const int loopIterationIndex)
{
    for (const QJsonValue& value : timelineArray(snapshot)) {
        const QJsonObject point = value.toObject();
        if (!branchId.isEmpty() && branchIdOf(point) != branchId) {
            continue;
        }
        if (loopIterationIndex >= 0 &&
            intValue(point, QStringLiteral("loop_iteration_index")) != loopIterationIndex) {
            continue;
        }
        return point;
    }
    return {};
}

QJsonArray fieldOptionsFor(const QJsonObject& snapshot,
                           const QString& branchId,
                           const int loopIterationIndex)
{
    const QJsonObject targetPoint = findTimelinePoint(snapshot, branchId, loopIterationIndex);
    const double targetPublicTime = publicTimeOf(targetPoint);
    QJsonArray timeMatched;
    QJsonArray loopMatched;
    for (const QJsonValue& value : snapshot.value(QStringLiteral("field_artifact_options")).toArray()) {
        const QJsonObject option = value.toObject();
        if (!branchId.isEmpty() && branchIdOf(option) != branchId) {
            continue;
        }
        if (samePublicTime(publicTimeOf(option), targetPublicTime)) {
            timeMatched.push_back(option);
        }
        if (loopIterationIndex < 0 ||
            intValue(option, QStringLiteral("loop_iteration_index")) == loopIterationIndex) {
            loopMatched.push_back(option);
        }
    }
    return timeMatched.isEmpty() ? loopMatched : timeMatched;
}

QJsonArray qoiOptionsFor(const QJsonObject& snapshot,
                         const QString& branchId,
                         const int loopIterationIndex)
{
    const QJsonObject targetPoint = findTimelinePoint(snapshot, branchId, loopIterationIndex);
    const double targetPublicTime = publicTimeOf(targetPoint);
    QJsonArray timeMatched;
    QJsonArray loopMatched;
    for (const QJsonValue& value : snapshot.value(QStringLiteral("qoi_options")).toArray()) {
        const QJsonObject option = value.toObject();
        if (!branchId.isEmpty() && branchIdOf(option) != branchId) {
            continue;
        }
        if (samePublicTime(publicTimeOf(option), targetPublicTime)) {
            timeMatched.push_back(option);
        }
        if (loopIterationIndex < 0 ||
            intValue(option, QStringLiteral("loop_iteration_index")) == loopIterationIndex) {
            loopMatched.push_back(option);
        }
    }
    return timeMatched.isEmpty() ? loopMatched : timeMatched;
}

QTableWidget* makeTable(const QStringList& headers, QWidget* parent)
{
    auto* table = new QTableWidget(parent);
    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->verticalHeader()->hide();
    table->horizontalHeader()->setStretchLastSection(true);
    table->setAlternatingRowColors(true);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    return table;
}

void setCell(QTableWidget* table, const int row, const int column, const QString& text)
{
    auto* item = new QTableWidgetItem(text);
    item->setToolTip(text);
    table->setItem(row, column, item);
}

void appendKeyValue(QTableWidget* table, const QString& key, const QString& value)
{
    const int row = table->rowCount();
    table->insertRow(row);
    setCell(table, row, 0, key);
    setCell(table, row, 1, value);
}

void showEmpty(QTableWidget* table, const QString& message)
{
    table->setRowCount(0);
    table->insertRow(0);
    setCell(table, 0, 0, QStringLiteral("-"));
    setCell(table, 0, 1, message);
}

QString selectedBranchOrDefault(const QJsonObject& snapshot, const QString& current)
{
    if (!current.isEmpty()) {
        return current;
    }
    const QString primary = stringValue(snapshot, QStringLiteral("primary_branch_id"));
    if (!primary.isEmpty()) {
        return primary;
    }
    const QJsonArray branches = branchArray(snapshot);
    if (!branches.isEmpty()) {
        return branchIdOf(branches.first().toObject());
    }
    return QStringLiteral("main.online");
}

} // namespace

BranchTreeWidget::BranchTreeWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(240);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::MinimumExpanding);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    tree_ = new QTreeWidget(this);
    tree_->setMinimumHeight(220);
    tree_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    tree_->setColumnCount(5);
    tree_->setHeaderLabels({
        QStringLiteral("分支"),
        QStringLiteral("类型"),
        QStringLiteral("触发"),
        QStringLiteral("状态"),
        QStringLiteral("摘要")
    });
    tree_->setRootIsDecorated(true);
    tree_->setAlternatingRowColors(true);
    tree_->setSelectionMode(QAbstractItemView::SingleSelection);
    tree_->header()->setStretchLastSection(true);
    layout->addWidget(tree_);

    connect(tree_, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem* current, QTreeWidgetItem*) {
                if (!current) {
                    return;
                }
                const QString branchId = current->data(0, kBranchRole).toString();
                if (branchId.isEmpty()) {
                    return;
                }
                currentBranchId_ = branchId;
                emit branchSelected(branchId);
            });
}

void BranchTreeWidget::setSnapshot(const QJsonObject& snapshot)
{
    snapshot_ = snapshot;
    currentBranchId_ = selectedBranchOrDefault(snapshot_, currentBranchId_);
    rebuild();
}

void BranchTreeWidget::setCurrentBranch(const QString& branchId)
{
    currentBranchId_ = branchId;
    rebuild();
}

void BranchTreeWidget::rebuild()
{
    const QSignalBlocker blocker(tree_);
    tree_->clear();

    const QJsonArray branches = branchArray(snapshot_);
    QHash<QString, QTreeWidgetItem*> itemsById;
    QList<QPair<QString, QTreeWidgetItem*>> deferred;
    for (const QJsonValue& value : branches) {
        const QJsonObject branch = value.toObject();
        const QString branchId = branchIdOf(branch);
        const QJsonObject summary = branch.value(QStringLiteral("summary")).toObject();
        const QString display = stringValue(branch, QStringLiteral("display_name"), branchId);
        const QString trigger = intValue(branch, QStringLiteral("trigger_frame_index")) >= 0
            ? QStringLiteral("帧 %1 / t=%2s")
                  .arg(intValue(branch, QStringLiteral("trigger_frame_index")))
                  .arg(textOf(branch.value(QStringLiteral("trigger_time_s"))))
            : QStringLiteral("主时间轴");
        const QString summaryText = QStringLiteral("步 %1，场 %2，QoI %3")
            .arg(textOf(summary.value(QStringLiteral("step_count")),
                        textOf(summary.value(QStringLiteral("iteration_count")))))
            .arg(textOf(summary.value(QStringLiteral("field_artifact_count")),
                        textOf(summary.value(QStringLiteral("artifact_count")))))
            .arg(textOf(summary.value(QStringLiteral("qoi_ref_count"))));

        auto* item = new QTreeWidgetItem({
            display,
            branchKindLabel(branch),
            trigger,
            stringValue(branch, QStringLiteral("status"), QStringLiteral("-")),
            summaryText
        });
        item->setData(0, kBranchRole, branchId);
        item->setToolTip(0, branchId);
        item->setToolTip(4, stringValue(branch, QStringLiteral("run_dir")));
        itemsById.insert(branchId, item);
        deferred.push_back(qMakePair(stringValue(branch, QStringLiteral("parent_branch_id")), item));
    }

    for (const auto& pair : deferred) {
        if (!pair.first.isEmpty() && itemsById.contains(pair.first)) {
            itemsById.value(pair.first)->addChild(pair.second);
        } else {
            tree_->addTopLevelItem(pair.second);
        }
    }
    tree_->expandAll();
    for (int column = 0; column < tree_->columnCount(); ++column) {
        tree_->resizeColumnToContents(column);
    }

    if (auto it = itemsById.find(currentBranchId_); it != itemsById.end()) {
        tree_->setCurrentItem(it.value());
    }
}

BranchTimelineWidget::BranchTimelineWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    table_ = makeTable({
        QStringLiteral("类型"),
        QStringLiteral("分支"),
        QStringLiteral("帧"),
        QStringLiteral("step"),
        QStringLiteral("t(s)"),
        QStringLiteral("h(m)"),
        QStringLiteral("Ma"),
        QStringLiteral("RUL(s)"),
        QStringLiteral("状态")
    }, this);
    layout->addWidget(table_);

    connect(table_, &QTableWidget::currentCellChanged, this,
            [this](int row, int, int, int) {
                if (row < 0 || row >= table_->rowCount() || !table_->item(row, 0)) {
                    return;
                }
                const QString branchId = table_->item(row, 0)->data(kBranchRole).toString();
                const int loop = table_->item(row, 0)->data(kLoopRole).toInt();
                if (branchId.isEmpty()) {
                    return;
                }
                currentBranchId_ = branchId;
                currentLoopIterationIndex_ = loop;
                emit timelinePointSelected(branchId, loop);
            });
}

void BranchTimelineWidget::setSnapshot(const QJsonObject& snapshot)
{
    snapshot_ = snapshot;
    currentBranchId_ = selectedBranchOrDefault(snapshot_, currentBranchId_);
    rebuild();
}

void BranchTimelineWidget::setCurrentBranch(const QString& branchId)
{
    currentBranchId_ = branchId;
    currentLoopIterationIndex_ = -1;
    rebuild();
}

void BranchTimelineWidget::setCurrentTimelinePoint(const QString& branchId, const int loopIterationIndex)
{
    const bool branchChanged = (currentBranchId_ != branchId);
    currentBranchId_ = branchId;
    currentLoopIterationIndex_ = loopIterationIndex;
    if (branchChanged) {
        rebuild();
    } else {
        selectCurrentRow();
    }
}

void BranchTimelineWidget::rebuild()
{
    const QSignalBlocker blocker(table_);
    table_->setRowCount(0);

    for (const QJsonValue& value : timelineArray(snapshot_)) {
        const QJsonObject point = value.toObject();
        const QString branchId = branchIdOf(point);
        if (!currentBranchId_.isEmpty() && branchId != currentBranchId_) {
            continue;
        }

        const int row = table_->rowCount();
        table_->insertRow(row);
        const int loop = intValue(point, QStringLiteral("loop_iteration_index"),
                                  intValue(point, QStringLiteral("frame_index")));
        const double time = publicTimeOf(point);
        const double altitude = pointNumber(point, QStringLiteral("altitude_m"), QStringLiteral("h"));
        const double mach = pointNumber(point, QStringLiteral("mach"), QStringLiteral("ma"));
        const double rul = pointNumber(point, QStringLiteral("remaining_life_s"));

        setCell(table_, row, 0, timelineKindText(point));
        table_->item(row, 0)->setData(kBranchRole, branchId);
        table_->item(row, 0)->setData(kLoopRole, loop);
        setCell(table_, row, 1, branchId);
        setCell(table_, row, 2, textOf(point.value(QStringLiteral("frame_index"))));
        setCell(table_, row, 3, textOf(point.value(QStringLiteral("step_index"))));
        setCell(table_, row, 4, numberText(time));
        setCell(table_, row, 5, numberText(altitude));
        setCell(table_, row, 6, numberText(mach));
        setCell(table_, row, 7, numberText(rul));
        setCell(table_, row, 8, stringValue(point, QStringLiteral("status"),
                                            stringValue(point, QStringLiteral("stop_reason"), QStringLiteral("-"))));
    }

    if (table_->rowCount() == 0) {
        table_->insertRow(0);
        setCell(table_, 0, 0, QStringLiteral("-"));
        setCell(table_, 0, 1, QStringLiteral("当前分支暂无 timeline point"));
    } else {
        selectCurrentRow();
    }
    table_->resizeColumnsToContents();
}

void BranchTimelineWidget::selectCurrentRow()
{
    const QSignalBlocker blocker(table_);
    for (int row = 0; row < table_->rowCount(); ++row) {
        QTableWidgetItem* item = table_->item(row, 0);
        if (!item) {
            continue;
        }
        const QString branchId = item->data(kBranchRole).toString();
        const int loop = item->data(kLoopRole).toInt();
        if (branchId == currentBranchId_ &&
            (currentLoopIterationIndex_ < 0 || loop == currentLoopIterationIndex_)) {
            table_->setCurrentCell(row, 0);
            return;
        }
    }
}

BranchStatePanel::BranchStatePanel(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    titleLabel_ = new QLabel(QStringLiteral("未选择分支时刻"), this);
    titleLabel_->setStyleSheet(QStringLiteral("font-weight:600;color:#0f172a;"));
    layout->addWidget(titleLabel_);

    stateTable_ = makeTable({QStringLiteral("状态量"), QStringLiteral("值")}, this);
    stateTable_->setMinimumHeight(190);
    layout->addWidget(stateTable_);

    filterTable_ = makeTable({QStringLiteral("滤波/时钟"), QStringLiteral("值")}, this);
    filterTable_->setMinimumHeight(150);
    layout->addWidget(filterTable_);
}

void BranchStatePanel::setSnapshot(const QJsonObject& snapshot)
{
    snapshot_ = snapshot;
    currentBranchId_ = selectedBranchOrDefault(snapshot_, currentBranchId_);
    rebuild();
}

void BranchStatePanel::setCurrentTimelinePoint(const QString& branchId, const int loopIterationIndex)
{
    currentBranchId_ = branchId;
    currentLoopIterationIndex_ = loopIterationIndex;
    rebuild();
}

void BranchStatePanel::rebuild()
{
    const QJsonObject point = findTimelinePoint(snapshot_, currentBranchId_, currentLoopIterationIndex_);
    if (point.isEmpty()) {
        titleLabel_->setText(QStringLiteral("等待选择分支时刻"));
        showEmpty(stateTable_, QStringLiteral("暂无状态"));
        showEmpty(filterTable_, QStringLiteral("暂无滤波/时钟信息"));
        return;
    }

    const QString branchId = branchIdOf(point);
    const int loop = intValue(point, QStringLiteral("loop_iteration_index"),
                              intValue(point, QStringLiteral("frame_index")));
    titleLabel_->setText(QStringLiteral("%1 · loop %2 · %3")
                             .arg(branchId)
                             .arg(loop)
                             .arg(timelineKindText(point)));

    stateTable_->setRowCount(0);
    appendKeyValue(stateTable_, QStringLiteral("分支"), branchId);
    appendKeyValue(stateTable_, QStringLiteral("时刻类型"), timelineKindText(point));
    appendKeyValue(stateTable_, QStringLiteral("在线帧"), textOf(point.value(QStringLiteral("frame_index"))));
    appendKeyValue(stateTable_, QStringLiteral("预测 step"), textOf(point.value(QStringLiteral("step_index"))));
    appendKeyValue(stateTable_, QStringLiteral("t(s)"),
                   numberText(publicTimeOf(point)));
    appendKeyValue(stateTable_, QStringLiteral("高度 h(m)"),
                   numberText(pointNumber(point, QStringLiteral("altitude_m"), QStringLiteral("h"))));
    appendKeyValue(stateTable_, QStringLiteral("Ma"),
                   numberText(pointNumber(point, QStringLiteral("mach"), QStringLiteral("ma"))));
    appendKeyValue(stateTable_, QStringLiteral("攻角 alpha"),
                   numberText(pointNumber(point, QStringLiteral("alpha"), QStringLiteral("alpha"))));
    appendKeyValue(stateTable_, QStringLiteral("侧滑 beta"),
                   numberText(pointNumber(point, QStringLiteral("beta"), QStringLiteral("beta"))));
    appendKeyValue(stateTable_, QStringLiteral("剩余寿命 RUL(s)"),
                   numberText(pointNumber(point, QStringLiteral("remaining_life_s"))));
    appendKeyValue(stateTable_, QStringLiteral("状态"), stringValue(point, QStringLiteral("status"),
                                                                  stringValue(point, QStringLiteral("stop_reason"))));
    stateTable_->resizeColumnsToContents();

    const QJsonObject filter = point.value(QStringLiteral("filter")).toObject();
    const QJsonObject timePoint = point.value(QStringLiteral("time_point")).toObject();
    const QJsonObject timeSummary = point.value(QStringLiteral("time_summary")).toObject();
    filterTable_->setRowCount(0);
    appendKeyValue(filterTable_, QStringLiteral("传感器数"),
                   textOf(point.value(QStringLiteral("sensor_count"))));
    appendKeyValue(filterTable_, QStringLiteral("ESS"),
                   textOf(filter.value(QStringLiteral("effective_sample_size"))));
    appendKeyValue(filterTable_, QStringLiteral("归一化 ESS"),
                   textOf(filter.value(QStringLiteral("normalized_effective_sample_size"))));
    appendKeyValue(filterTable_, QStringLiteral("粒子数"),
                   textOf(filter.value(QStringLiteral("particle_count"))));
    appendKeyValue(filterTable_, QStringLiteral("残差范数"),
                   textOf(filter.value(QStringLiteral("observation_residual_norm"))));
    appendKeyValue(filterTable_, QStringLiteral("重采样"),
                   textOf(filter.value(QStringLiteral("resampled"))));
    appendKeyValue(filterTable_, QStringLiteral("tick"),
                   textOf(timePoint.value(QStringLiteral("tick_index"))));
    appendKeyValue(filterTable_, QStringLiteral("online_observation_time_s"),
                   numberText(observationTimeOf(point)));
    appendKeyValue(filterTable_, QStringLiteral("public_time_s"),
                   numberText(publicTimeOf(point)));
    appendKeyValue(filterTable_, QStringLiteral("run_time_s"),
                   textOf(timePoint.value(QStringLiteral("run_time_s"))));
    appendKeyValue(filterTable_, QStringLiteral("source_time_s"),
                   textOf(timePoint.value(QStringLiteral("source_time_s"))));
    appendKeyValue(filterTable_, QStringLiteral("effective_delta_t_s"),
                   textOf(point.value(QStringLiteral("effective_delta_t_s"))));
    appendKeyValue(filterTable_, QStringLiteral("output_period_s"),
                   textOf(point.value(QStringLiteral("output_period_s"))));
    appendKeyValue(filterTable_, QStringLiteral("operator_internal_summary"),
                   textOf(QJsonValue(timeSummary)));
    filterTable_->resizeColumnsToContents();
}

BranchFieldPanel::BranchFieldPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    titleLabel_ = new QLabel(QStringLiteral("场与 QoI 输出"), this);
    titleLabel_->setStyleSheet(QStringLiteral("font-weight:600;color:#0f172a;"));
    layout->addWidget(titleLabel_);

    renderStatusLabel_ = new QLabel(QStringLiteral("选择一个完整场 artifact 后加载真实网格云图"), this);
    renderStatusLabel_->setWordWrap(true);
    renderStatusLabel_->setStyleSheet(QStringLiteral(
        "background:#fff7ed;border:1px solid #fed7aa;border-radius:4px;padding:6px;color:#9a3412;"));
    layout->addWidget(renderStatusLabel_);

    auto* splitter = new QSplitter(Qt::Vertical, this);
    splitter->setChildrenCollapsible(false);
    splitter->setOpaqueResize(false);
    layout->addWidget(splitter, 1);

    fieldTable_ = makeTable({
        QStringLiteral("场"),
        QStringLiteral("角色"),
        QStringLiteral("部件"),
        QStringLiteral("节点"),
        QStringLiteral("step"),
        QStringLiteral("单位"),
        QStringLiteral("算子/端口"),
        QStringLiteral("artifact")
    }, this);
    fieldTable_->setMinimumHeight(120);
    splitter->addWidget(fieldTable_);

    vtkWidget_ = new flightenv::ui::demo::VtkModelFieldWidget(this);
    vtkWidget_->setMinimumHeight(320);
    splitter->addWidget(vtkWidget_);

    qoiTable_ = makeTable({
        QStringLiteral("QoI"),
        QStringLiteral("表示"),
        QStringLiteral("step"),
        QStringLiteral("算子/端口"),
        QStringLiteral("统计/引用")
    }, this);
    qoiTable_->setMinimumHeight(100);
    splitter->addWidget(qoiTable_);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 5);
    splitter->setStretchFactor(2, 1);
    splitter->setSizes({130, 440, 110});

    connect(fieldTable_, &QTableWidget::currentCellChanged, this,
            [this](int row, int, int, int) {
                if (row < 0 || row >= fieldTable_->rowCount() || !fieldTable_->item(row, 0)) {
                    return;
                }
                const QString optionId = fieldTable_->item(row, 0)->data(kOptionRole).toString();
                const QString branchId = fieldTable_->item(row, 0)->data(kBranchRole).toString();
                const int loop = fieldTable_->item(row, 0)->data(kLoopRole).toInt();
                if (!optionId.isEmpty()) {
                    emit fieldOptionSelected(optionId, branchId, loop);
                }
                renderSelectedField();
            });
}

void BranchFieldPanel::setSnapshot(const QJsonObject& snapshot)
{
    snapshot_ = snapshot;
    currentBranchId_ = selectedBranchOrDefault(snapshot_, currentBranchId_);
    rebuild();
}

void BranchFieldPanel::setCurrentTimelinePoint(const QString& branchId, const int loopIterationIndex)
{
    currentBranchId_ = branchId;
    currentLoopIterationIndex_ = loopIterationIndex;
    rebuild();
}

void BranchFieldPanel::setAssetRoot(const QString& assetRoot)
{
    assetRoot_ = assetRoot;
    if (vtkWidget_) {
        vtkWidget_->setAssetRoot(assetRoot_);
    }
}

void BranchFieldPanel::rebuild()
{
    const QJsonArray fields = fieldOptionsFor(snapshot_, currentBranchId_, currentLoopIterationIndex_);
    const QJsonArray qois = qoiOptionsFor(snapshot_, currentBranchId_, currentLoopIterationIndex_);
    titleLabel_->setText(QStringLiteral("%1 · loop %2 · 场 %3 / QoI %4")
                             .arg(currentBranchId_.isEmpty() ? QStringLiteral("全部分支") : currentBranchId_)
                             .arg(currentLoopIterationIndex_)
                             .arg(fields.size())
                             .arg(qois.size()));

    const QSignalBlocker fieldBlocker(fieldTable_);
    fieldTable_->setRowCount(0);
    currentFieldOptions_.clear();
    int firstRenderableRow = -1;
    for (const QJsonValue& value : fields) {
        const QJsonObject option = value.toObject();
        const int row = fieldTable_->rowCount();
        fieldTable_->insertRow(row);
        currentFieldOptions_.push_back(option);
        if (firstRenderableRow < 0 && isRenderableFieldOption(option)) {
            firstRenderableRow = row;
        }
        const QString optionId = stringValue(option, QStringLiteral("option_id"),
                                             stringValue(option, QStringLiteral("ref")));
        setCell(fieldTable_, row, 0, fieldLabel(option));
        fieldTable_->item(row, 0)->setData(kOptionRole, optionId);
        fieldTable_->item(row, 0)->setData(kBranchRole, branchIdOf(option));
        fieldTable_->item(row, 0)->setData(
            kLoopRole, intValue(option, QStringLiteral("loop_iteration_index")));
        setCell(fieldTable_, row, 1, stringValue(option, QStringLiteral("field_role"), QStringLiteral("-")));
        setCell(fieldTable_, row, 2, stringValue(option, QStringLiteral("component_id"), QStringLiteral("-")));
        setCell(fieldTable_, row, 3, textOf(option.value(QStringLiteral("node_count"))));
        setCell(fieldTable_, row, 4, textOf(option.value(QStringLiteral("step_index"))));
        setCell(fieldTable_, row, 5, stringValue(option, QStringLiteral("unit"), QStringLiteral("-")));
        setCell(fieldTable_, row, 6,
                stringValue(option, QStringLiteral("operator_id")) + QStringLiteral(" / ") +
                    stringValue(option, QStringLiteral("port_id")));
        setCell(fieldTable_, row, 7,
                shortPath(stringValue(option, QStringLiteral("artifact_path"),
                                      stringValue(option, QStringLiteral("artifact_uri"),
                                                  stringValue(option, QStringLiteral("ref"))))));
    }
    if (fieldTable_->rowCount() == 0) {
        fieldTable_->insertRow(0);
        setCell(fieldTable_, 0, 0, QStringLiteral("-"));
        setCell(fieldTable_, 0, 1, QStringLiteral("当前时刻暂无可显示场 artifact"));
        if (renderStatusLabel_) {
            renderStatusLabel_->setText(QStringLiteral("当前分支/时刻没有完整场 artifact_ref"));
        }
        renderGeometryPreview();
    } else if (firstRenderableRow >= 0) {
        fieldTable_->setCurrentCell(firstRenderableRow, 0);
    } else {
        fieldTable_->setCurrentCell(0, 0);
        if (renderStatusLabel_) {
            renderStatusLabel_->setText(QStringLiteral("当前时刻只有系数或 inline 输出，未找到可映射到真实网格的完整场 artifact_ref"));
        }
        renderGeometryPreview();
    }
    fieldTable_->resizeColumnsToContents();

    qoiTable_->setRowCount(0);
    for (const QJsonValue& value : qois) {
        const QJsonObject option = value.toObject();
        const int row = qoiTable_->rowCount();
        qoiTable_->insertRow(row);
        setCell(qoiTable_, row, 0, stringValue(option, QStringLiteral("qoi_name"),
                                               stringValue(option, QStringLiteral("display_name"),
                                                           stringValue(option, QStringLiteral("port_id")))));
        setCell(qoiTable_, row, 1, stringValue(option, QStringLiteral("representation"), QStringLiteral("-")));
        setCell(qoiTable_, row, 2, textOf(option.value(QStringLiteral("step_index"))));
        setCell(qoiTable_, row, 3,
                stringValue(option, QStringLiteral("operator_id")) + QStringLiteral(" / ") +
                    stringValue(option, QStringLiteral("port_id")));
        setCell(qoiTable_, row, 4,
                textOf(option.value(QStringLiteral("statistics")),
                       shortPath(stringValue(option, QStringLiteral("ref")))));
    }
    if (qoiTable_->rowCount() == 0) {
        qoiTable_->insertRow(0);
        setCell(qoiTable_, 0, 0, QStringLiteral("-"));
        setCell(qoiTable_, 0, 1, QStringLiteral("当前时刻暂无 QoI 输出"));
    }
    qoiTable_->resizeColumnsToContents();

    if (firstRenderableRow >= 0) {
        renderSelectedField();
    }
}

void BranchFieldPanel::renderSelectedField()
{
    if (!fieldTable_ || !vtkWidget_) {
        return;
    }
    const int row = fieldTable_->currentRow();
    if (row < 0 || row >= currentFieldOptions_.size()) {
        return;
    }

    const QJsonObject option = currentFieldOptions_.at(row);
    if (!isRenderableFieldOption(option)) {
        const QString representation = stringValue(option, QStringLiteral("representation"), QStringLiteral("-"));
        if (renderStatusLabel_) {
            renderStatusLabel_->setText(QStringLiteral("当前选择不是完整场：representation=%1，node_count=%2")
                .arg(representation)
                .arg(textOf(option.value(QStringLiteral("node_count")))));
        }
        vtkWidget_->clearField(QStringLiteral("请选择完整场 artifact_ref；POD 系数和 inline 状态不直接渲染三维云图"));
        return;
    }

    const QString artifactPath = artifactPathOf(option);
    const QString runtimeSnapshotPath = runtimeSnapshotPathOf(snapshot_, option);
    if (!runtimeSnapshotPath.isEmpty() && !QFileInfo::exists(runtimeSnapshotPath)) {
        if (renderStatusLabel_) {
            renderStatusLabel_->setText(QStringLiteral("兼容快照不存在，将尝试对象包默认 mesh layout"));
        }
    }
    if (assetRoot_.isEmpty()) {
        vtkWidget_->clearField(QStringLiteral("未设置对象包根，无法解析 mesh layout catalog"));
        return;
    }

    const QString fieldKey = QStringLiteral("%1|%2|%3")
        .arg(artifactPath, runtimeSnapshotPath)
        .arg(stringValue(option, QStringLiteral("contract_id"), stringValue(option, QStringLiteral("field_name"))));
    if (fieldKey == lastRenderedFieldKey_) {
        return;
    }
    if (fieldLoadInFlight_) {
        if (fieldKey != inFlightFieldKey_) {
            fieldLoadPending_ = true;
        }
        if (renderStatusLabel_) {
            renderStatusLabel_->setText(QStringLiteral("完整场后台加载中，界面保持可操作..."));
        }
        return;
    }

    fieldLoadInFlight_ = true;
    fieldLoadPending_ = false;
    inFlightFieldKey_ = fieldKey;
    const quint64 serial = ++fieldLoadSerial_;
    if (renderStatusLabel_) {
        renderStatusLabel_->setText(QStringLiteral("后台加载完整场：%1").arg(shortPath(artifactPath)));
    }

    loadFieldArtifactAsync(
        this,
        artifactPath,
        assetRoot_,
        runtimeSnapshotPath,
        fieldRenderHintFromJson(option),
        renderTitleForField(option),
        [this, serial](LoadedFieldArtifact loaded) {
            applyLoadedField(serial, std::move(loaded));
        });
}

void BranchFieldPanel::renderGeometryPreview()
{
    if (!vtkWidget_) {
        return;
    }
    const QString snapshotPath = runtimeSnapshotPathForBranch(snapshot_, currentBranchId_);
    if (assetRoot_.isEmpty()) {
        const QString message = QStringLiteral("未设置对象包根，无法解析 mesh layout catalog");
        if (renderStatusLabel_) {
            renderStatusLabel_->setText(message);
        }
        vtkWidget_->clearField(message);
        showingGeometryPreview_ = false;
        return;
    }
    if (!snapshotPath.isEmpty() && !QFileInfo::exists(snapshotPath)) {
        const QString message = snapshotPath.isEmpty()
            ? QStringLiteral("当前 evidence 没有兼容快照，将尝试对象包默认 mesh layout")
            : QStringLiteral("兼容 runtime_snapshot.json 不存在：%1").arg(shortPath(snapshotPath));
        if (renderStatusLabel_) {
            renderStatusLabel_->setText(message);
        }
    }
    if (snapshotPath == lastPreviewSnapshotKey_ && showingGeometryPreview_) {
        return;
    }
    if (previewLoadInFlight_) {
        if (renderStatusLabel_) {
            renderStatusLabel_->setText(QStringLiteral("真实网格后台加载中，界面仍可拖拽操作..."));
        }
        return;
    }

    previewLoadInFlight_ = true;
    inFlightPreviewSnapshotKey_ = snapshotPath;
    const quint64 serial = ++previewLoadSerial_;
    if (renderStatusLabel_) {
        renderStatusLabel_->setText(QStringLiteral("后台加载对象包真实网格布局：%1")
                                        .arg(snapshotPath.isEmpty() ? shortPath(assetRoot_) : shortPath(snapshotPath)));
    }
    loadMeshLayoutCatalogAsync(
        this,
        assetRoot_,
        snapshotPath,
        [this, serial](LoadedMeshLayoutCatalog loaded) {
            applyLoadedGeometryPreview(serial, std::move(loaded));
        });
}

void BranchFieldPanel::applyLoadedGeometryPreview(const quint64 serial, LoadedMeshLayoutCatalog loaded)
{
    if (serial != previewLoadSerial_) {
        return;
    }
    previewLoadInFlight_ = false;

    if (!loaded.ok) {
        const QString message = loaded.message.isEmpty()
            ? QStringLiteral("mesh layout catalog 读取失败，无法显示真实网格")
            : loaded.message;
        if (renderStatusLabel_) {
            renderStatusLabel_->setText(message);
        }
        if (vtkWidget_) {
            vtkWidget_->clearField(message);
        }
        showingGeometryPreview_ = false;
        return;
    }

    vtkWidget_->setMeshLayoutCatalog(loaded.catalog);
    flightenv::ui::demo::VtkFieldRenderStats stats;
    for (const PlatformMeshLayoutView& layout : loaded.catalog.layouts) {
        stats = vtkWidget_->renderGeometryPreview(layout.layoutId, QStringLiteral("真实对象网格预览"));
        if (stats.ok) {
            break;
        }
    }

    if (!stats.ok) {
        const QString message = stats.message.isEmpty()
            ? QStringLiteral("对象包 mesh layout catalog 中没有可渲染布局")
            : stats.message;
        if (renderStatusLabel_) {
            renderStatusLabel_->setText(message);
        }
        return;
    }

    lastPreviewSnapshotKey_ = inFlightPreviewSnapshotKey_;
    showingGeometryPreview_ = true;
    if (renderStatusLabel_) {
        renderStatusLabel_->setText(QStringLiteral("已显示真实网格预览：节点 %1 / 表面顶点 %2。选择预测分支的完整场 artifact 后会自动上色。")
                                        .arg(stats.nodeCount)
                                        .arg(stats.vertexCount));
    }
}

void BranchFieldPanel::applyLoadedField(const quint64 serial, LoadedFieldArtifact loaded)
{
    if (serial != fieldLoadSerial_) {
        return;
    }
    fieldLoadInFlight_ = false;

    if (!loaded.ok) {
        if (renderStatusLabel_) {
            renderStatusLabel_->setText(loaded.message.isEmpty()
                ? QStringLiteral("完整场 artifact 加载失败")
                : loaded.message);
        }
        if (vtkWidget_) {
            vtkWidget_->clearField(loaded.message.isEmpty()
                ? QStringLiteral("完整场 artifact 加载失败")
                : loaded.message);
        }
        showingGeometryPreview_ = false;
        if (fieldLoadPending_) {
            fieldLoadPending_ = false;
            renderSelectedField();
        }
        return;
    }

    vtkWidget_->setMeshLayoutCatalog(loaded.meshCatalog);
    const auto stats = vtkWidget_->renderFlattenedValues(
        loaded.values,
        loaded.layoutId,
        1,
        0,
        QStringLiteral("%1 · %2").arg(loaded.fieldName, loaded.title),
        loaded.unit);
    if (!stats.ok) {
        if (renderStatusLabel_) {
            renderStatusLabel_->setText(stats.message.isEmpty()
                ? QStringLiteral("完整场渲染失败")
                : stats.message);
        }
    } else {
        lastRenderedFieldKey_ = inFlightFieldKey_;
        showingGeometryPreview_ = false;
        if (renderStatusLabel_) {
            renderStatusLabel_->setText(QStringLiteral("%1 · 节点 %2 · min %3 / max %4 / mean %5")
                .arg(loaded.fieldName)
                .arg(stats.nodeCount)
                .arg(stats.minValue, 0, 'g', 5)
                .arg(stats.maxValue, 0, 'g', 5)
                .arg(stats.meanValue, 0, 'g', 5));
        }
    }

    if (fieldLoadPending_) {
        fieldLoadPending_ = false;
        renderSelectedField();
    }
}

BranchSeriesPanel::BranchSeriesPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    titleLabel_ = new QLabel(QStringLiteral("分支参数历程"), this);
    titleLabel_->setStyleSheet(QStringLiteral("font-weight:600;color:#0f172a;"));
    layout->addWidget(titleLabel_);

    altitudeTrend_ = new flightenv::ui::display::ScalarTrendWidget(this);
    altitudeTrend_->setTitle(QStringLiteral("高度历程"), QStringLiteral("m"));
    layout->addWidget(altitudeTrend_);

    machTrend_ = new flightenv::ui::display::ScalarTrendWidget(this);
    machTrend_->setTitle(QStringLiteral("马赫数历程"));
    layout->addWidget(machTrend_);

    remainingLifeTrend_ = new flightenv::ui::display::ScalarTrendWidget(this);
    remainingLifeTrend_->setTitle(QStringLiteral("剩余寿命历程"), QStringLiteral("s"));
    layout->addWidget(remainingLifeTrend_);

    seriesTable_ = makeTable({
        QStringLiteral("序号"),
        QStringLiteral("t(s)"),
        QStringLiteral("h(m)"),
        QStringLiteral("Ma"),
        QStringLiteral("RUL(s)"),
        QStringLiteral("状态")
    }, this);
    seriesTable_->setMinimumHeight(170);
    layout->addWidget(seriesTable_);
}

void BranchSeriesPanel::setSnapshot(const QJsonObject& snapshot)
{
    snapshot_ = snapshot;
    currentBranchId_ = selectedBranchOrDefault(snapshot_, currentBranchId_);
    rebuild();
}

void BranchSeriesPanel::setCurrentBranch(const QString& branchId)
{
    currentBranchId_ = branchId;
    rebuild();
}

void BranchSeriesPanel::rebuild()
{
    const QString branchId = selectedBranchOrDefault(snapshot_, currentBranchId_);
    std::vector<double> altitudes;
    std::vector<double> machs;
    std::vector<double> ruls;
    seriesTable_->setRowCount(0);

    int sampleIndex = 0;
    for (const QJsonValue& value : timelineArray(snapshot_)) {
        const QJsonObject point = value.toObject();
        if (branchIdOf(point) != branchId) {
            continue;
        }

        const double time = publicTimeOf(point);
        const double altitude = pointNumber(point, QStringLiteral("altitude_m"), QStringLiteral("h"));
        const double mach = pointNumber(point, QStringLiteral("mach"), QStringLiteral("ma"));
        const double rul = pointNumber(point, QStringLiteral("remaining_life_s"));
        if (std::isfinite(altitude)) {
            altitudes.push_back(altitude);
        }
        if (std::isfinite(mach)) {
            machs.push_back(mach);
        }
        if (std::isfinite(rul)) {
            ruls.push_back(rul);
        }

        const int row = seriesTable_->rowCount();
        seriesTable_->insertRow(row);
        setCell(seriesTable_, row, 0, QString::number(sampleIndex++));
        setCell(seriesTable_, row, 1, numberText(time));
        setCell(seriesTable_, row, 2, numberText(altitude));
        setCell(seriesTable_, row, 3, numberText(mach));
        setCell(seriesTable_, row, 4, numberText(rul));
        setCell(seriesTable_, row, 5,
                stringValue(point, QStringLiteral("status"),
                            stringValue(point, QStringLiteral("stop_reason"), QStringLiteral("-"))));
    }

    titleLabel_->setText(QStringLiteral("%1 · 样本 %2").arg(branchId).arg(sampleIndex));
    altitudeTrend_->setSamples(altitudes);
    machTrend_->setSamples(machs);
    remainingLifeTrend_->setSamples(ruls);

    if (seriesTable_->rowCount() == 0) {
        seriesTable_->insertRow(0);
        setCell(seriesTable_, 0, 0, QStringLiteral("-"));
        setCell(seriesTable_, 0, 1, QStringLiteral("当前分支暂无参数历程"));
        altitudeTrend_->clear();
        machTrend_->clear();
        remainingLifeTrend_->clear();
    }
    seriesTable_->resizeColumnsToContents();
}

} // namespace twin
