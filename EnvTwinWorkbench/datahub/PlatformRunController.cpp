#include "PlatformRunController.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStringList>
#include <QTimer>

#include <utility>

namespace twin {

namespace {

QJsonObject readJsonObject(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return {};
    }
    return doc.object();
}

void appendUtf8Path(QProcessEnvironment& env, const QString& path) {
    const QString clean = QDir::toNativeSeparators(QDir::cleanPath(path));
    if (!QFileInfo::exists(clean)) {
        return;
    }
    const QString oldPath = env.value(QStringLiteral("Path"), env.value(QStringLiteral("PATH")));
    if (oldPath.contains(clean, Qt::CaseInsensitive)) {
        return;
    }
    env.insert(QStringLiteral("Path"), clean + QStringLiteral(";") + oldPath);
}

QString firstExistingFile(const QStringList& candidates) {
    for (const QString& candidate : candidates) {
        const QString path = QDir::fromNativeSeparators(candidate);
        if (QFileInfo::exists(path) && QFileInfo(path).isFile()) {
            return QFileInfo(path).absoluteFilePath();
        }
    }
    return QString();
}

QStringList jsonStringList(const QJsonValue& value) {
    QStringList out;
    for (const QJsonValue& item : value.toArray()) {
        const QString text = item.toString();
        if (!text.isEmpty()) {
            out.push_back(text);
        }
    }
    return out;
}

QJsonObject loadObjectRuntimeProfile(const QString& objectPackageRoot) {
    const QDir objectDir(objectPackageRoot);
    const QString manifestPath = objectDir.filePath(QStringLiteral("object/twin_object.json"));
    const QJsonObject manifest = readJsonObject(manifestPath);
    QString profileRef = manifest.value(QStringLiteral("platform_runtime_profile")).toString();
    if (profileRef.isEmpty()) {
        profileRef = manifest.value(QStringLiteral("platform_runtime")).toObject()
                         .value(QStringLiteral("profile_path")).toString();
    }
    QString profilePath;
    if (profileRef.isEmpty()) {
        profilePath = objectDir.filePath(QStringLiteral("runtime/platform_runtime_profile.json"));
    } else if (QFileInfo(profileRef).isAbsolute()) {
        profilePath = profileRef;
    } else {
        profilePath = QDir(QFileInfo(manifestPath).absolutePath()).filePath(profileRef);
    }
    QJsonObject profile = readJsonObject(QDir::cleanPath(profilePath));
    if (!profile.isEmpty()) {
        profile.insert(QStringLiteral("_profile_path"), QFileInfo(profilePath).absoluteFilePath());
    }
    return profile;
}

QJsonObject workflowRole(const QJsonObject& profile, const QString& role) {
    for (const QJsonValue& itemValue : profile.value(QStringLiteral("workflow_roles")).toArray()) {
        const QJsonObject item = itemValue.toObject();
        if (item.value(QStringLiteral("role")).toString() == role) {
            return item;
        }
    }
    return {};
}

QJsonObject workflowRoleForWorkflowId(const QJsonObject& profile, const QString& workflowId) {
    for (const QJsonValue& itemValue : profile.value(QStringLiteral("workflow_roles")).toArray()) {
        const QJsonObject item = itemValue.toObject();
        if (item.value(QStringLiteral("workflow_id")).toString() == workflowId) {
            return item;
        }
    }
    return {};
}

QString workflowIdForRole(const QJsonObject& profile, const QString& role) {
    const QJsonObject item = workflowRole(profile, role);
    const QString workflowId = item.value(QStringLiteral("workflow_id")).toString();
    if (!workflowId.isEmpty()) {
        return workflowId;
    }
    const QJsonObject defaults = profile.value(QStringLiteral("default_runtime_host")).toObject();
    if (role == QStringLiteral("online_mainline")) {
        return defaults.value(QStringLiteral("online_workflow_id")).toString();
    }
    if (role == QStringLiteral("future_prediction")) {
        return defaults.value(QStringLiteral("future_workflow_id")).toString();
    }
    return {};
}

QString profileIdForRole(const QJsonObject& profile, const QString& role) {
    const QJsonObject roleItem = workflowRole(profile, role);
    QString profileId = roleItem.value(QStringLiteral("default_profile_id")).toString();
    if (profileId.isEmpty()) {
        profileId = roleItem.value(QStringLiteral("profile_id")).toString();
    }
    if (!profileId.isEmpty()) {
        return profileId;
    }
    const QJsonObject defaults = profile.value(QStringLiteral("default_runtime_host")).toObject();
    if (role == QStringLiteral("online_mainline")) {
        return defaults.value(QStringLiteral("online_profile_id")).toString();
    }
    if (role == QStringLiteral("future_prediction")) {
        return defaults.value(QStringLiteral("future_profile_id")).toString();
    }
    return {};
}

QString resolveObjectProfilePath(
    const QString& workspaceRoot,
    const QString& objectPackageRoot,
    const QString& ref) {
    if (ref.trimmed().isEmpty()) {
        return {};
    }
    const QString cleanRef = QDir::fromNativeSeparators(ref.trimmed());
    if (QFileInfo(cleanRef).isAbsolute()) {
        return QFileInfo(cleanRef).absoluteFilePath();
    }
    if (cleanRef.startsWith(QStringLiteral("_local_artifacts")) ||
        cleanRef.startsWith(QStringLiteral("_deps"))) {
        return QDir(workspaceRoot).filePath(cleanRef);
    }
    return QDir(objectPackageRoot).filePath(cleanRef);
}

QString firstExistingProfileFile(
    const QJsonObject& profile,
    const QString& workspaceRoot,
    const QString& objectPackageRoot,
    const QString& key) {
    for (const QString& ref : jsonStringList(profile.value(key))) {
        const QString candidate = resolveObjectProfilePath(workspaceRoot, objectPackageRoot, ref);
        if (QFileInfo::exists(candidate) && QFileInfo(candidate).isFile()) {
            return QFileInfo(candidate).absoluteFilePath();
        }
    }
    return {};
}

QString compiledWorkflowRootFromProfile(const QJsonObject& profile, const QString& workspaceRoot) {
    QString ref = profile.value(QStringLiteral("default_runtime_host")).toObject()
                      .value(QStringLiteral("compiled_workflow_root")).toString();
    if (ref.isEmpty()) {
        ref = QStringLiteral("_local_artifacts/platform-pdk/compiled-workflows");
    }
    if (QFileInfo(ref).isAbsolute()) {
        return QFileInfo(ref).absoluteFilePath();
    }
    return QDir(workspaceRoot).filePath(QDir::fromNativeSeparators(ref));
}

QString resolveControllerRuntimeSnapshotPath(const QString& workspaceRoot, const QString& objectPackageRoot) {
    Q_UNUSED(workspaceRoot);
    const QDir objectDir(objectPackageRoot);
    return firstExistingFile({
        objectDir.filePath(QStringLiteral("runtime_snapshot.json")),
        objectDir.filePath(QStringLiteral("assets/runtime_snapshot.json"))
    });
}

QString resolveAdapterRegistryPath(const QString& workspaceRoot, const QString& objectPackageRoot) {
    const QJsonObject profile = loadObjectRuntimeProfile(objectPackageRoot);
    const QString profilePath =
        firstExistingProfileFile(profile, workspaceRoot, objectPackageRoot, QStringLiteral("adapter_registry_candidates"));
    if (!profilePath.isEmpty()) {
        return profilePath;
    }
    const QDir objectDir(objectPackageRoot);
    return firstExistingFile({
        objectDir.filePath(QStringLiteral("adapters/adapter_registry.json")),
        objectDir.filePath(QStringLiteral("tools/adapter_registries/adapter_registry.json"))
    });
}

QString resolveExternalObservationStreamPath(const QString& workspaceRoot, const QString& objectPackageRoot) {
    const QDir objectDir(objectPackageRoot);
    const QJsonObject profile = loadObjectRuntimeProfile(objectPackageRoot);
    const QString profilePath =
        firstExistingProfileFile(profile, workspaceRoot, objectPackageRoot, QStringLiteral("external_observation_stream_candidates"));
    if (!profilePath.isEmpty()) {
        return profilePath;
    }
    return firstExistingFile({
        objectDir.filePath(QStringLiteral("fixtures/sensor_stream_db70.json")),
        objectDir.filePath(QStringLiteral("fixtures/sensor_stream.json")),
        objectDir.filePath(QStringLiteral("fixtures/external_observation_stream.json")),
        objectDir.filePath(QStringLiteral("assets/sensor_stream.json"))
    });
}

void copyRuntimeSnapshotForUi(const QString& sourcePath, const QString& evidenceRoot) {
    if (sourcePath.isEmpty() || !QFileInfo::exists(sourcePath)) {
        return;
    }
    const QString target = QDir(evidenceRoot).filePath(QStringLiteral("runtime_snapshot.json"));
    QFile::remove(target);
    QFile::copy(sourcePath, target);
}

} // namespace

