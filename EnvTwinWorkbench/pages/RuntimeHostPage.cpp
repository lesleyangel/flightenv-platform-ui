#include "RuntimeHostPage.h"

#include "../widgets/KvList.h"
#include "../widgets/Panel.h"
#include "../widgets/PageHeader.h"
#include "../widgets/StatusUtil.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileInfoList>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QSet>
#include <QSignalBlocker>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <utility>

namespace twin {

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

QString workspaceFromObjectPackage(QString objectPackageRoot) {
    QDir dir(objectPackageRoot);
    dir.cdUp();
    return dir.absolutePath();
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
    if (QFileInfo::exists(QDir(evidenceRoot).filePath(QStringLiteral("runtime_host_evidence.json"))) ||
        QFileInfo::exists(QDir(evidenceRoot).filePath(QStringLiteral("run_timeline_index.json")))) {
        appendUnique(evidenceRoot);
    }
    const QJsonObject branchRegistry =
        readJsonObject(QDir(evidenceRoot).filePath(QStringLiteral("branch_registry.json")));
    for (const QJsonValue& value : branchRegistry.value(QStringLiteral("branches")).toArray()) {
        const QString runDir = pathFromJson(value.toObject().value(QStringLiteral("run_dir")));
        if (!runDir.isEmpty()) {
            appendUnique(runDir);
        }
    }

    const QJsonObject mainline = readJsonObject(QDir(evidenceRoot).filePath(QStringLiteral("mainline_summary.json")));
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
    const QString onlineDir = pathFromJson(mainline.value(QStringLiteral("online")).toObject().value(QStringLiteral("run_dir")));
    if (!onlineDir.isEmpty()) {
        appendUnique(onlineDir);
    }
    for (const QJsonValue& value : mainline.value(QStringLiteral("prediction")).toObject().value(QStringLiteral("runs")).toArray()) {
        const QString runDir = pathFromJson(value.toObject().value(QStringLiteral("run_dir")));
        if (!runDir.isEmpty()) {
            appendUnique(runDir);
        }
    }
    return out;
}

QString joinUniqueValues(const QJsonArray& values, const QString& key) {
    QStringList out;
    for (const QJsonValue& value : values) {
        const QString text = value.toObject().value(key).toString();
        if (!text.isEmpty() && !out.contains(text)) {
            out << text;
        }
    }
    return out.isEmpty() ? QStringLiteral("-") : out.join(QStringLiteral("\n"));
}

QString joinStringArray(const QJsonArray& values) {
    QStringList out;
    for (const QJsonValue& value : values) {
        const QString text = value.toString();
        if (!text.isEmpty() && !out.contains(text)) {
            out << text;
        }
    }
    return out.isEmpty() ? QStringLiteral("-") : out.join(QStringLiteral("\n"));
}

QJsonObject sessionSummaryForRun(const QString& runDir) {
    QJsonObject sessions = readJsonObject(QDir(runDir).filePath(QStringLiteral("adapter_session_summary.json")));
    if (!sessions.isEmpty()) {
        return sessions;
    }
    const QJsonObject host = readJsonObject(QDir(runDir).filePath(QStringLiteral("runtime_host_evidence.json")));
    return host.value(QStringLiteral("summary")).toObject()
        .value(QStringLiteral("online_native_session_summary")).toObject();
}

QString backendForRun(const QString& runDir) {
    const QJsonObject sessions = sessionSummaryForRun(runDir);
    const QString backend = sessions.value(QStringLiteral("execution_backend")).toString();
    if (!backend.isEmpty()) {
        return backend;
    }
    const QJsonObject host = readJsonObject(QDir(runDir).filePath(QStringLiteral("runtime_host_evidence.json")));
    const QString hostBackend = host.value(QStringLiteral("host")).toObject().value(QStringLiteral("execution_backend")).toString();
    if (!hostBackend.isEmpty()) {
        return hostBackend;
    }
    const QJsonObject evidence = readJsonObject(QDir(runDir).filePath(QStringLiteral("runtime_evidence.json")));
    return evidence.value(QStringLiteral("execution_backend")).toString(QStringLiteral("-"));
}

QString protocolsForRun(const QString& runDir) {
    return joinUniqueValues(
        sessionSummaryForRun(runDir).value(QStringLiteral("sessions")).toArray(),
        QStringLiteral("protocol"));
}

QString sessionModesForRun(const QString& runDir) {
    return joinUniqueValues(
        sessionSummaryForRun(runDir).value(QStringLiteral("sessions")).toArray(),
        QStringLiteral("session_mode"));
}

QString slowPathWarningForRun(const QString& runDir) {
    const QJsonArray sessions = sessionSummaryForRun(runDir).value(QStringLiteral("sessions")).toArray();
    for (const QJsonValue& value : sessions) {
        const QJsonObject session = value.toObject();
        const QString protocol = session.value(QStringLiteral("protocol")).toString();
        const QString mode = session.value(QStringLiteral("session_mode")).toString();
        if (protocol == QStringLiteral("json_file.v1") ||
            protocol == QStringLiteral("python_worker.v1") ||
            mode == QStringLiteral("external_process_per_event")) {
            return QStringLiteral("警告：存在 CLI/Python/外部进程适配器");
        }
    }
    return QStringLiteral("ok：未发现 json_file/python_worker 热路径");
}

QString dashIfEmpty(const QString& text) {
    return text.isEmpty() ? QStringLiteral("-") : text;
}

QString jsonNumberText(const QJsonValue& value) {
    if (!value.isDouble()) {
        return QStringLiteral("-");
    }
    return QString::number(value.toDouble(), 'g', 8);
}

QString boolText(const QJsonValue& value) {
    if (!value.isBool()) {
        return QStringLiteral("-");
    }
    return value.toBool() ? QStringLiteral("是") : QStringLiteral("否");
}

QString runLabel(const QString& evidenceRoot, const QString& runDir) {
    if (QDir::fromNativeSeparators(evidenceRoot).compare(QDir::fromNativeSeparators(runDir), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("主 RuntimeHost evidence");
    }
    return QFileInfo(runDir).fileName();
}

} // namespace

RuntimeHostPage::RuntimeHostPage(QString evidenceRoot, QString objectPackageRoot, QWidget* parent)
    : QWidget(parent),
      evidenceRoot_(std::move(evidenceRoot)),
      workspaceRoot_(workspaceFromObjectPackage(objectPackageRoot)),
      objectPackage_(PdkObjectPackageReader().read(objectPackageRoot)) {
    const QDir compiledRoot(QDir(workspaceRoot_).filePath(QStringLiteral("_local_artifacts/platform-pdk/compiled-workflows")));
    for (const PdkWorkflowView& workflow : objectPackage_.workflows) {
        const QString dir = compiledRoot.filePath(workflow.workflowId);
        if (QFileInfo::exists(dir)) {
            compiledWorkflows_.push_back(PdkCompiledWorkflowReader().read(dir));
        }
    }
    loadEvidenceFiles();
    runDirs_ = runtimeRunDirs(evidenceRoot_);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);
    root->addWidget(makePageHeader(
        QStringLiteral("运行时主机"),
        QStringLiteral("执行计划 / 时间计划 / 调度计划 / 状态存储 / 运行证据"), this));

