#include "DataPlanePage.h"

#include "../datahub/AsyncFieldArtifactLoader.h"
#include "../datahub/FieldRenderGuard.h"
#include "../widgets/KvList.h"
#include "../widgets/Panel.h"
#include "../widgets/PageHeader.h"
#include "../widgets/StatusUtil.h"
#include "../widgets/VtkModelFieldWidget.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileInfoList>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QSignalBlocker>
#include <QSet>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <utility>

namespace twin {

using flightenv::ui::demo::VtkModelFieldWidget;

namespace {

QJsonObject readJsonObject(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QByteArray payload = file.readAll();
    QJsonParseError err{};
    QJsonDocument doc = QJsonDocument::fromJson(payload, &err);
    if (err.error != QJsonParseError::NoError) {
        payload.replace("-Infinity", "null");
        payload.replace("Infinity", "null");
        payload.replace("NaN", "null");
        doc = QJsonDocument::fromJson(payload);
    }
    return doc.object();
}

QString pathFromJson(const QJsonValue& value) {
    return QDir::fromNativeSeparators(value.toString());
}

QString normalizeArtifactPath(QString path) {
    path = QDir::fromNativeSeparators(std::move(path));
    if (path.startsWith(QStringLiteral("file://"))) {
        path = path.mid(QStringLiteral("file://").size());
    }
    if (path.startsWith(QStringLiteral("//?/"))) {
        path = path.mid(4);
    }
    if (path.startsWith(QStringLiteral("\\\\?\\"))) {
        path = path.mid(4);
    }
    return path;
}

QStringList runtimeRunDirs(const QString& evidenceRoot) {
    QStringList out;
    QSet<QString> seen;
    auto appendUnique = [&](const QString& dir) {
        const QString clean = QDir::fromNativeSeparators(dir);
        if (clean.isEmpty() || seen.contains(clean.toLower())) {
            return;
        }
        seen.insert(clean.toLower());
        out << clean;
    };
    if (QFileInfo::exists(QDir(evidenceRoot).filePath(QStringLiteral("run_timeline_index.json")))) {
        appendUnique(evidenceRoot);
    }
    const QJsonObject branchRegistry =
        readJsonObject(QDir(evidenceRoot).filePath(QStringLiteral("branch_registry.json")));
    for (const QJsonValue& value : branchRegistry.value(QStringLiteral("branches")).toArray()) {
        const QString runDir = pathFromJson(value.toObject().value(QStringLiteral("run_dir")));
        if (QFileInfo::exists(QDir(runDir).filePath(QStringLiteral("data_plane_manifest.json"))) ||
            QFileInfo::exists(QDir(runDir).filePath(QStringLiteral("run_timeline_index.json")))) {
            appendUnique(runDir);
        }
    }

    const QJsonObject mainline =
        readJsonObject(QDir(evidenceRoot).filePath(QStringLiteral("mainline_summary.json")));
    if (mainline.isEmpty()) {
        if (QFileInfo::exists(QDir(evidenceRoot).filePath(QStringLiteral("runtime_node_snapshot.json")))) {
            appendUnique(evidenceRoot);
        } else {
            const QFileInfoList children =
                QDir(evidenceRoot).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);
            for (const QFileInfo& child : children) {
                if (QFileInfo::exists(QDir(child.absoluteFilePath()).filePath(QStringLiteral("runtime_node_snapshot.json")))) {
                    appendUnique(child.absoluteFilePath());
                }
            }
        }
        return out;
    }