PlatformRunController::PlatformRunController(
    QString workspaceRoot,
    QString objectPackageRoot,
    QObject* parent)
    : QObject(parent),
      workspaceRoot_(QDir::fromNativeSeparators(std::move(workspaceRoot))),
      objectPackageRoot_(QDir::fromNativeSeparators(std::move(objectPackageRoot))) {
    progressTimer_ = new QTimer(this);
    progressTimer_->setInterval(500);
    connect(progressTimer_, &QTimer::timeout, this, &PlatformRunController::pollProgress);
}

bool PlatformRunController::isRunning() const {
    return process_ && process_->state() != QProcess::NotRunning;
}

void PlatformRunController::setObjectPackageRoot(const QString& objectPackageRoot) {
    objectPackageRoot_ = QDir::fromNativeSeparators(objectPackageRoot);
}

void PlatformRunController::prepareDefaultMainline() {
    const QString workflowId = defaultWorkflowId();
    startWorkflowInternal(true, workflowId, defaultProfileId(workflowId));
}

void PlatformRunController::startDefaultMainline() {
    const QString workflowId = defaultWorkflowId();
    startWorkflowInternal(false, workflowId, defaultProfileId(workflowId));
}

void PlatformRunController::prepareWorkflow(const QString& workflowId, const QString& profileId) {
    startWorkflowInternal(true, workflowId, profileId);
}