    auto* body = new QHBoxLayout();
    body->setSpacing(12);

    auto* leftCol = new QVBoxLayout();
    leftCol->setSpacing(12);

    auto* hostPanel = new Panel(QStringLiteral("Host 总览"), this);
    hostPanel->setSubtitle(QStringLiteral("mainline_progress / runtime_host_evidence / operator_initialization"));
    hostKv_ = new KvList(hostPanel->body());
    hostPanel->bodyLayout()->addWidget(hostKv_);
    leftCol->addWidget(hostPanel, 0);

    auto* compiledPanel = new Panel(QStringLiteral("编译产物"), this);
    compiledPanel->setSubtitle(QStringLiteral("compiled-workflows/<workflow_id>"));
    compiledTable_ = makeTable({QStringLiteral("工作流"), QStringLiteral("阶段"), QStringLiteral("节点数"), QStringLiteral("计划")}, compiledPanel->body());
    for (const PdkCompiledWorkflowView& compiled : compiledWorkflows_) {
        const int row = compiledTable_->rowCount();
        compiledTable_->insertRow(row);
        compiledTable_->setItem(row, 0, new QTableWidgetItem(compiled.workflowId));
        compiledTable_->setItem(row, 1, new QTableWidgetItem(compiled.phase));
        compiledTable_->setItem(row, 2, new QTableWidgetItem(QString::number(compiled.nodes.size())));
        compiledTable_->setItem(row, 3, new QTableWidgetItem(QStringLiteral("执行/时间/调度/状态/数据")));
    }
    if (compiledTable_->rowCount() == 0) {
        compiledTable_->insertRow(0);
        compiledTable_->setItem(0, 0, new QTableWidgetItem(QStringLiteral("暂无已编译工作流")));
    }
    connect(compiledTable_, &QTableWidget::currentCellChanged, this, [this](int row, int, int, int) { showCompiledWorkflow(row); });
    compiledPanel->bodyLayout()->addWidget(compiledTable_);
    leftCol->addWidget(compiledPanel, 1);