    const QString onlineDir =
        pathFromJson(mainline.value(QStringLiteral("online")).toObject().value(QStringLiteral("run_dir")));
    if (!onlineDir.isEmpty()) {
        appendUnique(onlineDir);
    }
    for (const QJsonValue& value :
         mainline.value(QStringLiteral("prediction")).toObject().value(QStringLiteral("runs")).toArray()) {
        const QString runDir = pathFromJson(value.toObject().value(QStringLiteral("run_dir")));
        if (!runDir.isEmpty()) {
            appendUnique(runDir);
        }
    }
    return out;
}

QString resolveArtifactPath(const QString& runDir, const PdkDataPlaneEntryView& entry) {
    QString path = normalizeArtifactPath(entry.artifactUri.isEmpty() ? entry.ref : entry.artifactUri);
    if (QDir::isAbsolutePath(path)) {
        return path;
    }
    if (!entry.sourceRunDir.isEmpty()) {
        return QDir(entry.sourceRunDir).filePath(path);
    }
    return QDir(runDir).filePath(path);
}

QString shapeText(const PdkDataPlaneEntryView& entry) {
    if (!entry.shape.isEmpty()) {
        return entry.shape.join(QStringLiteral(" x "));
    }
    if (entry.nodeCount > 0) {
        return QString::number(entry.nodeCount);
    }
    return QStringLiteral("-");
}

QString dashIfEmpty(const QString& text) {
    return text.isEmpty() ? QStringLiteral("-") : text;
}

QString intText(int value) {
    return value >= 0 ? QString::number(value) : QStringLiteral("-");
}

QString jsonNumberText(const QJsonObject& object, const QString& key) {
    const QJsonValue value = object.value(key);
    if (!value.isDouble()) {
        return QStringLiteral("-");
    }
    return QString::number(value.toDouble(), 'g', 8);
}

QString statsText(const QJsonObject& stats) {
    if (stats.isEmpty()) {
        return QStringLiteral("-");
    }
    return QStringLiteral("min=%1, max=%2, mean=%3")
        .arg(jsonNumberText(stats, QStringLiteral("min")),
             jsonNumberText(stats, QStringLiteral("max")),
             jsonNumberText(stats, QStringLiteral("mean")));
}

QString runScopeLabel(const QString& evidenceRoot, const QString& runDir) {
    if (QDir::fromNativeSeparators(evidenceRoot).compare(QDir::fromNativeSeparators(runDir), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("主 evidence 聚合");
    }
    return QFileInfo(runDir).fileName();
}

} // namespace

DataPlanePage::DataPlanePage(QString evidenceRoot, QString objectPackageRoot, QWidget* parent)
    : QWidget(parent),
      evidenceRoot_(std::move(evidenceRoot)),
      objectPackageRoot_(std::move(objectPackageRoot)),
      runDirs_(runtimeRunDirs(evidenceRoot_)) {
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    auto* leftCol = new QVBoxLayout();
    leftCol->setSpacing(12);
    leftCol->addWidget(makePageHeader(
        QStringLiteral("数据平面与三维场"),
        QStringLiteral("运行时端口数据、artifact_ref、大场文件与真实网格渲染"), this));

    auto* summaryPanel = new Panel(QStringLiteral("数据平面总览"), this);
    summaryPanel->setSubtitle(QStringLiteral("run_timeline_index / branch_registry / data_plane_manifest"));
    summaryKv_ = new KvList(summaryPanel->body());
    summaryPanel->bodyLayout()->addWidget(summaryKv_);
    leftCol->addWidget(summaryPanel, 0);

    auto* runPanel = new Panel(QStringLiteral("运行实例"), this);
    runTable_ = makeTable({
        QStringLiteral("范围"),
        QStringLiteral("端口记录"),
        QStringLiteral("artifact_ref"),
        QStringLiteral("inline_json"),
        QStringLiteral("run / workflow")
    }, runPanel->body());
    for (const QString& runDir : runDirs_) {
        const PdkDataPlaneView dataPlane = PdkDataPlaneReader().read(runDir);
        const int row = runTable_->rowCount();
        runTable_->insertRow(row);
        runTable_->setItem(row, 0, new QTableWidgetItem(runScopeLabel(evidenceRoot_, runDir)));
        runTable_->setItem(row, 1, new QTableWidgetItem(QString::number(dataPlane.entries.size())));
        runTable_->setItem(row, 2, new QTableWidgetItem(QString::number(dataPlane.artifactRefCount())));
        runTable_->setItem(row, 3, new QTableWidgetItem(QString::number(dataPlane.inlineJsonCount())));
        runTable_->setItem(row, 4, new QTableWidgetItem(dataPlane.workflowId.isEmpty() ? QFileInfo(runDir).fileName() : dataPlane.workflowId));
    }
    if (runTable_->rowCount() == 0) {
        runTable_->insertRow(0);
        runTable_->setItem(0, 0, new QTableWidgetItem(QStringLiteral("暂无运行时证据")));
    }
    connect(runTable_, &QTableWidget::currentCellChanged, this, [this](int row, int, int, int) {
        loadSelectedRun(row);
    });
    runPanel->bodyLayout()->addWidget(runTable_);
    leftCol->addWidget(runPanel, 1);

    auto* entryPanel = new Panel(QStringLiteral("端口 / artifact"), this);
    entryPanel->setSubtitle(QStringLiteral("run_timeline_index.json / data_plane_manifest.json"));
    entryTable_ = makeTable(
        {QStringLiteral("分支"), QStringLiteral("帧"), QStringLiteral("step"), QStringLiteral("节点"), QStringLiteral("端口"),
         QStringLiteral("表示"), QStringLiteral("场/QoI"), QStringLiteral("部件"), QStringLiteral("节点数"), QStringLiteral("来源")},
        entryPanel->body());
    connect(entryTable_, &QTableWidget::currentCellChanged, this, [this](int row, int, int, int) {
        showEntry(row);
    });
    entryPanel->bodyLayout()->addWidget(entryTable_);
    leftCol->addWidget(entryPanel, 2);
    root->addLayout(leftCol, 2);

    auto* rightCol = new QVBoxLayout();
    rightCol->setSpacing(12);
    auto* detailPanel = new Panel(QStringLiteral("数据平面详情"), this);
    detailTitle_ = new QLabel(QStringLiteral("选择一个 artifact_ref 端口"), detailPanel->body());
    detailTitle_->setProperty("mono", true);
    detailTitle_->setWordWrap(true);
    detailTitle_->setStyleSheet(QStringLiteral("font-weight:700;font-size:13px;color:#1b1f27;"));
    detailPanel->bodyLayout()->addWidget(detailTitle_);
    detailKv_ = new KvList(detailPanel->body());
    detailPanel->bodyLayout()->addWidget(detailKv_);
    rightCol->addWidget(detailPanel, 1);

    auto* fieldPanel = new Panel(QStringLiteral("真实场渲染"), this);
    fieldPanel->setSubtitle(QStringLiteral("对象包 mesh layout + field artifact"));
    fieldWidget_ = new VtkModelFieldWidget(fieldPanel->body());
    fieldPanel->bodyLayout()->addWidget(fieldWidget_, 1);
    rightCol->addWidget(fieldPanel, 2);
    root->addLayout(rightCol, 3);

    populateSummary();
    if (!runDirs_.isEmpty()) {
        runTable_->setCurrentCell(0, 0);
        loadSelectedRun(0);
    }
}

void DataPlanePage::setEvidenceRoot(const QString& evidenceRoot) {
    evidenceRoot_ = QDir::fromNativeSeparators(evidenceRoot);
    rebuildRunTable(currentRunDir_);
}

void DataPlanePage::populateSummary() {
    if (!summaryKv_) {
        return;
    }
    summaryKv_->clear();
    summaryKv_->addRow(QStringLiteral("evidence 根"), evidenceRoot_.isEmpty() ? QStringLiteral("-") : evidenceRoot_, false);

    const QJsonObject timeline =
        readJsonObject(QDir(evidenceRoot_).filePath(QStringLiteral("run_timeline_index.json")));
    const QJsonObject branchRegistry =
        readJsonObject(QDir(evidenceRoot_).filePath(QStringLiteral("branch_registry.json")));
    const QJsonObject mainline =
        readJsonObject(QDir(evidenceRoot_).filePath(QStringLiteral("mainline_summary.json")));

    const QJsonObject timelineSummary = timeline.value(QStringLiteral("summary")).toObject();
    const QJsonObject branchSummary = branchRegistry.value(QStringLiteral("summary")).toObject();
    summaryKv_->addRow(QStringLiteral("run_id"),
                       dashIfEmpty(timeline.value(QStringLiteral("run_id")).toString(
                           branchRegistry.value(QStringLiteral("run_id")).toString(
                               mainline.value(QStringLiteral("run_id_prefix")).toString()))));
    summaryKv_->addRow(QStringLiteral("object_id"),
                       dashIfEmpty(timeline.value(QStringLiteral("object_id")).toString(
                           branchRegistry.value(QStringLiteral("object_id")).toString(
                               mainline.value(QStringLiteral("object_id")).toString()))));
    summaryKv_->addRow(QStringLiteral("分支数"),
                       QString::number(branchSummary.value(QStringLiteral("branch_count")).toInt(
                           timelineSummary.value(QStringLiteral("branch_count")).toInt())));
    summaryKv_->addRow(QStringLiteral("在线帧"),
                       QString::number(timelineSummary.value(QStringLiteral("online_frame_count")).toInt(
                           mainline.value(QStringLiteral("online")).toObject().value(QStringLiteral("effective_frames")).toInt())));
    summaryKv_->addRow(QStringLiteral("预测分支"),
                       QString::number(timelineSummary.value(QStringLiteral("prediction_run_count")).toInt(
                           mainline.value(QStringLiteral("prediction")).toObject().value(QStringLiteral("run_count")).toInt())));
    summaryKv_->addRow(QStringLiteral("artifact_ref"),
                       QString::number(timelineSummary.value(QStringLiteral("artifact_ref_count")).toInt()));
    summaryKv_->addRow(QStringLiteral("QoI 引用"),
                       QString::number(timelineSummary.value(QStringLiteral("qoi_ref_count")).toInt()));
    summaryKv_->addRow(QStringLiteral("checkpoint"),
                       QString::number(timeline.value(QStringLiteral("checkpoint_refs")).toArray().size()));
}

void DataPlanePage::rebuildRunTable(const QString& preferredRunDir) {
    if (!runTable_ || !entryTable_) {
        return;
    }
    const QString preferred = QDir::fromNativeSeparators(preferredRunDir);
    runDirs_ = runtimeRunDirs(evidenceRoot_);
    populateSummary();
    currentDataPlane_ = {};
    currentRunDir_.clear();
    ++fieldLoadSerial_;

    const QSignalBlocker blocker(runTable_);
    runTable_->setRowCount(0);
    int preferredRow = -1;
    for (const QString& runDir : runDirs_) {
        const PdkDataPlaneView dataPlane = PdkDataPlaneReader().read(runDir);
        const int row = runTable_->rowCount();
        runTable_->insertRow(row);
        runTable_->setItem(row, 0, new QTableWidgetItem(runScopeLabel(evidenceRoot_, runDir)));
        runTable_->setItem(row, 1, new QTableWidgetItem(QString::number(dataPlane.entries.size())));
        runTable_->setItem(row, 2, new QTableWidgetItem(QString::number(dataPlane.artifactRefCount())));
        runTable_->setItem(row, 3, new QTableWidgetItem(QString::number(dataPlane.inlineJsonCount())));
        runTable_->setItem(row, 4, new QTableWidgetItem(dataPlane.workflowId.isEmpty() ? QFileInfo(runDir).fileName() : dataPlane.workflowId));
        if (!preferred.isEmpty() &&
            QDir::fromNativeSeparators(runDir).compare(preferred, Qt::CaseInsensitive) == 0) {
            preferredRow = row;
        }
    }
    if (runTable_->rowCount() == 0) {
        runTable_->insertRow(0);
        runTable_->setItem(0, 0, new QTableWidgetItem(QStringLiteral("暂无运行时证据")));
        entryTable_->setRowCount(0);
        detailTitle_->setText(QStringLiteral("选择一个 artifact_ref 端口"));
        detailKv_->clear();
        fieldWidget_->clearField(QStringLiteral("等待 data_plane_manifest.json"));
        return;
    }

    const int selectedRow = preferredRow >= 0 ? preferredRow : 0;
    runTable_->setCurrentCell(selectedRow, 0);
    loadSelectedRun(selectedRow);
}

void DataPlanePage::loadSelectedRun(int row) {
    if (row < 0 || row >= runDirs_.size()) {
        return;
    }
    currentRunDir_ = runDirs_[row];
    currentDataPlane_ = PdkDataPlaneReader().read(currentRunDir_);
    entryTable_->setRowCount(0);
    for (const PdkDataPlaneEntryView& entry : currentDataPlane_.entries) {
        const int outRow = entryTable_->rowCount();
        entryTable_->insertRow(outRow);
        entryTable_->setItem(outRow, 0, new QTableWidgetItem(dashIfEmpty(entry.branchId)));
        entryTable_->setItem(outRow, 1, new QTableWidgetItem(intText(entry.mainlineFrameIndex)));
        entryTable_->setItem(outRow, 2, new QTableWidgetItem(intText(entry.stepIndex)));
        entryTable_->setItem(outRow, 3, new QTableWidgetItem(entry.nodeId));
        entryTable_->setItem(outRow, 4, new QTableWidgetItem(entry.portId));
        entryTable_->setItem(outRow, 5, new QTableWidgetItem(entry.representation));
        entryTable_->setItem(outRow, 6, new QTableWidgetItem(entry.fieldName));
        entryTable_->setItem(outRow, 7, new QTableWidgetItem(dashIfEmpty(entry.componentId)));
        entryTable_->setItem(outRow, 8, new QTableWidgetItem(QString::number(entry.nodeCount)));
        entryTable_->setItem(outRow, 9, new QTableWidgetItem(QFileInfo(entry.sourceRunDir).fileName()));
    }
    if (entryTable_->rowCount() == 0) {
        entryTable_->insertRow(0);
        entryTable_->setItem(0, 0, new QTableWidgetItem(QStringLiteral("当前运行实例没有数据平面端口记录")));
    } else {
        entryTable_->setCurrentCell(0, 0);
        showEntry(0);
    }
}

void DataPlanePage::showEntry(int row) {
    if (row < 0 || row >= currentDataPlane_.entries.size()) {
        return;
    }
    const PdkDataPlaneEntryView& entry = currentDataPlane_.entries[row];
    detailTitle_->setText(QStringLiteral("%1 / %2").arg(entry.nodeId, entry.portId));
    detailKv_->clear();
    detailKv_->addRow(QStringLiteral("分支"), dashIfEmpty(entry.branchId));
    detailKv_->addRow(QStringLiteral("主线帧"), intText(entry.mainlineFrameIndex));
    detailKv_->addRow(QStringLiteral("step"), intText(entry.stepIndex));
    detailKv_->addRow(QStringLiteral("源运行目录"), dashIfEmpty(entry.sourceRunDir), false);
    detailKv_->addRow(QStringLiteral("算子ID"), entry.operatorId);
    detailKv_->addRow(QStringLiteral("方向"), entry.direction, false);
    detailKv_->addRow(QStringLiteral("契约ID"), entry.contractId);
    detailKv_->addRow(QStringLiteral("数据表示"), entry.representation, false);
    detailKv_->addRow(QStringLiteral("artifact 路径"), resolveArtifactPath(currentRunDir_, entry));
    detailKv_->addRow(QStringLiteral("布局引用"), entry.layoutRef);
    detailKv_->addRow(QStringLiteral("网格引用"), entry.meshRef);
    detailKv_->addRow(QStringLiteral("对象部件"), entry.componentId);
    detailKv_->addRow(QStringLiteral("形状"), shapeText(entry));
    detailKv_->addRow(QStringLiteral("节点数"), QString::number(entry.nodeCount));
    detailKv_->addRow(QStringLiteral("统计"), statsText(entry.statistics));
    detailKv_->addRow(QStringLiteral("校验值"), entry.checksum);
    renderEntryField(entry);
}

void DataPlanePage::renderEntryField(const PdkDataPlaneEntryView& entry) {
    if (entry.representation != QStringLiteral("artifact_ref") || entry.fieldName.isEmpty()) {
        fieldWidget_->clearField(QStringLiteral("当前端口不是可渲染的场 artifact_ref"));
        return;
    }
    const QString snapshotBase = entry.sourceRunDir.isEmpty() ? currentRunDir_ : entry.sourceRunDir;
    const QString snapshotPath = QFileInfo::exists(QDir(snapshotBase).filePath(QStringLiteral("runtime_snapshot.json")))
        ? QDir(snapshotBase).filePath(QStringLiteral("runtime_snapshot.json"))
        : QDir(evidenceRoot_).filePath(QStringLiteral("runtime_snapshot.json"));
    const QString artifactPath = resolveArtifactPath(currentRunDir_, entry);
    const quint64 serial = ++fieldLoadSerial_;
    fieldWidget_->clearField(QStringLiteral("场数据后台加载中，界面保持可操作..."));
    loadFieldArtifactAsync(
        this,
        artifactPath,
        objectPackageRoot_,
        snapshotPath,
        fieldRenderHintFromEntry(entry),
        QFileInfo(artifactPath).baseName(),
        [this, serial](LoadedFieldArtifact loaded) {
            applyFieldLoad(serial, std::move(loaded));
        });
}

void DataPlanePage::applyFieldLoad(quint64 serial, LoadedFieldArtifact loaded) {
    if (serial != fieldLoadSerial_) {
        return;
    }
    if (!loaded.ok) {
        fieldWidget_->clearField(loaded.message.isEmpty() ? QStringLiteral("field artifact 读取失败") : loaded.message);
        return;
    }
    fieldWidget_->setMeshLayoutCatalog(loaded.meshCatalog);
    const auto stats = fieldWidget_->renderFlattenedValues(
        loaded.values,
        loaded.layoutId,
        1,
        0,
        QStringLiteral("%1 / %2").arg(loaded.fieldName, loaded.title),
        loaded.unit);
    if (!stats.ok) {
        fieldWidget_->clearField(stats.message.isEmpty() ? QStringLiteral("场渲染失败") : stats.message);
        return;
    }
    detailKv_->addRow(QStringLiteral("场/网格绑定"), loaded.bindingMessage, false);
}

} // namespace twin