void PlatformRunController::startWorkflow(const QString& workflowId, const QString& profileId) {
    startWorkflowInternal(false, workflowId, profileId);
}

void PlatformRunController::stopCurrentRun() {
    pendingSteps_.clear();
    watchDetachedBranches_ = false;
    if (!isRunning()) {
        emit statusChanged(QStringLiteral("idle"), QStringLiteral("No active Runtime Host process"));
        return;
    }
    emitSyntheticProgress(
        QStringLiteral("stopping"),
        QStringLiteral("running"),
        QStringLiteral("Stopping current Runtime Host process"),
        0.0);
    process_->terminate();
    if (!process_->waitForFinished(2000)) {
        process_->kill();
    }
}

void PlatformRunController::startMainline(bool prepareOnly) {
    const QString workflowId = defaultWorkflowId();
    startWorkflowInternal(prepareOnly, workflowId, defaultProfileId(workflowId));
}
QString PlatformRunController::defaultWorkflowId() const {
    const QJsonObject runtimeProfile = loadObjectRuntimeProfile(objectPackageRoot_);
    const QString profileWorkflowId = workflowIdForRole(runtimeProfile, QStringLiteral("online_mainline"));
    if (!profileWorkflowId.isEmpty()) {
        return profileWorkflowId;
    }
    const QJsonObject manifest =
        readJsonObject(QDir(objectPackageRoot_).filePath(QStringLiteral("object/twin_object.json")));
    QString firstWorkflow;
    for (const QJsonValue& value : manifest.value(QStringLiteral("workflows")).toArray()) {
        const QString workflowId = value.toObject().value(QStringLiteral("workflow_id")).toString();
        if (firstWorkflow.isEmpty()) {
            firstWorkflow = workflowId;
        }
    }
    return firstWorkflow.isEmpty() ? QStringLiteral("all") : firstWorkflow;
}