    auto* runPanel = new Panel(QStringLiteral("运行实例"), this);
    runPanel->setSubtitle(QStringLiteral("runtime-host-runs"));
    runTable_ = makeTable({
        QStringLiteral("范围"),
        QStringLiteral("backend"),
        QStringLiteral("protocol"),
        QStringLiteral("session"),
        QStringLiteral("节点数"),
        QStringLiteral("迭代次数"),
        QStringLiteral("检查点"),
        QStringLiteral("目录")
    }, runPanel->body());
    for (const QString& runDir : runDirs_) {
        const QJsonObject snapshot = readJsonObject(QDir(runDir).filePath(QStringLiteral("runtime_node_snapshot.json")));
        const QJsonObject loop = readJsonObject(QDir(runDir).filePath(QStringLiteral("runtime_loop_summary.json"))).value(QStringLiteral("summary")).toObject();
        const QJsonObject timeline = readJsonObject(QDir(runDir).filePath(QStringLiteral("run_timeline_index.json")));
        const int row = runTable_->rowCount();
        runTable_->insertRow(row);
        runTable_->setItem(row, 0, new QTableWidgetItem(runLabel(evidenceRoot_, runDir)));
        runTable_->setItem(row, 1, new QTableWidgetItem(backendForRun(runDir)));
        runTable_->setItem(row, 2, new QTableWidgetItem(protocolsForRun(runDir)));
        runTable_->setItem(row, 3, new QTableWidgetItem(sessionModesForRun(runDir)));
        runTable_->setItem(row, 4, new QTableWidgetItem(QString::number(snapshot.value(QStringLiteral("nodes")).toArray().size())));
        runTable_->setItem(row, 5, new QTableWidgetItem(QString::number(loop.value(QStringLiteral("iteration_count")).toInt(
            timeline.value(QStringLiteral("summary")).toObject().value(QStringLiteral("branch_step_count")).toInt()))));
        runTable_->setItem(row, 6, new QTableWidgetItem(QFileInfo::exists(QDir(runDir).filePath(QStringLiteral("state_checkpoint.json"))) ? QStringLiteral("有") : QStringLiteral("无")));
        runTable_->setItem(row, 7, new QTableWidgetItem(QFileInfo(runDir).fileName()));
    }
    if (runTable_->rowCount() == 0) {
        runTable_->insertRow(0);
        runTable_->setItem(0, 0, new QTableWidgetItem(QStringLiteral("暂无运行时证据")));
    }
    connect(runTable_, &QTableWidget::currentCellChanged, this, [this](int row, int, int, int) { showRun(row); });
    runPanel->bodyLayout()->addWidget(runTable_);
    leftCol->addWidget(runPanel, 1);

    auto* branchPanel = new Panel(QStringLiteral("分支 registry"), this);
    branchPanel->setSubtitle(QStringLiteral("branch_registry.json"));
    branchTable_ = makeTable({
        QStringLiteral("分支"),
        QStringLiteral("类型"),
        QStringLiteral("状态"),
        QStringLiteral("触发帧"),
        QStringLiteral("步/帧"),
        QStringLiteral("artifact")
    }, branchPanel->body());
    connect(branchTable_, &QTableWidget::currentCellChanged, this, [this](int row, int, int, int) { showBranch(row); });
    branchPanel->bodyLayout()->addWidget(branchTable_);
    leftCol->addWidget(branchPanel, 1);

    auto* eventPanel = new Panel(QStringLiteral("Runtime 事件"), this);
    eventPanel->setSubtitle(QStringLiteral("runtime_events.json"));
    eventTable_ = makeTable({
        QStringLiteral("事件"),
        QStringLiteral("分支"),
        QStringLiteral("帧"),
        QStringLiteral("t(s)")
    }, eventPanel->body());
    connect(eventTable_, &QTableWidget::currentCellChanged, this, [this](int row, int, int, int) { showEvent(row); });
    eventPanel->bodyLayout()->addWidget(eventTable_);
    leftCol->addWidget(eventPanel, 1);

    body->addLayout(leftCol, 2);

    auto* rightCol = new QVBoxLayout();
    rightCol->setSpacing(12);

    auto* sessionPanel = new Panel(QStringLiteral("Adapter Session"), this);
    sessionPanel->setSubtitle(QStringLiteral("dll_abi / native_in_process_persistent"));
    sessionTable_ = makeTable({
        QStringLiteral("节点"),
        QStringLiteral("算子"),
        QStringLiteral("protocol"),
        QStringLiteral("session"),
        QStringLiteral("execute"),
        QStringLiteral("库")
    }, sessionPanel->body());
    connect(sessionTable_, &QTableWidget::currentCellChanged, this, [this](int row, int, int, int) { showSession(row); });
    sessionPanel->bodyLayout()->addWidget(sessionTable_);
    rightCol->addWidget(sessionPanel, 1);

    auto* detailPanel = new Panel(QStringLiteral("详情"), this);
    detailPanel->setMinimumWidth(340);
    detailPanel->setMaximumWidth(480);
    detailTitle_ = new QLabel(QStringLiteral("选择编译产物或运行实例"), detailPanel->body());
    detailTitle_->setProperty("mono", true);
    detailTitle_->setWordWrap(true);
    detailTitle_->setStyleSheet(QStringLiteral("font-weight:700;font-size:13px;color:#1b1f27;"));
    detailPanel->bodyLayout()->addWidget(detailTitle_);
    detailKv_ = new KvList(detailPanel->body());
    detailPanel->bodyLayout()->addWidget(detailKv_);
    detailPanel->bodyLayout()->addStretch(1);
    rightCol->addWidget(detailPanel, 1);
    body->addLayout(rightCol, 2);
    root->addLayout(body, 1);

    populateHostSummary();
    rebuildRuntimeTables();

    if (!compiledWorkflows_.isEmpty()) {
        compiledTable_->setCurrentCell(0, 0);
        showCompiledWorkflow(0);
    } else if (!runDirs_.isEmpty()) {
        runTable_->setCurrentCell(0, 0);
        showRun(0);
    }
}

void RuntimeHostPage::setEvidenceRoot(const QString& evidenceRoot) {
    evidenceRoot_ = QDir::fromNativeSeparators(evidenceRoot);
    loadEvidenceFiles();
    runDirs_ = runtimeRunDirs(evidenceRoot_);
    populateHostSummary();
    rebuildRuntimeTables();
}

void RuntimeHostPage::loadEvidenceFiles() {
    const QDir root(evidenceRoot_);
    progressJson_ = readJsonObject(root.filePath(QStringLiteral("mainline_progress.json")));
    hostEvidenceJson_ = readJsonObject(root.filePath(QStringLiteral("runtime_host_evidence.json")));
    branchRegistryJson_ = readJsonObject(root.filePath(QStringLiteral("branch_registry.json")));
    runtimeEventsJson_ = readJsonObject(root.filePath(QStringLiteral("runtime_events.json")));
    initializationJson_ = readJsonObject(root.filePath(QStringLiteral("operator_initialization.json")));
    timelineIndexJson_ = readJsonObject(root.filePath(QStringLiteral("run_timeline_index.json")));
    branches_ = branchRegistryJson_.value(QStringLiteral("branches")).toArray();
    events_ = runtimeEventsJson_.value(QStringLiteral("events")).toArray();
    sessions_ = hostEvidenceJson_.value(QStringLiteral("summary")).toObject()
        .value(QStringLiteral("online_native_session_summary")).toObject()
        .value(QStringLiteral("sessions")).toArray();
}

void RuntimeHostPage::populateHostSummary() {
    if (!hostKv_) {
        return;
    }
    hostKv_->clear();
    const QJsonObject host = hostEvidenceJson_.value(QStringLiteral("host")).toObject();
    const QJsonObject progressOnline = progressJson_.value(QStringLiteral("online")).toObject();
    const QJsonObject progressPrediction = progressJson_.value(QStringLiteral("prediction")).toObject();
    const QJsonObject timelineSummary = timelineIndexJson_.value(QStringLiteral("summary")).toObject();
    const QJsonObject branchSummary = branchRegistryJson_.value(QStringLiteral("summary")).toObject();
    hostKv_->addRow(QStringLiteral("状态"), dashIfEmpty(progressJson_.value(QStringLiteral("status")).toString(
        hostEvidenceJson_.value(QStringLiteral("status")).toString())), false);
    hostKv_->addRow(QStringLiteral("阶段"), dashIfEmpty(progressJson_.value(QStringLiteral("stage")).toString()), false);
    hostKv_->addRow(QStringLiteral("后端"), dashIfEmpty(host.value(QStringLiteral("execution_backend")).toString(
        progressJson_.value(QStringLiteral("execution_backend")).toString())), false);
    hostKv_->addRow(QStringLiteral("分支模式"), dashIfEmpty(host.value(QStringLiteral("branch_execution_mode")).toString()), false);
    hostKv_->addRow(QStringLiteral("索引器"), dashIfEmpty(host.value(QStringLiteral("runtime_run_indexer")).toString()), false);
    hostKv_->addRow(QStringLiteral("禁用 wildcard"), dashIfEmpty(host.value(QStringLiteral("wildcard_adapter_policy")).toString()), false);
    hostKv_->addRow(QStringLiteral("初始化"), dashIfEmpty(initializationJson_.value(QStringLiteral("status")).toString(
        progressJson_.value(QStringLiteral("initialization")).toObject().value(QStringLiteral("status")).toString())), false);
    hostKv_->addRow(QStringLiteral("在线帧"),
                    QStringLiteral("%1 / %2")
                        .arg(progressOnline.value(QStringLiteral("completed_frames")).toInt(
                                 timelineSummary.value(QStringLiteral("online_frame_count")).toInt()))
                        .arg(progressOnline.value(QStringLiteral("requested_frames")).toInt()));
    hostKv_->addRow(QStringLiteral("预测分支"),
                    QStringLiteral("%1 完成 / %2 运行 / %3 失败")
                        .arg(branchSummary.value(QStringLiteral("completed_prediction_count")).toInt(
                                 progressPrediction.value(QStringLiteral("completed_runs")).toInt()))
                        .arg(branchSummary.value(QStringLiteral("running_prediction_count")).toInt(
                                 progressPrediction.value(QStringLiteral("running_runs")).toInt()))
                        .arg(branchSummary.value(QStringLiteral("failed_prediction_count")).toInt(
                                 progressPrediction.value(QStringLiteral("failed_runs")).toInt())));
    hostKv_->addRow(QStringLiteral("artifact/qoi"),
                    QStringLiteral("%1 / %2")
                        .arg(timelineSummary.value(QStringLiteral("artifact_ref_count")).toInt())
                        .arg(timelineSummary.value(QStringLiteral("qoi_ref_count")).toInt()));
    hostKv_->addRow(QStringLiteral("evidence 根"), evidenceRoot_.isEmpty() ? QStringLiteral("-") : evidenceRoot_, false);
}