QString PlatformRunController::defaultProfileId(const QString& workflowId) const {
    const QJsonObject runtimeProfile = loadObjectRuntimeProfile(objectPackageRoot_);
    const QJsonObject role = workflowRoleForWorkflowId(runtimeProfile, workflowId);
    if (!role.isEmpty()) {
        QString roleProfile = role.value(QStringLiteral("default_profile_id")).toString();
        if (roleProfile.isEmpty()) {
            roleProfile = role.value(QStringLiteral("profile_id")).toString();
        }
        if (!roleProfile.isEmpty()) {
            return roleProfile;
        }
    }
    const QString onlineWorkflowId = workflowIdForRole(runtimeProfile, QStringLiteral("online_mainline"));
    if (!onlineWorkflowId.isEmpty() && workflowId == onlineWorkflowId) {
        const QString onlineProfile = profileIdForRole(runtimeProfile, QStringLiteral("online_mainline"));
        if (!onlineProfile.isEmpty()) {
            return onlineProfile;
        }
    }
    const QString futureWorkflowId = workflowIdForRole(runtimeProfile, QStringLiteral("future_prediction"));
    if (!futureWorkflowId.isEmpty() && workflowId == futureWorkflowId) {
        const QString futureProfile = profileIdForRole(runtimeProfile, QStringLiteral("future_prediction"));
        if (!futureProfile.isEmpty()) {
            return futureProfile;
        }
    }
    return QString();
}

QString PlatformRunController::stableWorkflowDirName(const QString& workflowId) const {
    QString out;
    out.reserve(workflowId.size());
    for (const QChar ch : workflowId) {
        out.push_back(ch.isLetterOrNumber() || ch == QLatin1Char('_') ||
                          ch == QLatin1Char('.') || ch == QLatin1Char('-')
                      ? ch
                      : QLatin1Char('_'));
    }
    while (out.startsWith(QLatin1Char('_'))) {
        out.remove(0, 1);
    }
    while (out.endsWith(QLatin1Char('_'))) {
        out.chop(1);
    }
    return out.isEmpty() ? QStringLiteral("unnamed") : out;
}

void PlatformRunController::enqueueStep(
    QString stage,
    QString message,
    QString program,
    QStringList arguments) {
    pendingSteps_.push_back(RunStep{
        std::move(stage),
        std::move(message),
        std::move(program),
        std::move(arguments)});
}

void PlatformRunController::startWorkflowInternal(
    bool prepareOnly,
    const QString& workflowId,
    const QString& profileId) {
    if (isRunning() || watchDetachedBranches_) {
        emit statusChanged(QStringLiteral("running"), QStringLiteral("Runtime Host is already running"));
        return;
    }
    if (objectPackageRoot_.isEmpty() ||
        !QFileInfo::exists(QDir(objectPackageRoot_).filePath(QStringLiteral("object/twin_object.json"))) ||
        !QFileInfo::exists(QDir(objectPackageRoot_).filePath(QStringLiteral("assets/resources.json")))) {
        emitSyntheticProgress(
            QStringLiteral("failed"),
            QStringLiteral("failed"),
            QStringLiteral("Please load a valid object package first"),
            0.0);
        return;
    }

    const QJsonObject manifest =
        readJsonObject(QDir(objectPackageRoot_).filePath(QStringLiteral("object/twin_object.json")));
    const QJsonObject runtimeProfile = loadObjectRuntimeProfile(objectPackageRoot_);
    const QString requestedWorkflowId = workflowId.trimmed().isEmpty() ? defaultWorkflowId() : workflowId.trimmed();
    QString runtimeOnlineWorkflowId = workflowIdForRole(runtimeProfile, QStringLiteral("online_mainline"));
    QString runtimeFutureWorkflowId = workflowIdForRole(runtimeProfile, QStringLiteral("future_prediction"));
    const QJsonObject requestedRole = workflowRoleForWorkflowId(runtimeProfile, requestedWorkflowId);
    const QString requestedBranchKind = requestedRole.value(QStringLiteral("branch_kind")).toString();
    const bool profileRuntimeWorkflow =
        requestedWorkflowId == runtimeOnlineWorkflowId ||
        requestedWorkflowId == runtimeFutureWorkflowId ||
        requestedBranchKind == QStringLiteral("online_mainline") ||
        requestedBranchKind == QStringLiteral("future_prediction");
    const bool useCppRuntimeHostChain =
        !runtimeOnlineWorkflowId.isEmpty() &&
        !runtimeFutureWorkflowId.isEmpty() &&
        profileRuntimeWorkflow;

    activeWorkflowId_ = useCppRuntimeHostChain ? runtimeOnlineWorkflowId : requestedWorkflowId;
    activeProfileId_ = profileId.trimmed();
    if (activeProfileId_.isEmpty()) {
        activeProfileId_ = defaultProfileId(activeWorkflowId_);
    }
    activeRunId_ = prepareOnly
        ? QStringLiteral("ui_prepare_%1").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")))
        : makeRunId();
    activeEvidenceRoot_ = QDir(workspaceRoot_).filePath(
        QStringLiteral("_local_artifacts/platform-pdk/mainline-runs/%1").arg(activeRunId_));
    progressPath_ = QDir(activeEvidenceRoot_).filePath(QStringLiteral("mainline_progress.json"));
    lastProgressMtime_ = -1;
    watchDetachedBranches_ = false;
    pendingSteps_.clear();

    const QString pdkRoot = QDir(workspaceRoot_).filePath(QStringLiteral("flightenv-platform-pdk"));
    const QString cliPath = QDir(pdkRoot).filePath(QStringLiteral("tools/flightenv-platform-cli/flightenv-platform-cli.py"));
    const QString compiledRoot = compiledWorkflowRootFromProfile(runtimeProfile, workspaceRoot_);
    const QString compiledDir = QDir(compiledRoot).filePath(stableWorkflowDirName(activeWorkflowId_));
    const QString adapterRegistry = resolveAdapterRegistryPath(workspaceRoot_, objectPackageRoot_);
    const QString runtimeSnapshotPath = resolveControllerRuntimeSnapshotPath(workspaceRoot_, objectPackageRoot_);
    const QString externalObservationStream = resolveExternalObservationStreamPath(workspaceRoot_, objectPackageRoot_);

    QDir().mkpath(activeEvidenceRoot_);
    copyRuntimeSnapshotForUi(runtimeSnapshotPath, activeEvidenceRoot_);
    emit evidenceRootChanged(activeEvidenceRoot_);
    emitSyntheticProgress(
        QStringLiteral("validate_object_package"),
        QStringLiteral("running"),
        QStringLiteral("Validating object package and preparing workflow run"),
        2.0);

    enqueueStep(
        QStringLiteral("validate_object_package"),
        QStringLiteral("Validate object package"),
        QStringLiteral("python"),
        {cliPath,
         QStringLiteral("validate"),
         QStringLiteral("object-package"),
         QStringLiteral("--root"),
         pdkRoot,
         QStringLiteral("--object-package"),
         objectPackageRoot_});

    if (useCppRuntimeHostChain) {
        QString onlineProfile = profileIdForRole(runtimeProfile, QStringLiteral("online_mainline"));
        QString futureProfile = profileIdForRole(runtimeProfile, QStringLiteral("future_prediction"));
        const QString compiledOnlineDir = QDir(compiledRoot).filePath(stableWorkflowDirName(runtimeOnlineWorkflowId));
        const QString compiledFutureDir = QDir(compiledRoot).filePath(stableWorkflowDirName(runtimeFutureWorkflowId));

        QStringList compileOnlineArgs{
            cliPath,
            QStringLiteral("compile"),
            QStringLiteral("workflow"),
            QStringLiteral("--root"),
            pdkRoot,
            QStringLiteral("--object-package"),
            objectPackageRoot_,
            QStringLiteral("--workflow"),
            runtimeOnlineWorkflowId,
            QStringLiteral("--out-dir"),
            compiledRoot,
            QStringLiteral("--run-id"),
            activeRunId_ + QStringLiteral(".compile.online"),
            QStringLiteral("--profile"),
            onlineProfile
        };
        enqueueStep(QStringLiteral("compile_online_workflow"), QStringLiteral("Compile online workflow"), QStringLiteral("python"), compileOnlineArgs);

        QStringList compileFutureArgs{
            cliPath,
            QStringLiteral("compile"),
            QStringLiteral("workflow"),
            QStringLiteral("--root"),
            pdkRoot,
            QStringLiteral("--object-package"),
            objectPackageRoot_,
            QStringLiteral("--workflow"),
            runtimeFutureWorkflowId,
            QStringLiteral("--out-dir"),
            compiledRoot,
            QStringLiteral("--run-id"),
            activeRunId_ + QStringLiteral(".compile.future"),
            QStringLiteral("--profile"),
            futureProfile
        };
        enqueueStep(QStringLiteral("compile_future_workflow"), QStringLiteral("Compile future prediction workflow"), QStringLiteral("python"), compileFutureArgs);

        const QString runtimeHostExe = QDir(workspaceRoot_).filePath(
            QStringLiteral("_deps/workspace/x64/Release/FlightEnvPlatformRuntimeHost.exe"));
        const QString runRoot = QDir(workspaceRoot_).filePath(QStringLiteral("_local_artifacts/platform-pdk/runtime-host-runs"));
        QStringList hostArgs{
            QStringLiteral("--workspace-root"), workspaceRoot_,
            QStringLiteral("--pdk-root"), pdkRoot,
            QStringLiteral("--object-package-root"), objectPackageRoot_,
            QStringLiteral("--compiled-online"), compiledOnlineDir,
            QStringLiteral("--compiled-future"), compiledFutureDir,
            QStringLiteral("--adapter-registry"), adapterRegistry,
            QStringLiteral("--run-id-prefix"), activeRunId_,
            QStringLiteral("--run-root"), runRoot,
            QStringLiteral("--chain-dir"), activeEvidenceRoot_,
            QStringLiteral("--python"), QStringLiteral("python"),
            QStringLiteral("--execution-backend"), QStringLiteral("native_adapter_sessions"),
            QStringLiteral("--branch-chunk-iterations"), QStringLiteral("1")
        };
        if (!prepareOnly) {
            hostArgs << QStringLiteral("--external-observation-stream") << externalObservationStream;
            hostArgs << QStringLiteral("--replay-by-platform-clock") << QStringLiteral("--replay-time-scale") << QStringLiteral("1.0");
            hostArgs << QStringLiteral("--wait-for-branches");
        } else {
            hostArgs << QStringLiteral("--prepare-only") << QStringLiteral("--preflight-adapters");
        }
        emit logLine(QStringLiteral("Runtime Host exe: %1").arg(runtimeHostExe));
        emit logLine(QStringLiteral("Adapter registry: %1").arg(adapterRegistry));
        emit logLine(QStringLiteral("Runtime snapshot for VTK: %1").arg(runtimeSnapshotPath));
        if (!prepareOnly) {
            emit logLine(QStringLiteral("External observation stream: %1").arg(externalObservationStream));
        }
        enqueueStep(
            prepareOnly ? QStringLiteral("prepare_runtime_host") : QStringLiteral("run_runtime_host"),
            prepareOnly ? QStringLiteral("Prepare C++ Runtime Host adapters") : QStringLiteral("Run C++ Runtime Host online + prediction chain"),
            runtimeHostExe,
            hostArgs);
    } else {
        QStringList compileArgs{
            cliPath,
            QStringLiteral("compile"),
            QStringLiteral("workflow"),
            QStringLiteral("--root"),
            pdkRoot,
            QStringLiteral("--object-package"),
            objectPackageRoot_,
            QStringLiteral("--workflow"),
            activeWorkflowId_,
            QStringLiteral("--out-dir"),
            compiledRoot,
            QStringLiteral("--run-id"),
            activeRunId_
        };
        if (!activeProfileId_.isEmpty()) {
            compileArgs << QStringLiteral("--profile") << activeProfileId_;
        }
        enqueueStep(QStringLiteral("compile_workflow"), QStringLiteral("Compile workflow"), QStringLiteral("python"), compileArgs);

        QStringList runArgs{
            cliPath,
            QStringLiteral("run"),
            QStringLiteral("compiled-workflow"),
            QStringLiteral("--root"),
            pdkRoot,
            QStringLiteral("--compiled-workflow"),
            compiledDir,
            QStringLiteral("--run-dir"),
            activeEvidenceRoot_,
            QStringLiteral("--run-id"),
            activeRunId_,
            QStringLiteral("--controller-ui-compat-dir"),
            activeEvidenceRoot_
        };
        if (prepareOnly) {
            runArgs << QStringLiteral("--prepare-only");
        } else {
            runArgs << QStringLiteral("--runtime-max-iterations") << QStringLiteral("120");
        }
        if (!adapterRegistry.isEmpty() && QFileInfo::exists(adapterRegistry)) {
            runArgs << QStringLiteral("--adapter-registry") << adapterRegistry;
            emit logLine(QStringLiteral("Adapter registry: %1").arg(adapterRegistry));
        } else {
            emit logLine(QStringLiteral("Adapter registry not found; Runtime Host will fall back to recording adapters"));
        }
        if (!runtimeSnapshotPath.isEmpty()) {
            runArgs << QStringLiteral("--controller-ui-runtime-snapshot") << runtimeSnapshotPath;
            emit logLine(QStringLiteral("Runtime snapshot for VTK: %1").arg(runtimeSnapshotPath));
        }
        enqueueStep(
            prepareOnly ? QStringLiteral("prepare_runtime_host") : QStringLiteral("run_runtime_host"),
            prepareOnly ? QStringLiteral("Prepare Runtime Host adapters") : QStringLiteral("Run compiled workflow"),
            QStringLiteral("python"),
            runArgs);
    }

    progressTimer_->start();
    startNextStep();
}