void RuntimeHostPage::rebuildRuntimeTables() {
    if (!runTable_ || !branchTable_ || !eventTable_ || !sessionTable_) {
        return;
    }

    {
        const QSignalBlocker blocker(runTable_);
        runTable_->setRowCount(0);
        for (const QString& runDir : runDirs_) {
            const QJsonObject snapshot = readJsonObject(QDir(runDir).filePath(QStringLiteral("runtime_node_snapshot.json")));
            const QJsonObject loop = readJsonObject(QDir(runDir).filePath(QStringLiteral("runtime_loop_summary.json"))).value(QStringLiteral("summary")).toObject();
            const QJsonObject timeline = readJsonObject(QDir(runDir).filePath(QStringLiteral("run_timeline_index.json")));
            const int row = runTable_->rowCount();
            runTable_->insertRow(row);
            runTable_->setItem(row, 0, new QTableWidgetItem(runLabel(evidenceRoot_, runDir)));
            runTable_->setItem(row, 1, new QTableWidgetItem(backendForRun(runDir)));
            runTable_->setItem(row, 2, new QTableWidgetItem(protocolsForRun(runDir)));
            runTable_->setItem(row, 3, new QTableWidgetItem(sessionModesForRun(runDir)));
            runTable_->setItem(row, 4, new QTableWidgetItem(QString::number(snapshot.value(QStringLiteral("nodes")).toArray().size())));
            runTable_->setItem(row, 5, new QTableWidgetItem(QString::number(loop.value(QStringLiteral("iteration_count")).toInt(
                timeline.value(QStringLiteral("summary")).toObject().value(QStringLiteral("branch_step_count")).toInt()))));
            runTable_->setItem(row, 6, new QTableWidgetItem(QFileInfo::exists(QDir(runDir).filePath(QStringLiteral("state_checkpoint.json"))) ? QStringLiteral("有") : QStringLiteral("无")));
            runTable_->setItem(row, 7, new QTableWidgetItem(QFileInfo(runDir).fileName()));
        }
        if (runTable_->rowCount() == 0) {
            runTable_->insertRow(0);
            runTable_->setItem(0, 0, new QTableWidgetItem(QStringLiteral("暂无运行时证据")));
        }
    }

    {
        const QSignalBlocker blocker(branchTable_);
        branchTable_->setRowCount(0);
        for (const QJsonValue& value : branches_) {
            const QJsonObject branch = value.toObject();
            const QJsonObject summary = branch.value(QStringLiteral("summary")).toObject();
            const int row = branchTable_->rowCount();
            branchTable_->insertRow(row);
            branchTable_->setItem(row, 0, new QTableWidgetItem(branch.value(QStringLiteral("display_name")).toString(
                branch.value(QStringLiteral("branch_id")).toString())));
            branchTable_->setItem(row, 1, new QTableWidgetItem(branch.value(QStringLiteral("kind_label")).toString(
                branch.value(QStringLiteral("branch_kind")).toString())));
            branchTable_->setItem(row, 2, new QTableWidgetItem(branch.value(QStringLiteral("status")).toString()));
            branchTable_->setItem(row, 3, new QTableWidgetItem(jsonNumberText(branch.value(QStringLiteral("trigger_frame_index")))));
            branchTable_->setItem(row, 4, new QTableWidgetItem(QString::number(summary.value(QStringLiteral("iteration_count")).toInt(
                summary.value(QStringLiteral("step_count")).toInt(summary.value(QStringLiteral("frame_count")).toInt())))));
            branchTable_->setItem(row, 5, new QTableWidgetItem(QString::number(summary.value(QStringLiteral("field_artifact_count")).toInt(
                summary.value(QStringLiteral("artifact_count")).toInt()))));
        }
        if (branchTable_->rowCount() == 0) {
            branchTable_->insertRow(0);
            branchTable_->setItem(0, 0, new QTableWidgetItem(QStringLiteral("暂无分支 registry")));
        }
    }

    {
        const QSignalBlocker blocker(eventTable_);
        eventTable_->setRowCount(0);
        for (const QJsonValue& value : events_) {
            const QJsonObject event = value.toObject();
            const int row = eventTable_->rowCount();
            eventTable_->insertRow(row);
            eventTable_->setItem(row, 0, new QTableWidgetItem(event.value(QStringLiteral("event_kind")).toString()));
            eventTable_->setItem(row, 1, new QTableWidgetItem(event.value(QStringLiteral("branch_id")).toString()));
            eventTable_->setItem(row, 2, new QTableWidgetItem(jsonNumberText(event.value(QStringLiteral("frame_index")))));
            eventTable_->setItem(row, 3, new QTableWidgetItem(jsonNumberText(event.value(QStringLiteral("time_s")))));
        }
        if (eventTable_->rowCount() == 0) {
            eventTable_->insertRow(0);
            eventTable_->setItem(0, 0, new QTableWidgetItem(QStringLiteral("暂无运行事件")));
        }
    }

    {
        const QSignalBlocker blocker(sessionTable_);
        sessionTable_->setRowCount(0);
        for (const QJsonValue& value : sessions_) {
            const QJsonObject session = value.toObject();
            const QJsonObject counters = session.value(QStringLiteral("counters")).toObject();
            const int row = sessionTable_->rowCount();
            sessionTable_->insertRow(row);
            sessionTable_->setItem(row, 0, new QTableWidgetItem(session.value(QStringLiteral("node_id")).toString()));
            sessionTable_->setItem(row, 1, new QTableWidgetItem(session.value(QStringLiteral("operator_id")).toString()));
            sessionTable_->setItem(row, 2, new QTableWidgetItem(session.value(QStringLiteral("protocol")).toString()));
            sessionTable_->setItem(row, 3, new QTableWidgetItem(session.value(QStringLiteral("session_mode")).toString()));
            sessionTable_->setItem(row, 4, new QTableWidgetItem(QString::number(counters.value(QStringLiteral("execute")).toInt())));
            sessionTable_->setItem(row, 5, new QTableWidgetItem(QFileInfo(session.value(QStringLiteral("library")).toString()).fileName()));
        }
        if (sessionTable_->rowCount() == 0) {
            sessionTable_->insertRow(0);
            sessionTable_->setItem(0, 0, new QTableWidgetItem(QStringLiteral("暂无 adapter session")));
        }
    }
}