void PlatformRunController::startNextStep() {
    if (pendingSteps_.isEmpty()) {
        progressTimer_->stop();
        emitSyntheticProgress(
            QStringLiteral("completed"),
            QStringLiteral("completed"),
            QStringLiteral("Runtime Host workflow finished"),
            100.0);
        emit runFinished(0, QStringLiteral("completed"));
        return;
    }

    const RunStep step = pendingSteps_.takeFirst();
    currentStepStage_ = step.stage;
    emitSyntheticProgress(step.stage, QStringLiteral("running"), step.message, 10.0);

    auto* proc = new QProcess(this);
    process_ = proc;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
    appendUtf8Path(env, QDir(workspaceRoot_).filePath(QStringLiteral("_deps/workspace/x64/Release")));
    appendUtf8Path(env, QDir(workspaceRoot_).filePath(QStringLiteral("_deps/bin/legacy_support_runtime")));
    appendUtf8Path(env, QDir(workspaceRoot_).filePath(QStringLiteral("_deps/bin/third_party_runtime")));
    proc->setProcessEnvironment(env);
    proc->setWorkingDirectory(workspaceRoot_);
    proc->setProgram(step.program);
    proc->setArguments(step.arguments);
    connect(proc, &QProcess::readyReadStandardOutput, this, &PlatformRunController::readProcessOutput);
    connect(proc, &QProcess::readyReadStandardError, this, &PlatformRunController::readProcessOutput);
    connect(proc, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &PlatformRunController::onProcessFinished);

    proc->start();
    if (!proc->waitForStarted(1000)) {
        emitSyntheticProgress(
            QStringLiteral("failed"),
            QStringLiteral("failed"),
            QStringLiteral("Failed to start backend process: %1").arg(proc->errorString()),
            0.0);
        proc->deleteLater();
        process_ = nullptr;
        pendingSteps_.clear();
        progressTimer_->stop();
    }
}

void PlatformRunController::readProcessOutput() {
    if (!process_) {
        return;
    }
    const QByteArray raw = process_->readAllStandardOutput() + process_->readAllStandardError();
    // 把 host/CLI 的 stdout+stderr 落盘到 run 目录的 host_runner.log——C++ Host 崩溃时
    // 这里是唯一能拿到真实异常/堆栈的地方（progress/evidence 只记到“失败”不记原因）。
    if (!raw.isEmpty() && !activeEvidenceRoot_.isEmpty()) {
        QDir().mkpath(activeEvidenceRoot_);
        QFile log(QDir(activeEvidenceRoot_).filePath(QStringLiteral("host_runner.log")));
        if (log.open(QIODevice::Append | QIODevice::WriteOnly)) {
            log.write(raw);
            log.close();
        }
    }
    const QString text = QString::fromUtf8(raw);
    for (QString line : text.split(QLatin1Char('\n'))) {
        line = line.trimmed();
        if (!line.isEmpty()) {
            emit logLine(line);
        }
    }
}

void PlatformRunController::pollProgress() {
    const QJsonObject progress = readProgressFile();
    if (!progress.isEmpty()) {
        emit progressUpdated(progress);
        emit statusChanged(
            progress.value(QStringLiteral("status")).toString(),
            progress.value(QStringLiteral("message")).toString());
        if (watchDetachedBranches_ && !isRunning()) {
            const QString status = progress.value(QStringLiteral("status")).toString();
            const QString stage = progress.value(QStringLiteral("stage")).toString();
            const bool branchStillRunning =
                status == QStringLiteral("mainline_completed_branch_running") ||
                status == QStringLiteral("running") ||
                stage == QStringLiteral("prediction_running") ||
                stage == QStringLiteral("prediction_branch_live");
            if (!branchStillRunning) {
                watchDetachedBranches_ = false;
                progressTimer_->stop();
                emit runFinished(status == QStringLiteral("failed") ? 2 : 0,
                                 status.isEmpty() ? QStringLiteral("completed") : status);
            }
        }
    }
}