void RuntimeHostPage::showCompiledWorkflow(int row) {
    if (row < 0 || row >= compiledWorkflows_.size()) {
        return;
    }
    const PdkCompiledWorkflowView& compiled = compiledWorkflows_[row];
    detailTitle_->setText(compiled.workflowId);
    detailKv_->clear();
    detailKv_->addRow(QStringLiteral("编译目录"), compiled.compiledDir);
    detailKv_->addRow(QStringLiteral("阶段"), compiled.phase, false);
    detailKv_->addRow(QStringLiteral("节点数"), QString::number(compiled.nodes.size()));
    detailKv_->addRow(QStringLiteral("激活节点"), QString::number(compiled.activationNodes.size()));
    detailKv_->addRow(QStringLiteral("时间计划"), compiled.timePlanJson.value(QStringLiteral("schema_version")).toString());
    detailKv_->addRow(QStringLiteral("调度计划"), compiled.schedulerPlanJson.value(QStringLiteral("schema_version")).toString());
    detailKv_->addRow(QStringLiteral("数据平面计划"), compiled.dataPlanePlanJson.value(QStringLiteral("schema_version")).toString());
    detailKv_->addRow(QStringLiteral("状态存储计划"), QFileInfo::exists(QDir(compiled.compiledDir).filePath(QStringLiteral("state_store_plan.json"))) ? QStringLiteral("存在") : QStringLiteral("缺失"));
}

void RuntimeHostPage::showRun(int row) {
    if (row < 0 || row >= runDirs_.size()) {
        return;
    }
    const QString runDir = runDirs_[row];
    const QJsonObject snapshot = readJsonObject(QDir(runDir).filePath(QStringLiteral("runtime_node_snapshot.json")));
    const QJsonObject scheduler = readJsonObject(QDir(runDir).filePath(QStringLiteral("scheduler_timeline.json")));
    const QJsonObject loop = readJsonObject(QDir(runDir).filePath(QStringLiteral("runtime_loop_summary.json"))).value(QStringLiteral("summary")).toObject();
    const QJsonObject host = readJsonObject(QDir(runDir).filePath(QStringLiteral("runtime_host_evidence.json")));
    const QJsonObject timeline = readJsonObject(QDir(runDir).filePath(QStringLiteral("run_timeline_index.json")));
    const QJsonObject sessionSummary = sessionSummaryForRun(runDir);
    const QJsonArray sessions = sessionSummary.value(QStringLiteral("sessions")).toArray();
    detailTitle_->setText(runLabel(evidenceRoot_, runDir));
    detailKv_->clear();
    detailKv_->addRow(QStringLiteral("运行目录"), runDir);
    detailKv_->addRow(QStringLiteral("工作流ID"), dashIfEmpty(snapshot.value(QStringLiteral("workflow_id")).toString(
        timeline.value(QStringLiteral("workflow_id")).toString(host.value(QStringLiteral("workflow_id")).toString()))));
    detailKv_->addRow(QStringLiteral("运行ID"), dashIfEmpty(snapshot.value(QStringLiteral("run_id")).toString(
        timeline.value(QStringLiteral("run_id")).toString(host.value(QStringLiteral("run_id")).toString()))));
    detailKv_->addRow(QStringLiteral("执行后端"), backendForRun(runDir), false);
    detailKv_->addRow(QStringLiteral("适配协议"), protocolsForRun(runDir), false);
    detailKv_->addRow(QStringLiteral("Session 模式"), sessionModesForRun(runDir), false);
    detailKv_->addRow(QStringLiteral("慢路径检查"), slowPathWarningForRun(runDir), false);
    detailKv_->addRow(QStringLiteral("Session 数"), QString::number(sessions.size()));
    detailKv_->addRow(QStringLiteral("节点数"), QString::number(snapshot.value(QStringLiteral("nodes")).toArray().size()));
    detailKv_->addRow(QStringLiteral("调度/运行事件"), QString::number(scheduler.value(QStringLiteral("events")).toArray().size() +
        runtimeEventsJson_.value(QStringLiteral("events")).toArray().size()));
    detailKv_->addRow(QStringLiteral("迭代次数"), QString::number(loop.value(QStringLiteral("iteration_count")).toInt(
        timeline.value(QStringLiteral("summary")).toObject().value(QStringLiteral("branch_step_count")).toInt())));
    detailKv_->addRow(QStringLiteral("停止原因"), dashIfEmpty(loop.value(QStringLiteral("stop_reason")).toString(
        progressJson_.value(QStringLiteral("message")).toString())), false);
    detailKv_->addRow(QStringLiteral("状态检查点"), QFileInfo::exists(QDir(runDir).filePath(QStringLiteral("state_checkpoint.json"))) ? QStringLiteral("存在") : QStringLiteral("缺失"));
    detailKv_->addRow(QStringLiteral("run_timeline_index"), QFileInfo::exists(QDir(runDir).filePath(QStringLiteral("run_timeline_index.json"))) ? QStringLiteral("存在") : QStringLiteral("缺失"));
    detailKv_->addRow(QStringLiteral("runtime_host_evidence"), QFileInfo::exists(QDir(runDir).filePath(QStringLiteral("runtime_host_evidence.json"))) ? QStringLiteral("存在") : QStringLiteral("缺失"));
}