void PlatformRunController::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    readProcessOutput();
    pollProgress();
    progressTimer_->stop();
    const bool ok = exitCode == 0 && exitStatus == QProcess::NormalExit;
    if (process_) {
        process_->deleteLater();
        process_ = nullptr;
    }
    if (ok && !pendingSteps_.isEmpty()) {
        progressTimer_->start();
        startNextStep();
        return;
    }
    if (ok && pendingSteps_.isEmpty()) {
        const QJsonObject progress = readJsonObject(progressPath_);
        const QString status = progress.value(QStringLiteral("status")).toString();
        const QString stage = progress.value(QStringLiteral("stage")).toString();
        if (status == QStringLiteral("mainline_completed_branch_running") ||
            stage == QStringLiteral("prediction_running") ||
            stage == QStringLiteral("prediction_branch_live")) {
            watchDetachedBranches_ = true;
            progressTimer_->start();
            emit statusChanged(status,
                               progress.value(QStringLiteral("message")).toString(
                                   QStringLiteral("预测分支仍在后台运行")));
            return;
        }
    }
    const bool hasFinalProgress = !readJsonObject(progressPath_).isEmpty();
    if (ok && !hasFinalProgress) {
        emitSyntheticProgress(
            QStringLiteral("completed"),
            QStringLiteral("completed"),
            QStringLiteral("平台在线仿真主链路完成"),
            100.0);
    } else if (!ok) {
        emitSyntheticProgress(
            QStringLiteral("failed"),
            QStringLiteral("failed"),
            QStringLiteral("平台在线仿真主链路失败，退出码=%1").arg(exitCode),
            0.0);
    }
    emit runFinished(exitCode, ok ? QStringLiteral("completed") : QStringLiteral("failed"));

    pendingSteps_.clear();
}

QString PlatformRunController::makeRunId() const {
    return QStringLiteral("ui_live_%1").arg(
        QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
}

QJsonObject PlatformRunController::readProgressFile() {
    QFileInfo info(progressPath_);
    if (!info.exists() || !info.isFile()) {
        return {};
    }
    const qint64 mtime = info.lastModified().toMSecsSinceEpoch();
    if (mtime == lastProgressMtime_) {
        return {};
    }
    lastProgressMtime_ = mtime;
    return readJsonObject(progressPath_);
}

void PlatformRunController::emitSyntheticProgress(
    const QString& stage,
    const QString& status,
    const QString& message,
    double percent) {
    QJsonObject progress;
    progress.insert(QStringLiteral("schema_version"), QStringLiteral("flightenv.platform.mainline_progress.v1"));
    progress.insert(QStringLiteral("run_id_prefix"), activeRunId_);
    const QJsonObject manifest =
        readJsonObject(QDir(objectPackageRoot_).filePath(QStringLiteral("object/twin_object.json")));
    progress.insert(QStringLiteral("object_id"),
                    manifest.value(QStringLiteral("object_id")).toString(QFileInfo(objectPackageRoot_).fileName()));
    progress.insert(QStringLiteral("workflow_id"), activeWorkflowId_);
    progress.insert(QStringLiteral("run_profile_id"), activeProfileId_);
    progress.insert(QStringLiteral("stage"), stage);
    progress.insert(QStringLiteral("status"), status);
    progress.insert(QStringLiteral("message"), message);
    progress.insert(QStringLiteral("total_progress_percent"), percent);
    progress.insert(QStringLiteral("mainline_dir"), activeEvidenceRoot_);
    emit progressUpdated(progress);
    emit statusChanged(status, message);
}

} // namespace twin