void RuntimeHostPage::showBranch(int row) {
    if (row < 0 || row >= branches_.size()) {
        return;
    }
    const QJsonObject branch = branches_.at(row).toObject();
    const QJsonObject summary = branch.value(QStringLiteral("summary")).toObject();
    const QJsonObject refs = branch.value(QStringLiteral("refs")).toObject();
    detailTitle_->setText(branch.value(QStringLiteral("display_name")).toString(
        branch.value(QStringLiteral("branch_id")).toString()));
    detailKv_->clear();
    detailKv_->addRow(QStringLiteral("branch_id"), branch.value(QStringLiteral("branch_id")).toString());
    detailKv_->addRow(QStringLiteral("类型"), branch.value(QStringLiteral("kind_label")).toString(
        branch.value(QStringLiteral("branch_kind")).toString()), false);
    detailKv_->addRow(QStringLiteral("角色"), joinStringArray(branch.value(QStringLiteral("branch_roles")).toArray()), false);
    detailKv_->addRow(QStringLiteral("状态"), branch.value(QStringLiteral("status")).toString(), false);
    detailKv_->addRow(QStringLiteral("父分支"), dashIfEmpty(branch.value(QStringLiteral("parent_branch_id")).toString()));
    detailKv_->addRow(QStringLiteral("触发帧"), jsonNumberText(branch.value(QStringLiteral("trigger_frame_index"))));
    detailKv_->addRow(QStringLiteral("触发时间"), jsonNumberText(branch.value(QStringLiteral("trigger_time_s"))));
    detailKv_->addRow(QStringLiteral("运行目录"), branch.value(QStringLiteral("run_dir")).toString(), false);
    detailKv_->addRow(QStringLiteral("seed"), dashIfEmpty(branch.value(QStringLiteral("seed_runtime_outputs_ref")).toString()), false);
    detailKv_->addRow(QStringLiteral("步/帧数"), QString::number(summary.value(QStringLiteral("iteration_count")).toInt(
        summary.value(QStringLiteral("step_count")).toInt(summary.value(QStringLiteral("frame_count")).toInt()))));
    detailKv_->addRow(QStringLiteral("artifact"), QString::number(summary.value(QStringLiteral("field_artifact_count")).toInt(
        summary.value(QStringLiteral("artifact_count")).toInt())));
    detailKv_->addRow(QStringLiteral("停止原因"), dashIfEmpty(summary.value(QStringLiteral("stop_reason")).toString()), false);
    detailKv_->addRow(QStringLiteral("branch_control"), dashIfEmpty(refs.value(QStringLiteral("branch_control")).toString()), false);
}

void RuntimeHostPage::showEvent(int row) {
    if (row < 0 || row >= events_.size()) {
        return;
    }
    const QJsonObject event = events_.at(row).toObject();
    const QJsonObject payload = event.value(QStringLiteral("payload")).toObject();
    detailTitle_->setText(event.value(QStringLiteral("event_kind")).toString());
    detailKv_->clear();
    detailKv_->addRow(QStringLiteral("event_id"), event.value(QStringLiteral("event_id")).toString(), false);
    detailKv_->addRow(QStringLiteral("分支"), event.value(QStringLiteral("branch_id")).toString());
    detailKv_->addRow(QStringLiteral("帧"), jsonNumberText(event.value(QStringLiteral("frame_index"))));
    detailKv_->addRow(QStringLiteral("时间"), jsonNumberText(event.value(QStringLiteral("time_s"))));
    detailKv_->addRow(QStringLiteral("生成时间"), event.value(QStringLiteral("generated_at_utc")).toString(), false);
    detailKv_->addRow(QStringLiteral("run_dir"), dashIfEmpty(payload.value(QStringLiteral("run_dir")).toString()), false);
    detailKv_->addRow(QStringLiteral("runtime_outputs"), dashIfEmpty(payload.value(QStringLiteral("runtime_outputs_ref")).toString()), false);
    detailKv_->addRow(QStringLiteral("workflow"), dashIfEmpty(payload.value(QStringLiteral("workflow_id")).toString()));
}

void RuntimeHostPage::showSession(int row) {
    if (row < 0 || row >= sessions_.size()) {
        return;
    }
    const QJsonObject session = sessions_.at(row).toObject();
    const QJsonObject counters = session.value(QStringLiteral("counters")).toObject();
    detailTitle_->setText(session.value(QStringLiteral("node_id")).toString());
    detailKv_->clear();
    detailKv_->addRow(QStringLiteral("adapter_id"), session.value(QStringLiteral("adapter_id")).toString());
    detailKv_->addRow(QStringLiteral("operator_id"), session.value(QStringLiteral("operator_id")).toString());
    detailKv_->addRow(QStringLiteral("protocol"), session.value(QStringLiteral("protocol")).toString(), false);
    detailKv_->addRow(QStringLiteral("session_mode"), session.value(QStringLiteral("session_mode")).toString(), false);
    detailKv_->addRow(QStringLiteral("prepared"), boolText(session.value(QStringLiteral("prepared"))));
    detailKv_->addRow(QStringLiteral("handle_created"), boolText(session.value(QStringLiteral("handle_created"))));
    detailKv_->addRow(QStringLiteral("initialize"), QString::number(counters.value(QStringLiteral("initialize")).toInt()));
    detailKv_->addRow(QStringLiteral("execute"), QString::number(counters.value(QStringLiteral("execute")).toInt()));
    detailKv_->addRow(QStringLiteral("flush"), QString::number(counters.value(QStringLiteral("flush")).toInt()));
    detailKv_->addRow(QStringLiteral("snapshot"), QString::number(counters.value(QStringLiteral("snapshot")).toInt()));
    detailKv_->addRow(QStringLiteral("library"), session.value(QStringLiteral("library")).toString(), false);
}

} // namespace twin
