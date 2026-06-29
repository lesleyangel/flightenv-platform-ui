#include "EnvPredictorUI.h"
#include <QCloseEvent>
#include "EnvPredictorUiHelpers.h"
#include <QTreeView>
#include <QStandardItemModel>
#include <QComboBox>
#include <QCheckBox>
#include <QStyledItemDelegate>
#include <QStandardItem>
#include <QTreeWidgetItem>
#include <QTabWidget>
#include <QSplitter>
#include <QGroupBox>
#include <QBoxLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QTableWidget>
#include <QHeaderView>
#include <QAbstractItemView>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QRunnable>
#include <QVariant>
#include <QVariantMap>
#include <QMap>
#include <QItemSelectionModel>
#include <QTableWidgetItem>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QSlider>
#include <QSignalBlocker>
#include <QDialogButtonBox>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QScrollArea>
#include <QThreadPool>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QDebug>
#include <QVBoxLayout>
#include <QSizePolicy>
#include <QGroupBox>
#include <QRandomGenerator>
#include <QtCharts>
#include <QColor>
#include <vector>
#include <stdexcept>
#include <vtkActor2D.h>

#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QIcon>
#include <QPixmap>
#include <QDebug>

#define MODELSTLDIR "..//example//"
#define MODELFLIGHTSTLDIR "..//data//surf_mesh.stl"
#define MODELINNERSTLDIR "..//data//in_mesh.stl"

#include <iostream>
#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <set>
#include <sstream>
#include <tuple>
#include <utility>
#include <vector>
#include <map>
#include <memory>
#include <filesystem>

#include "EnvNodeSupport/GraphRunEvidenceReader.h"
#include "EnvNodeSupport/PlatformCatalogReader.h"
#include "module-demos/common/GraphWorkflowDisplayWidgets.h"
#include "module-demos/common/VtkModelFieldWidget.h"
#include "EnvContracts/dto/DamageForecastFrame.hpp"
#include "EnvContracts/dto/FieldForecastFrame.hpp"
#include "EnvContracts/dto/LifeAssessmentFrame.hpp"
#include "EnvContracts/dto/RuntimeSnapshotDTO.hpp"
#include "EnvContracts/dto/TrajectoryPredictionFrame.hpp"

namespace flightenv::platform_ui::internal {

namespace {

void platformUiLogImpl(google::LogSeverity severity, const char* format, va_list args)
{
    char buffer[2048] = {};
    const int written = std::vsnprintf(buffer, sizeof(buffer), format ? format : "", args);
    const char* message = written >= 0 ? buffer : "platform ui log formatting failed";
    google::LogMessage(__FILE__, __LINE__, severity).stream() << message;
}

} // namespace

void platformUiLogInfo(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    platformUiLogImpl(google::GLOG_INFO, format, args);
    va_end(args);
}

void platformUiLogWarning(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    platformUiLogImpl(google::GLOG_WARNING, format, args);
    va_end(args);
}

void platformUiLogError(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    platformUiLogImpl(google::GLOG_ERROR, format, args);
    va_end(args);
}

QString workspacePath(const QString& relativePath);
QJsonObject readJsonObject(const QString& path);
QString displayMetricLabel(const QString& label);
QString displayMetricUnit(const QString& label, const QString& unit);
bool shouldDisplaySeriesMetric(const QString& label);
bool isDisplayableScalarValue(const QJsonValue& value);
QString scalarValueText(const QJsonValue& value);
QString canonicalMetricLabel(const QString& label);
QJsonObject platformBallisticPreview(const QJsonObject& frame);

bool controllerBackendCanRun()
{
    return true;
}

QString platformPathText(const std::filesystem::path& path)
{
    return QString::fromStdString(path.string());
}

QString lowerText(QString text)
{
    return text.toLower();
}

bool containsAnyToken(const QString& text, const QStringList& tokens)
{
    const QString lower = lowerText(text);
    for (const QString& token : tokens) {
        if (!token.isEmpty() && lower.contains(token)) {
            return true;
        }
    }
    return false;
}

QJsonValue firstAvailableValue(
    const std::vector<std::pair<QJsonObject, QStringList>>& sources)
{
    for (const auto& source : sources) {
        const QJsonObject& object = source.first;
        for (const QString& key : source.second) {
            const QJsonValue value = object.value(key);
            if (!value.isUndefined() && !value.isNull()) {
                return value;
            }
        }
    }
    return {};
}

void copyIfAvailable(QJsonObject& target,
                     const QString& targetKey,
                     const std::vector<std::pair<QJsonObject, QStringList>>& sources)
{
    const QJsonValue value = firstAvailableValue(sources);
    if (!value.isUndefined() && !value.isNull()) {
        target.insert(targetKey, value);
    }
}

QJsonObject platformBallisticPreview(const QJsonObject& frame)
{
    const QJsonObject filterPreview = frame.value(QStringLiteral("filter"))
        .toObject()
        .value(QStringLiteral("numeric_preview"))
        .toObject();
    const QJsonObject selectedState = frame.value(QStringLiteral("selected_state")).toObject();

    QJsonObject preview = filterPreview;
    copyIfAvailable(preview, QStringLiteral("components.ballistic.time_s"), {
        { filterPreview, { QStringLiteral("components.ballistic.time_s"), QStringLiteral("time_s") } },
        { selectedState, { QStringLiteral("time_s") } },
        { frame, {
            QStringLiteral("state_time_s"),
            QStringLiteral("source_time_s"),
            QStringLiteral("time_s"),
            QStringLiteral("sample_time_s"),
            QStringLiteral("public_time_s"),
            QStringLiteral("public_output_time_s")
        } }
    });
    copyIfAvailable(preview, QStringLiteral("components.ballistic.h"), {
        { filterPreview, { QStringLiteral("components.ballistic.h"), QStringLiteral("h"), QStringLiteral("altitude_m") } },
        { selectedState, { QStringLiteral("h"), QStringLiteral("altitude_m") } },
        { frame, { QStringLiteral("h"), QStringLiteral("altitude_m") } }
    });
    copyIfAvailable(preview, QStringLiteral("components.ballistic.ma"), {
        { filterPreview, { QStringLiteral("components.ballistic.ma"), QStringLiteral("ma"), QStringLiteral("mach") } },
        { selectedState, { QStringLiteral("ma"), QStringLiteral("mach") } },
        { frame, { QStringLiteral("ma"), QStringLiteral("mach") } }
    });
    copyIfAvailable(preview, QStringLiteral("components.ballistic.alpha"), {
        { filterPreview, { QStringLiteral("components.ballistic.alpha"), QStringLiteral("alpha") } },
        { selectedState, { QStringLiteral("alpha") } },
        { frame, { QStringLiteral("alpha") } }
    });
    copyIfAvailable(preview, QStringLiteral("components.ballistic.q"), {
        { filterPreview, { QStringLiteral("components.ballistic.q"), QStringLiteral("q"), QStringLiteral("dynamic_pressure_pa") } },
        { selectedState, { QStringLiteral("q"), QStringLiteral("dynamic_pressure_pa") } },
        { frame, { QStringLiteral("q"), QStringLiteral("dynamic_pressure_pa") } }
    });
    return preview;
}

QString platformFieldIdentityKey(const launchsupport::PlatformFieldArtifactView& field)
{
    const QString fieldName = QString::fromStdString(field.field_name);
    const QString portId = QString::fromStdString(field.port_id);
    const QString componentId = QString::fromStdString(field.component_id);
    const QString layoutRef = QString::fromStdString(field.layout_ref);
    const QString nodeId = QString::fromStdString(field.node_id);
    const QString contractId = QString::fromStdString(field.contract_id);
    return QStringLiteral("%1|%2|%3|%4|%5|%6")
        .arg(fieldName, portId, componentId, layoutRef, nodeId, contractId);
}

QString platformFieldTitle(const launchsupport::PlatformFieldArtifactView& field)
{
    QString name = QString::fromStdString(field.field_name);
    if (name.isEmpty()) {
        name = QString::fromStdString(field.port_id);
    }
    QString component = QString::fromStdString(field.component_id);
    QString unit = QString::fromStdString(field.unit);
    QString title = component.isEmpty() ? name : QStringLiteral("%1 / %2").arg(name, component);
    if (!unit.isEmpty()) {
        title += QStringLiteral(" (%1)").arg(unit);
    }
    return title;
}

bool isRenderablePlatformField(const launchsupport::PlatformFieldArtifactView& field)
{
    if (field.node_count <= 0 || field.artifact_path.empty()) {
        return false;
    }
    const QString representation = QString::fromStdString(field.representation).toLower();
    if (!representation.contains(QStringLiteral("artifact"))) {
        return false;
    }
    return QFileInfo::exists(platformPathText(field.artifact_path));
}

contracts::SubjectType inferPlatformFieldSubject(
    const launchsupport::PlatformFieldArtifactView& field,
    const std::shared_ptr<const launchsupport::RuntimeViewModel>& runtimeView)
{
    const QString semanticText = QStringLiteral("%1 %2 %3 %4 %5 %6")
        .arg(QString::fromStdString(field.field_name),
             QString::fromStdString(field.port_id),
             QString::fromStdString(field.contract_id),
             QString::fromStdString(field.component_id),
             QString::fromStdString(field.layout_ref),
             QString::fromStdString(field.mesh_ref))
        .toLower();

    if (containsAnyToken(semanticText, {QStringLiteral("heatflux"), QStringLiteral("heat_flux"), QStringLiteral("ablation")})) {
        return contracts::SubjectType::K;
    }
    if (containsAnyToken(semanticText, {QStringLiteral("temperature"), QStringLiteral("thermal")})) {
        return contracts::SubjectType::T;
    }
    if (containsAnyToken(semanticText, {QStringLiteral("strain"), QStringLiteral("mises"), QStringLiteral("damage"),
                                        QStringLiteral("rul"), QStringLiteral("life"), QStringLiteral("structure")})) {
        return contracts::SubjectType::S;
    }
    if (containsAnyToken(semanticText, {QStringLiteral("pressure"), QStringLiteral("shell")})) {
        return contracts::SubjectType::P;
    }

    if (runtimeView) {
        for (const auto& fieldLayout : runtimeView->fields) {
            if (static_cast<std::int64_t>(fieldLayout.nodes.size()) == field.node_count) {
                return fieldLayout.subject;
            }
        }
    }
    return contracts::SubjectType::P;
}

std::vector<double> readPlatformFieldArtifactValues(const std::filesystem::path& artifactPath, QString* error)
{
    std::vector<double> values;
    QFile file(platformPathText(artifactPath));
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QStringLiteral("无法读取 field artifact: %1").arg(platformPathText(artifactPath));
        }
        return values;
    }

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) {
            *error = QStringLiteral("field artifact JSON 解析失败: %1").arg(parseError.errorString());
        }
        return values;
    }

    const QJsonArray array = doc.object().value(QStringLiteral("values")).toArray();
    values.reserve(static_cast<std::size_t>(array.size()));
    for (const QJsonValue& item : array) {
        if (item.isDouble()) {
            values.push_back(item.toDouble());
            continue;
        }
        if (item.isArray()) {
            const QJsonArray row = item.toArray();
            for (const QJsonValue& nested : row) {
                if (nested.isDouble()) {
                    values.push_back(nested.toDouble());
                    break;
                }
            }
            continue;
        }
        if (item.isObject()) {
            const QJsonValue value = item.toObject().value(QStringLiteral("value"));
            if (value.isDouble()) {
                values.push_back(value.toDouble());
            }
        }
    }
    return values;
}

std::filesystem::path environmentPath(const char* name)
{
#ifdef _MSC_VER
    char* value = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&value, &size, name) == 0 && value != nullptr) {
        std::filesystem::path path;
        if (*value != '\0') {
            path = std::filesystem::path(value);
        }
        std::free(value);
        return path;
    }
    if (value != nullptr) {
        std::free(value);
    }
    return {};
#else
    if (const char* value = std::getenv(name)) {
        if (*value != '\0') {
            return std::filesystem::path(value);
        }
    }
    return {};
#endif
}

std::string environmentString(const char* name)
{
#ifdef _MSC_VER
    char* value = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&value, &size, name) == 0 && value != nullptr) {
        std::string result(value);
        std::free(value);
        return result;
    }
    if (value != nullptr) {
        std::free(value);
    }
    return {};
#else
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string();
#endif
}

bool environmentFlagEnabled(const char* name, const bool defaultValue)
{
    std::string value = environmentString(name);
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](const unsigned char ch) {
        return !std::isspace(ch);
    }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [](const unsigned char ch) {
        return !std::isspace(ch);
    }).base(), value.end());
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (value.empty()) {
        return defaultValue;
    }
    if (value == "0" || value == "false" || value == "off" || value == "no") {
        return false;
    }
    if (value == "1" || value == "true" || value == "on" || value == "yes") {
        return true;
    }
    return defaultValue;
}

std::filesystem::path workspaceRootFromEnvironment()
{
    if (const auto explicitRoot = environmentPath("FLIGHTENV_WORKSPACE_HOME"); !explicitRoot.empty()) {
        return explicitRoot.lexically_normal();
    }
    if (const auto explicitRoot = environmentPath("FLIGHTENV_WORKSPACE_ROOT"); !explicitRoot.empty()) {
        return explicitRoot.lexically_normal();
    }
    if (const auto depsWorkspace = environmentPath("FLIGHTENV_DEPS_WORKSPACE_ROOT"); !depsWorkspace.empty()) {
        return depsWorkspace.parent_path().parent_path().lexically_normal();
    }
    return std::filesystem::current_path().lexically_normal();
}

std::string generatedPlatformRunId()
{
    const auto configured = environmentString("FLIGHTENV_PLATFORM_RUN_ID_PREFIX");
    if (!configured.empty()) {
        return configured;
    }
    return "ui_live_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss").toStdString();
}

struct PlatformUiRunConfig {
    int online_frames = 70;
    int prediction_every_frames = 20;
    int future_max_iterations = 15;
    int branch_chunk_iterations = 1;
    int max_concurrent_branches = 3;
    double replay_time_scale = 1.0;
    std::filesystem::path external_observation_stream;
    std::vector<std::string> disabled_operator_refs;
};

std::filesystem::path platformUiRunConfigPath(const std::filesystem::path& workspaceRoot)
{
    return workspaceRoot / "_local_artifacts" / "platform-ui" / "env_platform_controller_run_config.json";
}

QJsonObject readPlatformUiRunConfigJson(const std::filesystem::path& workspaceRoot)
{
    QFile file(QString::fromStdString(platformUiRunConfigPath(workspaceRoot).string()));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        return {};
    }
    return doc.object();
}

int configInt(const QJsonObject& config, const QString& key, const int fallback)
{
    const QJsonValue value = config.value(key);
    if (value.isDouble()) {
        return value.toInt(fallback);
    }
    if (value.isString()) {
        bool ok = false;
        const int parsed = value.toString().toInt(&ok);
        return ok ? parsed : fallback;
    }
    return fallback;
}

double configDouble(const QJsonObject& config, const QString& key, const double fallback)
{
    const QJsonValue value = config.value(key);
    if (value.isDouble()) {
        return value.toDouble(fallback);
    }
    if (value.isString()) {
        bool ok = false;
        const double parsed = value.toString().toDouble(&ok);
        return ok ? parsed : fallback;
    }
    return fallback;
}

std::vector<std::string> configStringArray(const QJsonObject& config, const QString& key)
{
    std::vector<std::string> out;
    const QJsonValue value = config.value(key);
    if (value.isString()) {
        const QString text = value.toString().trimmed();
        if (!text.isEmpty()) {
            out.push_back(text.toStdString());
        }
        return out;
    }
    if (!value.isArray()) {
        return out;
    }
    const QJsonArray array = value.toArray();
    for (const QJsonValue& item : array) {
        const QString text = item.toString().trimmed();
        if (!text.isEmpty()) {
            out.push_back(text.toStdString());
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

bool containsString(const std::vector<std::string>& values, const QString& value)
{
    const std::string needle = value.toStdString();
    return std::find(values.begin(), values.end(), needle) != values.end();
}

std::filesystem::path platformConfigPathValue(
    const QJsonObject& config,
    const QString& key,
    const std::filesystem::path& fallback,
    const std::filesystem::path& objectRoot)
{
    const QString text = config.value(key).toString();
    if (text.trimmed().isEmpty()) {
        return fallback;
    }
    std::filesystem::path value = text.toStdString();
    if (value.is_relative()) {
        value = objectRoot / value;
    }
    return value.lexically_normal();
}

PlatformUiRunConfig loadPlatformUiRunConfig(
    const std::filesystem::path& workspaceRoot,
    const std::filesystem::path& objectRoot)
{
    PlatformUiRunConfig config;
    config.external_observation_stream = objectRoot / "fixtures" / "sensor_stream_db70.json";

    const QJsonObject saved = readPlatformUiRunConfigJson(workspaceRoot);
    config.online_frames = configInt(saved, QStringLiteral("online_frames"), config.online_frames);
    config.prediction_every_frames = configInt(saved, QStringLiteral("prediction_every_frames"), config.prediction_every_frames);
    config.future_max_iterations = configInt(saved, QStringLiteral("future_max_iterations"), config.future_max_iterations);
    config.branch_chunk_iterations = configInt(saved, QStringLiteral("branch_chunk_iterations"), config.branch_chunk_iterations);
    config.max_concurrent_branches = configInt(saved, QStringLiteral("max_concurrent_branches"), config.max_concurrent_branches);
    config.replay_time_scale = configDouble(saved, QStringLiteral("replay_time_scale"), config.replay_time_scale);
    config.external_observation_stream = platformConfigPathValue(
        saved,
        QStringLiteral("external_observation_stream"),
        config.external_observation_stream,
        objectRoot);
    config.disabled_operator_refs = configStringArray(saved, QStringLiteral("disabled_operator_refs"));
    return config;
}

std::filesystem::path platformUiRunProfilePath(
    const std::filesystem::path& workspaceRoot,
    const std::string& runId)
{
    return workspaceRoot / "_local_artifacts" / "platform-ui" / "run-profiles" /
        (runId + ".operator_selection.json");
}

std::filesystem::path platformUiCompiledRootPath(
    const std::filesystem::path& workspaceRoot,
    const std::string& runId)
{
    return workspaceRoot / "_local_artifacts" / "platform-ui" / "compiled-workflows" / runId;
}

void writePlatformUiRunProfile(
    const std::filesystem::path& workspaceRoot,
    const std::string& runId,
    const PlatformUiRunConfig& runConfig)
{
    const auto profilePath = platformUiRunProfilePath(workspaceRoot, runId);
    QDir().mkpath(QFileInfo(QString::fromStdString(profilePath.string())).absolutePath());

    QJsonArray workflowIds;
    workflowIds.push_back(QStringLiteral("reentry.online_filtering_external_input.v1"));
    workflowIds.push_back(QStringLiteral("reentry.posterior_future_prediction.v1"));

    QJsonArray disabledOperators;
    for (const std::string& value : runConfig.disabled_operator_refs) {
        disabledOperators.push_back(QString::fromStdString(value));
    }

    QJsonObject runtimeLaunch;
    runtimeLaunch.insert(QStringLiteral("online_frames"), runConfig.online_frames);
    runtimeLaunch.insert(QStringLiteral("prediction_every_frames"), runConfig.prediction_every_frames);
    runtimeLaunch.insert(QStringLiteral("future_max_iterations"), runConfig.future_max_iterations);
    runtimeLaunch.insert(QStringLiteral("branch_chunk_iterations"), runConfig.branch_chunk_iterations);
    runtimeLaunch.insert(QStringLiteral("max_concurrent_branches"), runConfig.max_concurrent_branches);
    runtimeLaunch.insert(QStringLiteral("replay_time_scale"), runConfig.replay_time_scale);
    runtimeLaunch.insert(
        QStringLiteral("external_observation_stream"),
        QDir::fromNativeSeparators(QString::fromStdString(runConfig.external_observation_stream.string())));

    QJsonObject profile;
    profile.insert(QStringLiteral("schema_version"), QStringLiteral("flightenv.platform.run_profile.v1"));
    profile.insert(QStringLiteral("profile_id"), QStringLiteral("env_platform_controller_operator_selection"));
    profile.insert(QStringLiteral("title"), QStringLiteral("EnvPlatformController operator selection"));
    profile.insert(QStringLiteral("description"), QStringLiteral("Generated by EnvPlatformController from the operator checkbox table."));
    profile.insert(QStringLiteral("workflow_ids"), workflowIds);
    profile.insert(QStringLiteral("default_feature_enabled"), true);
    profile.insert(QStringLiteral("features"), QJsonObject{});
    profile.insert(QStringLiteral("disabled_operator_refs"), disabledOperators);
    profile.insert(QStringLiteral("runtime_launch"), runtimeLaunch);

    QFile file(QString::fromStdString(profilePath.string()));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        throw std::runtime_error(("Failed to write UI run profile: " + file.errorString()).toStdString());
    }
    file.write(QJsonDocument(profile).toJson(QJsonDocument::Indented));
}

void runProcessChecked(
    const QString& program,
    const QStringList& arguments,
    const std::filesystem::path& workingDirectory,
    const QString& label)
{
    QProcess process;
    process.setWorkingDirectory(QString::fromStdString(workingDirectory.string()));
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(program, arguments);
    if (!process.waitForStarted(30000)) {
        throw std::runtime_error((label + QStringLiteral(" failed to start: ") + process.errorString()).toStdString());
    }
    if (!process.waitForFinished(-1)) {
        throw std::runtime_error((label + QStringLiteral(" did not finish: ") + process.errorString()).toStdString());
    }
    const QString output = QString::fromLocal8Bit(process.readAllStandardOutput());
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        throw std::runtime_error((label + QStringLiteral(" failed:\n") + output).toStdString());
    }
    if (!output.trimmed().isEmpty()) {
        platformUiLogInfo( "%s output:\n%s", label.toStdString().c_str(), output.toStdString().c_str());
    }
}

std::filesystem::path preparePlatformUiCompiledWorkflows(
    const std::filesystem::path& workspaceRoot,
    const std::filesystem::path& pdkRoot,
    const std::filesystem::path& objectRoot,
    const std::string& runId,
    const PlatformUiRunConfig& runConfig)
{
    writePlatformUiRunProfile(workspaceRoot, runId, runConfig);

    const auto outDir = platformUiCompiledRootPath(workspaceRoot, runId);
    QDir().mkpath(QString::fromStdString(outDir.string()));

    const QString compileScript = QString::fromStdString(
        (pdkRoot / "tools" / "compile_object_workflow.ps1").string());
    const QString profilePath = QString::fromStdString(
        platformUiRunProfilePath(workspaceRoot, runId).string());

    auto compileWorkflow = [&](const QString& workflowId) {
        QStringList args;
        args << QStringLiteral("-NoProfile")
             << QStringLiteral("-ExecutionPolicy") << QStringLiteral("Bypass")
             << QStringLiteral("-File") << compileScript
             << QStringLiteral("-ObjectPackage") << QString::fromStdString(objectRoot.string())
             << QStringLiteral("-Workflow") << workflowId
             << QStringLiteral("-OutDir") << QString::fromStdString(outDir.string())
             << QStringLiteral("-RunId") << QString::fromStdString(runId)
             << QStringLiteral("-RunProfile") << profilePath;
        runProcessChecked(QStringLiteral("powershell"), args, workspaceRoot, QStringLiteral("compile ") + workflowId);
    };

    compileWorkflow(QStringLiteral("reentry.online_filtering_external_input.v1"));
    compileWorkflow(QStringLiteral("reentry.posterior_future_prediction.v1"));
    return outDir;
}

void appendArg(std::vector<std::string>& args, const char* name, const std::filesystem::path& value)
{
    args.push_back(name);
    args.push_back(value.string());
}

void appendArg(std::vector<std::string>& args, const char* name, const std::string& value)
{
    args.push_back(name);
    args.push_back(value);
}

void appendArg(std::vector<std::string>& args, const char* name, const char* value)
{
    args.push_back(name);
    args.push_back(value ? value : "");
}

void appendArg(std::vector<std::string>& args, const char* name, const int value)
{
    args.push_back(name);
    args.push_back(std::to_string(value));
}

launchsupport::PlatformControllerBackendOptions platformBackendOptions(bool compileWorkflowsForRun = false)
{
    launchsupport::PlatformControllerBackendOptions options;
    options.poll_interval = std::chrono::milliseconds(500);

    const auto replayRunDir = environmentPath("FLIGHTENV_PLATFORM_RUN_DIR");
    if (!replayRunDir.empty()) {
        options.evidence_root = replayRunDir;
        options.start_process_on_start = false;
        platformUiLogInfo( "Platform UI replay mode: %s", replayRunDir.string().c_str());
        return options;
    }

    const auto workspaceRoot = workspaceRootFromEnvironment();
    const auto depsWorkspace = environmentPath("FLIGHTENV_DEPS_WORKSPACE_ROOT").empty()
        ? (workspaceRoot / "_deps" / "workspace")
        : environmentPath("FLIGHTENV_DEPS_WORKSPACE_ROOT");
    const auto runtimeHostExe = environmentPath("FLIGHTENV_PLATFORM_RUNTIME_HOST_EXE").empty()
        ? (depsWorkspace / "x64" / "Release" / "FlightEnvPlatformRuntimeHost.exe")
        : environmentPath("FLIGHTENV_PLATFORM_RUNTIME_HOST_EXE");

    const auto runId = generatedPlatformRunId();
    const auto objectRoot = environmentPath("FLIGHTENV_PLATFORM_OBJECT_ROOT").empty()
        ? (workspaceRoot / "flightenv-object-reentry-vehicle")
        : environmentPath("FLIGHTENV_PLATFORM_OBJECT_ROOT");
    const auto pdkRoot = environmentPath("FLIGHTENV_PLATFORM_PDK_ROOT").empty()
        ? (workspaceRoot / "flightenv-platform-pdk")
        : environmentPath("FLIGHTENV_PLATFORM_PDK_ROOT");
    const auto compiledRoot = environmentPath("FLIGHTENV_PLATFORM_COMPILED_ROOT").empty()
        ? (workspaceRoot / "_local_artifacts" / "platform-pdk" / "compiled-workflows")
        : environmentPath("FLIGHTENV_PLATFORM_COMPILED_ROOT");
    const auto runRoot = environmentPath("FLIGHTENV_PLATFORM_RUNTIME_RUN_ROOT").empty()
        ? (workspaceRoot / "_local_artifacts" / "platform-runtime" / "runtime-host-runs")
        : environmentPath("FLIGHTENV_PLATFORM_RUNTIME_RUN_ROOT");
    const auto chainDir = environmentPath("FLIGHTENV_PLATFORM_CHAIN_DIR").empty()
        ? (workspaceRoot / "_local_artifacts" / "platform-runtime" / "mainline-runs" / runId)
        : environmentPath("FLIGHTENV_PLATFORM_CHAIN_DIR");

    const auto adapterRegistry = environmentPath("FLIGHTENV_PLATFORM_ADAPTER_REGISTRY").empty()
        ? (objectRoot / "tools" / "adapter_registries" / "ballistic_adapters.local.json")
        : environmentPath("FLIGHTENV_PLATFORM_ADAPTER_REGISTRY");
    const PlatformUiRunConfig runConfig = loadPlatformUiRunConfig(workspaceRoot, objectRoot);
    const auto explicitCompiledOnline = environmentPath("FLIGHTENV_PLATFORM_COMPILED_ONLINE");
    const auto explicitCompiledFuture = environmentPath("FLIGHTENV_PLATFORM_COMPILED_FUTURE");
    auto compiledOnline = explicitCompiledOnline.empty()
        ? (compiledRoot / "reentry.online_filtering_external_input.v1")
        : explicitCompiledOnline;
    auto compiledFuture = explicitCompiledFuture.empty()
        ? (compiledRoot / "reentry.posterior_future_prediction.v1")
        : explicitCompiledFuture;
    if (compileWorkflowsForRun && explicitCompiledOnline.empty() && explicitCompiledFuture.empty()) {
        const auto uiCompiledRoot = preparePlatformUiCompiledWorkflows(
            workspaceRoot,
            pdkRoot,
            objectRoot,
            runId,
            runConfig);
        compiledOnline = uiCompiledRoot / "reentry.online_filtering_external_input.v1";
        compiledFuture = uiCompiledRoot / "reentry.posterior_future_prediction.v1";
    }
    const auto externalStream = environmentPath("FLIGHTENV_PLATFORM_EXTERNAL_OBSERVATION_STREAM").empty()
        ? runConfig.external_observation_stream
        : environmentPath("FLIGHTENV_PLATFORM_EXTERNAL_OBSERVATION_STREAM");

    std::vector<std::string> args;
    appendArg(args, "--workspace-root", workspaceRoot);
    appendArg(args, "--pdk-root", pdkRoot);
    appendArg(args, "--object-package-root", objectRoot);
    appendArg(args, "--compiled-online", compiledOnline);
    appendArg(args, "--compiled-future", compiledFuture);
    appendArg(args, "--adapter-registry", adapterRegistry);
    appendArg(args, "--external-observation-stream", externalStream);
    appendArg(args, "--run-id-prefix", runId);
    appendArg(args, "--run-root", runRoot);
    appendArg(args, "--chain-dir", chainDir);
    appendArg(args, "--execution-backend", "native_adapter_sessions");
    appendArg(args, "--zero-copy-mode", "auto");
    appendArg(args, "--typed-buffer-persistence", "shadow_artifact");
    appendArg(args, "--online-frames", runConfig.online_frames);
    appendArg(args, "--prediction-every-frames", runConfig.prediction_every_frames);
    appendArg(args, "--future-max-iterations", runConfig.future_max_iterations);
    appendArg(args, "--branch-chunk-iterations", runConfig.branch_chunk_iterations);
    appendArg(args, "--max-concurrent-branches", runConfig.max_concurrent_branches);
    if (environmentFlagEnabled("FLIGHTENV_PLATFORM_PREFLIGHT_ADAPTERS", true)) {
        args.push_back("--preflight-adapters");
    }
    args.push_back("--replay-by-platform-clock");
    appendArg(args, "--replay-time-scale", std::to_string(runConfig.replay_time_scale));

    options.evidence_root = chainDir;
    options.start_process_on_start = true;
    options.runtime_process.executable = runtimeHostExe;
    options.runtime_process.working_directory = workspaceRoot;
    options.runtime_process.arguments = std::move(args);
    platformUiLogInfo(
        "Platform UI live mode: RuntimeHost=%s chain_dir=%s",
        runtimeHostExe.string().c_str(),
        chainDir.string().c_str());
    return options;
}

std::shared_ptr<launchsupport::IControllerBackend> createControllerBackend()
{
    return std::make_shared<launchsupport::PlatformControllerBackend>(platformBackendOptions());
}

} // namespace flightenv::platform_ui::internal

using namespace flightenv::platform_ui::internal;


void EnvPredictorUI::syncVTKSize() {
    QTimer::singleShot(0, this, [this]() {
        if (vtkMarker)
            vtkMarker->resize(ui.sensorCheckWidget->size());
        if (objectSensorVtk_ && objectSensorVtkHost_)
            objectSensorVtk_->resize(objectSensorVtkHost_->size());
        if (vtkXYZDlg)
            vtkXYZDlg->resize(ui.flightAttitudeWidget->size());
        });
}

EnvPredictorUI::EnvPredictorUI(QWidget* parent)
    : QMainWindow(parent),
    currentAxis(2),
    frameCount(0),
    currentFps(0.0),
    controller_backend_(createControllerBackend()),
    frameTimer(new QElapsedTimer),
    frameRateTimer(new QTimer(this))
{
    ui.setupUi(this);
    //初始化窗口
    initWindow();
    //初始化VTK窗口
    vtkSizeTimer = new QTimer(this);
    connect(vtkSizeTimer, &QTimer::timeout, this, &EnvPredictorUI::syncVTKSize);
    //日志配置
    initLogging();
    //初始化tab
    initTabs();
    //初始化模型树
    initTree();
    connect(ui.treeWidget_subjects, &QTreeWidget::itemClicked, this, &EnvPredictorUI::onItemClicked);
    ui.tabWidget_main->setCurrentIndex(0);
    connect(ui.pushButton_minimize, &QPushButton::clicked, this, &EnvPredictorUI::showMinimized);
    connect(ui.pushButton_maximize, &QPushButton::clicked, this, [this]() {
        if (isMaximized()) {
            showNormal();
            ui.pushButton_maximize->setIcon(QIcon(":/ui/img/arrows-out.svg"));
            ui.pushButton_maximize->setToolTip(QString::fromUtf8("最大化"));
        }
        else {
            showMaximized();
            ui.pushButton_maximize->setIcon(QIcon(":/ui/img/arrows-in.svg"));
            ui.pushButton_maximize->setToolTip(QString::fromUtf8("还原"));
        }
    });
    connect(ui.pushButton_39, &QPushButton::clicked, this, &EnvPredictorUI::close);
    //初始化图标
    initIcons();
    // train/start/pause/reset follow Qt auto-connect naming: on_<object>_<signal>().
}


/**
* @brief 初始化窗口组件
*/
void EnvPredictorUI::initWindow()
{
    ui.windowNumspinBox->setEnabled(0);
    ui.numHorWindowSpin->setEnabled(0);
    ui.splitter->setSizes(QList<int>() << 100 << 300 << 300 << 300);
    ui.splitter_2->setSizes(QList<int>() << 700 << 400);
    ui.splitter_3->setSizes(QList<int>() << 100 << 800);
    ui.groupBox_42->setMinimumWidth(100);
    ui.trainBtn->hide();
    ui.trainBtn->setEnabled(false);

    auto* trainingBox = new QGroupBox(QStringLiteral("训练配置与日志"), ui.tab);
    auto* trainingBoxLayout = new QVBoxLayout(trainingBox);
    trainingBoxLayout->setContentsMargins(8, 8, 8, 8);
    trainingBoxLayout->setSpacing(6);
    buildTrainingCliControls_(trainingBoxLayout, trainingBox);
    ui.gridLayout_84->addWidget(trainingBox, 1, 0);
    hideLegacyInlineTrainingButtons_();
}

/**
* @brief 初始化日志配置
*/
void EnvPredictorUI::initLogging()
{
    const QString logDir = QDir::current().filePath("log");
    QDir().mkpath(logDir);
    std::string std_logpath = QDir::toNativeSeparators(logDir).toStdString();
    if (!google::IsGoogleLoggingInitialized()) {
        google::InitGoogleLogging("");
        google::SetStderrLogging(google::GLOG_INFO);
        google::SetLogDestination(google::GLOG_INFO, (std_logpath + "\\I_").c_str());
        google::SetLogDestination(google::GLOG_WARNING, (std_logpath + "\\W_").c_str());
        google::SetLogDestination(google::GLOG_ERROR, (std_logpath + "\\E_").c_str());
    }
    FLAGS_minloglevel = 0;
    google::SetLogFilenameExtension(".log");
    FLAGS_logbufsecs = 5;//实时输出日志
    LOG(INFO) << "打印输出log文件在：" << std_logpath;
}

/**
* @brief 初始化标签页
*/
void EnvPredictorUI::initTabs()
{
    ui.tabWidget_main->setTabText(0, QString::fromUtf8("多学科智能预测模型"));
    ui.tabWidget_main->setTabText(1, QString::fromUtf8("模拟演示"));
    buildAcqAndConfigFlat_();
    buildRuntimeChainPage_();
    // 旧 GraphRuntimeController 页面只保留给迁移诊断。
    // 默认主 UI 不再暴露该入口；需要排查旧 evidence 时显式定义
    // FLIGHTENV_ENABLE_LEGACY_GRAPH_RUNTIME_PAGE 后再构建。
#ifdef FLIGHTENV_ENABLE_LEGACY_GRAPH_RUNTIME_PAGE
    buildGraphRuntimePage_();
#endif
}
/**
* @brief 初始化模型树
*/
void EnvPredictorUI::initTree()
{
    QString treeNode = QString::fromUtf8("学科反演模型");
    QStringList subjectList = {
        QString::fromUtf8("⽓动压⼒场"),
        QString::fromUtf8("⽓动热流场"),
        QString::fromUtf8("结构应变场"),
        QString::fromUtf8("结构温度场")
    };
    addNewRootNode(ui.treeWidget_subjects, treeNode, subjectList);

    treeNode = QString::fromUtf8("学科预测模型");
    subjectList = {
        QString::fromUtf8("⽓动压⼒场"),
        QString::fromUtf8("⽓动热流场"),
        QString::fromUtf8("结构⼒热场")
    };
    addNewRootNode(ui.treeWidget_subjects, treeNode, subjectList);
    treeNode = QString::fromUtf8("融合预示模型");
    subjectList.clear();
    addNewRootNode(ui.treeWidget_subjects, treeNode, subjectList);
    ui.treeWidget_subjects->expandAll();
}

/**
* @brief 初始化图标
*/
void EnvPredictorUI::initIcons()
{
    ui.pushButton_minimize->setToolTip(QString::fromUtf8("最小化"));
    ui.pushButton_maximize->setToolTip(QString::fromUtf8("还原"));
    ui.pushButton_39->setToolTip(QString::fromUtf8("关闭"));
    ui.pushButton_minimize->setIcon(QIcon(":/ui/img/arrows-in-line-horizontal.svg"));
    ui.pushButton_maximize->setIcon(QIcon(":/ui/img/arrows-in.svg"));
    ui.pushButton_minimize->setIconSize(QSize(20, 20));
    ui.pushButton_maximize->setIconSize(QSize(20, 20));

    this->setWindowIcon(QIcon(":/ui/img/codepen-logo.svg"));

    this->setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    this->showMaximized();

    QIcon tabIcon1;
    QPixmap tabPixmap1(":/ui/img/1.png");
    tabPixmap1 = tabPixmap1.scaled(200, 200, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    tabIcon1.addPixmap(tabPixmap1);

    QIcon tabIcon2;
    QPixmap tabPixmap2(":/ui/img/2.png");
    tabPixmap2 = tabPixmap2.scaled(200, 200, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    tabIcon2.addPixmap(tabPixmap2);

    QIcon tabIcon3;
    QPixmap tabPixmap3(":/ui/img/3.png");
    tabPixmap3 = tabPixmap3.scaled(200, 200, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    tabIcon3.addPixmap(tabPixmap3);
    ui.tabWidget_main->tabBar()->setIconSize(QSize(60, 30));
    ui.tabWidget_main->tabBar()->setTabIcon(0, tabIcon1);
    ui.tabWidget_main->tabBar()->setTabIcon(1, tabIcon2);
    ui.tabWidget_main->tabBar()->setTabIcon(2, tabIcon3);
}

/**
* @brief 创建固定图标
* @param
* @ - iconPath 图标路径
* @ - iconPath 图标尺寸
* @return 加载的图标
*/
QIcon EnvPredictorUI::createFixedIcon(const QString& iconPath, const QSize& iconSize)
{
    QIcon icon;
    QPixmap pixmap(iconPath);
    if (pixmap.isNull()) {
        qDebug() << "图标加载失败！路径：" << iconPath;
        return icon;
    }
    // 平滑缩放图标，保持比例，避免锯齿
    pixmap = pixmap.scaled(iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    icon.addPixmap(pixmap);
    return icon;
}

#ifdef ONLINE

/**
* @brief 建立ROS后台数据流与UI的连接
*/
int EnvPredictorUI::interactionData() {
    if (runtime_initialized_)
    {
        return 0;
    }
    //初始化runtime环境
    if (!initializeRuntime())
    {
        return 1;
    }
    //注册后台数据回调
    bindControllerCallbacks();
    runtime_initialized_ = true;
    return 0;
}
#endif

/**
* @brief 初始化 Runtime 运行环境
*/
bool EnvPredictorUI::initializeRuntime()
{
    try
    {
        //初始化runtime
        controller_backend_->initialize_runtime();
        runtime_view_ = controller_backend_->runtime_view();
        if (!runtime_view_)
        {
            platformUiLogWarning( "Platform controller backend initialized without legacy RuntimeViewModel.");
            initPlatformFieldWidget();
            initPlatformFieldControlPanel();
            return true;
        }
        platformUiLogInfo(
            "Controller runtime view ready: fields=%zu meshes=%zu sensors=%zu",
            runtime_view_->fields.size(),
            runtime_view_->meshes.size(),
            runtime_view_->sensors.size());
        initModel();
        return true;
    }
    catch (const std::exception& e)
    {
        platformUiLogError( "Controller runtime initialization failed: %s", e.what());
        return false;
    }
}

/**
* @brief 注册所有 Controller 回调函数
*/
void EnvPredictorUI::bindControllerCallbacks()
{
    launchsupport::ControllerViewCallbacks callbacks;
    callbacks.onLog = [](const std::string& s) { std::cout << s << std::endl; };
    callbacks.onSensor = [this](const launchsupport::SensorViewModel& d) {
        QPointer<EnvPredictorUI> self(this);
        QMetaObject::invokeMethod(this, [self, d]() {
            if (self) {
                self->handleSensorView(d);
            }
        }, Qt::QueuedConnection);
        return;
        sensorPkts.push_back(d);
        std::vector<double> sens;
        std::vector<std::vector<double>> senss;
        if (sensorsss.empty()) {
            return;
        }
        for (size_t i = 0; i < d.channels.size(); i++)
        {
            senss.clear();
            for (size_t p = 0; p < d.channels[i].values_by_node.size(); p++) {
                sens = d.channels[i].values_by_node[p];
                senss.push_back(sens);
            }
            sensorsss[i].push_back(senss);
        }

        QTimer::singleShot(0, this, [this]() {
            updateCharts(visibilitys, transpose(sensorsss[sensorflg].back()));
        });

        LOG(INFO) << "监听到传感器发出信号";
    };
    callbacks.onState = [](const contracts::StateFrame&) {
        LOG(INFO) << "监听到飞行状态发出信号";
    };
    callbacks.onRuntime = [this](const launchsupport::RuntimeViewModel& s) {
        LOG(INFO) << "监听到初始化完成，发出快照信息";
        runtime_view_ = std::make_shared<launchsupport::RuntimeViewModel>(s);
    };
    controller_backend_->bind_callbacks(std::move(callbacks));
    bindPlatformSnapshotCallback();
}

/**
 * @brief 注册预测结果回调
 * 当后台收到预测结果时，Controller 会调用 handlePrediction
 */
void EnvPredictorUI::bindPredictionCallback()
{
    // Platform UI binds all backend callbacks through bindControllerCallbacks().
}
/**
 * @brief 处理预测结果数据
 * Prediction 数据主要用于：
 * 1. 更新图表
 * 2. 更新参数表格
 * 3. 更新历史最大值
 * 4. 更新攻角
 * 5. 更新运行时间
 * 6. ROS线程与Qt线程，UI更新必须切回主线程
 */
void EnvPredictorUI::handlePrediction(const launchsupport::PredictionViewModel & d)
{
    //通知UI已经接收数据
    QMetaObject::invokeMethod(this, [this, d]() {
        emit dataChanged(d);
        }, Qt::QueuedConnection);

    std::vector < std::vector <double>> datass;
    std::vector <double> datas;
    int num = 0;
    bool flg = 0;
    int flg2 = 1;
    tableItemContents.clear();
    tableItemContents.resize(d.parameters.size() + 1);
    tableItemContents[0] = { "值","历史最大值", "上界","冗余度", "类别", "单位", "描述" };
    //便利所有预测参数
    for (size_t i = 0; i < sensorss.size(); i++)
    {
        datas.clear();
        flg = 0;
        for (size_t paramIndex = 0; paramIndex < d.parameters.size(); ++paramIndex) {
            const auto& param = d.parameters[paramIndex];
            if ((paramIndex == i) && param.numeric) {
                num++;
                flg = 1;
                datas.push_back(param.numeric_value);
            }
            flg2 = 1 + static_cast<int>(paramIndex);
            tableItemContents[flg2].push_back(QString::fromStdString(param.display_name));
            tableItemContents[flg2].push_back(QString::fromStdString(param.value_text));

            if (flg2 < static_cast<int>(historyMax.size()) && param.numeric_value > historyMax[flg2]) {
                tableItemContents[flg2].push_back(QString::fromStdString(param.value_text));
                historyMax[flg2] = param.numeric_value;
            }
            else {
                tableItemContents[flg2].push_back(flg2 < static_cast<int>(historyMax.size()) ? QString::number(historyMax[flg2]) : QString());
            }


            tableItemContents[flg2].push_back(QString::fromStdString(param.hard_max_text));
            tableItemContents[flg2].push_back(QString::number(
                (param.hard_max.value_or(param.numeric_value) - param.numeric_value)));

            tableItemContents[flg2].push_back(QString::fromStdString(param.category));
            tableItemContents[flg2].push_back(QString::fromStdString(param.unit));
            tableItemContents[flg2].push_back(QString::fromStdString(param.description));


            if (param.name == "alpha" || (param.name.empty() && QString::fromStdString(param.display_name) == "攻角"))
                currentY = param.numeric_value;
        }
        if (flg)
            sensorss[num - 1].insert(sensorss[num - 1].end(), datas.begin(), datas.end());
    }
    //切回UI线程
    QTimer::singleShot(0, this, [this]() {
        chartInif->updateChartXYData(sensorss[ui.Ballistic_X->currentIndex()], sensorss[ui.Ballistic_Y->currentIndex()],
            ui.Ballistic_Y->currentText() + " / " + ui.Ballistic_X->currentText());

        QDateTime currentDateTime = ui.missionTimeEdit->dateTime();
        QDateTime newDateTime = currentDateTime.addSecs(2);
        ui.missionTimeEdit->setDateTime(newDateTime);
        ui.missionTimeEdit->setDisplayFormat("yyyy/MM/dd m:ss");


        tableItemContents[1][2] = "";
        tableItemContents[1][3] = "";
        tableItemContents[1][4] = "";
        tableItemContents[2][2] = "";
        tableItemContents[2][3] = "";
        tableItemContents[2][4] = "";

        tableItemContents[3][2] = "";
        tableItemContents[3][4] = "";
        tableItemContents[4][2] = "";
        tableItemContents[4][4] = "";
        tableItemContents[5][2] = "";
        tableItemContents[5][4] = "";
        tableItemContents[6][2] = "";
        tableItemContents[6][4] = "";

        fillTableWidget(ui.paramCompareTableWidget, tableItemContents);
        });

    //更新攻角 UI
    QTimer::singleShot(0, this, &EnvPredictorUI::setAngle);
    LOG(INFO) << "[Predict] stamp_ns=" << d.stamp_ns;

}

void EnvPredictorUI::bindSensorCallback()
{
    // Platform UI binds all backend callbacks through bindControllerCallbacks().
}
void EnvPredictorUI::handleSensorView(const launchsupport::SensorViewModel& d)
{
    sensorPkts.push_back(d);
    if (sensorsss.empty()) {
        return;
    }

    const size_t channelCount = std::min(d.channels.size(), sensorsss.size());
    for (size_t i = 0; i < channelCount; ++i)
    {
        std::vector<std::vector<double>> senss;
        senss.reserve(d.channels[i].values_by_node.size());
        for (const auto& values : d.channels[i].values_by_node) {
            senss.push_back(values);
        }
        sensorsss[i].push_back(senss);
    }

    QTimer::singleShot(0, this, [this]() {
        if (sensorflg < 0 || static_cast<size_t>(sensorflg) >= markerPoints.size()) {
            return;
        }
        const auto selected = static_cast<std::size_t>(sensorflg);
        const bool needsRebuild =
            platformSensorDisplayIndex_ != sensorflg ||
            chartMarkers.size() != markerPoints[selected].size();
        if (needsRebuild) {
            rebuildSensorCurveCharts_(sensorflg, true);
            platformSensorDisplayIndex_ = sensorflg;
            return;
        }
        if (selected >= sensorsss.size() || sensorsss[selected].empty()) {
            return;
        }
        try {
            updateCharts(visibilitys, transpose(sensorsss[selected].back()));
        }
        catch (const std::exception& e) {
            platformUiLogWarning( "platform sensor chart update skipped: %s", e.what());
        }
    });

    LOG(INFO) << "platform sensor frame cached for controller display";
    return;

    if (sensorflg < 0 ||
        static_cast<size_t>(sensorflg) >= sensorsss.size() ||
        sensorsss[static_cast<size_t>(sensorflg)].empty()) {
        return;
    }

    QTimer::singleShot(0, this, [this]() {
        if (sensorflg < 0 ||
            static_cast<size_t>(sensorflg) >= sensorsss.size() ||
            sensorsss[static_cast<size_t>(sensorflg)].empty()) {
            return;
        }
        try {
            updateCharts(visibilitys, transpose(sensorsss[static_cast<size_t>(sensorflg)].back()));
        } catch (const std::exception& e) {
            platformUiLogWarning( "sensor chart update skipped: %s", e.what());
        }
    });

    LOG(INFO) << "platform/ROS sensor frame applied to controller charts";
}

void EnvPredictorUI::bindStateCallback()
{
    // Platform UI binds all backend callbacks through bindControllerCallbacks().
}
void EnvPredictorUI::bindRuntimeCallback()
{
    // Platform UI binds all backend callbacks through bindControllerCallbacks().
}

void EnvPredictorUI::comboBoxInfCurrentIndexChanged(int index) {
    (void)index;
    if (!chartInif || ui.Ballistic_X->currentIndex() < 0 || ui.Ballistic_Y->currentIndex() < 0) {
        return;
    }
    if (static_cast<std::size_t>(ui.Ballistic_X->currentIndex()) >= sensorss.size() ||
        static_cast<std::size_t>(ui.Ballistic_Y->currentIndex()) >= sensorss.size()) {
        return;
    }
    chartInif->setTitleY(ui.Ballistic_Y->currentText());
    chartInif->updateChartXYData(sensorss[ui.Ballistic_X->currentIndex()], sensorss[ui.Ballistic_Y->currentIndex()],
        ui.Ballistic_Y->currentText() + " / " + ui.Ballistic_X->currentText());
}

void EnvPredictorUI::initModel() {
    if (!runtime_view_) {
        qWarning() << "runtime view未初始化，无法初始化模型视图";
        return;
    }
    platformUiLogInfo( "UI initModel step: sensor markers begin");
    initSensorMarkerWidget();//标记点窗口
    platformUiLogInfo( "UI initModel step: sensor markers done");
    refreshRuntimeChainPage_();
    platformUiLogInfo( "UI initModel step: field widget begin");
    initFieldWidget();//物理场
    platformUiLogInfo( "UI initModel step: field widget done");
    platformUiLogInfo( "UI initModel step: field control begin");
    initFIeldControlPanel();//fieldcontrol
    platformUiLogInfo( "UI initModel step: field control done");
    platformUiLogInfo( "UI initModel step: parameter table begin");
    initParameterTable();//ROS信息参数表格
    platformUiLogInfo( "UI initModel step: parameter table done");
    platformUiLogInfo( "UI initModel step: scatter chart begin");
    initScatterChart();//初始化图标
    platformUiLogInfo( "UI initModel step: scatter chart done");
    platformUiLogInfo( "UI initModel step: flight attitude begin");
    initFlightAttitudeWidget();//初始化飞行状态窗口
    platformUiLogInfo( "UI initModel step: flight attitude done");

}

bool EnvPredictorUI::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::Wheel)
        if (watched == vtkContainer)
            return true;
    return QWidget::eventFilter(watched, event);
}

//鼠标按压事件
void EnvPredictorUI::mousePressEvent(QMouseEvent* event)
{
    if(event->button()==Qt::LeftButton)
    {
        if (event->pos().y() <= TITLE_HEIGHT)
        {
            dragging_ = true;
            dragPosition_ = event->globalPosition().toPoint() - frameGeometry().topLeft();
            event->accept();
        }
        else
        {
            dragging_ = false;
            QMainWindow::mousePressEvent(event);
        }
    }
    else
    {
        QMainWindow::mousePressEvent(event);
    }

}
//鼠标移动事件
void EnvPredictorUI::mouseMoveEvent(QMouseEvent* event)
{
    if (dragging_ && (event->buttons() & Qt::LeftButton))
    {
        move(event->globalPosition().toPoint()-dragPosition_);
        event->accept();
    }
    else
        QMainWindow::mouseMoveEvent(event);
}
//鼠标松开 事件
void EnvPredictorUI::mouseReleaseEvent(QMouseEvent* event)
{
    dragging_ = false;
    QMainWindow::mouseReleaseEvent(event);
}

void EnvPredictorUI::updateChartData(const launchsupport::PredictionViewModel& DTO) {

    (void)DTO;
}

void EnvPredictorUI::onCheckBoxToggled() {
    QVector<bool> visibility;
    for (QCheckBox* check : checkBoxes)
        visibility.push_back(check->isChecked());

    layoutVtk(visibility);
}


void EnvPredictorUI::updateCharts(const  QVector<bool>& visibility, std::vector< std::vector<double>> yData) {
    if (visibility.isEmpty() || chartMarkers.empty()) return;
    if (visibility.size() != chartMarkers.size()) {
        qWarning() << "图表数量与数据组数不匹配，跳过更新（图表数："
            << chartMarkers.size() << "，数据组数：" << visibility.size() << "）";
        return;
    }
    std::vector<std::vector<double>> datass(1);
    std::vector<double> datas;
    for (int chartIdx = 0; chartIdx < visibility.size(); ++chartIdx) {
        ChartSingleDialog* chartDlg = chartMarkers[chartIdx];
        if (chartDlg && visibility[chartIdx]){
            for (size_t i = 0; i < yData.size(); i++) {
                if (static_cast<size_t>(chartIdx) < yData[i].size()) {
                    datas.push_back(yData[i][static_cast<size_t>(chartIdx)]);
                }
            }
            if (datas.empty()) {
                continue;
            }
            datass[0] = (datas);
            const std::vector<std::vector<double>> multiCurveData = datass;
            chartDlg->updateChartData(multiCurveData);
            datas.clear();
        }
    }
}

void EnvPredictorUI::rebuildSensorCurveCharts_(int index, bool replayHistory)
{
    if (!vtkMarker || index < 0 || static_cast<std::size_t>(index) >= markerPoints.size()) {
        return;
    }

    sensorflg = index;

    if (!chartScrollArea) {
        chartScrollArea = new QScrollArea(ui.widget_4);
        chartScrollArea->setWidgetResizable(true);
        chartScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        chartScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        chartScrollArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        chartScrollArea->setStyleSheet("QScrollArea { border: none; }");
        chartScrollArea->setContentsMargins(0, 0, 0, 0);

        chartContainerWidget = new QWidget(chartScrollArea);
        chartScrollArea->setWidget(chartContainerWidget);

        QLayout* widget4Layout = ui.widget_4->layout();
        if (!widget4Layout) {
            auto* createdLayout = new QVBoxLayout(ui.widget_4);
            createdLayout->setSpacing(0);
            createdLayout->setContentsMargins(0, 0, 0, 0);
            widget4Layout = createdLayout;
        }
        widget4Layout->addWidget(chartScrollArea);
    }

    if (!chartContainerWidget) {
        return;
    }

    chartMarkers.clear();
    platformSensorTable_ = nullptr;

    QLayout* oldLayout = chartContainerWidget->layout();
    if (oldLayout) {
        QLayoutItem* item = nullptr;
        while ((item = oldLayout->takeAt(0)) != nullptr) {
            if (item->widget()) {
                item->widget()->deleteLater();
            }
            delete item;
        }
        delete oldLayout;
    }

    auto* newLayout = new QGridLayout(chartContainerWidget);
    newLayout->setSpacing(6);
    newLayout->setContentsMargins(0, 0, 0, 0);

    const int columnCount = std::max(1, ui.numHorWindowSpin->value());
    for (int col = 0; col < columnCount; ++col) {
        newLayout->setColumnStretch(col, 1);
    }

    const auto& points = markerPoints[static_cast<std::size_t>(index)];
    visibilitys.clear();
    visibilitys.reserve(static_cast<int>(points.size()));
    for (std::size_t i = 0; i < points.size(); ++i) {
        const auto& point = points[i];
        auto* chartDlg = new ChartSingleDialog(
            QString::number(static_cast<int>(i) + 1) +
                "\nX:" + QString::number(point[0], 'f', 2) +
                " Y:" + QString::number(point[1], 'f', 2) +
                " Z:" + QString::number(point[2], 'f', 2),
            chartContainerWidget);
        chartDlg->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        chartDlg->setMinimumHeight(120);
        chartDlg->setContentsMargins(0, 0, 0, 0);
        chartDlg->setStyleSheet("border: none;");

        const int row = static_cast<int>(i) / columnCount;
        const int col = static_cast<int>(i) % columnCount;
        if (points.size() == 1) {
            newLayout->addWidget(chartDlg, 0, 0, 1, columnCount);
        }
        else {
            newLayout->addWidget(chartDlg, row, col);
        }
        visibilitys.push_back(true);
        chartMarkers.push_back(chartDlg);
    }

    chartContainerWidget->setLayout(newLayout);
    chartContainerWidget->updateGeometry();
    chartScrollArea->updateGeometry();
    ui.widget_4->updateGeometry();

    vtkMarker->setMarkerPoints(points);

    if (replayHistory && static_cast<std::size_t>(sensorflg) < sensorsss.size()) {
        for (const auto& frame : sensorsss[static_cast<std::size_t>(sensorflg)]) {
            try {
                updateCharts(visibilitys, transpose(frame));
            }
            catch (const std::exception& e) {
                platformUiLogWarning( "sensor replay chart update skipped: %s", e.what());
            }
        }
    }
}

void EnvPredictorUI::refreshPlatformCoreParameterChart_(const QJsonObject& timelineRoot)
{
    const QJsonArray preferredFrames = timelineRoot.value(QStringLiteral("online_frames")).toArray();
    const QJsonArray fallbackFrames = timelineRoot.value(QStringLiteral("timeline_points")).toArray();
    const QJsonArray frames = preferredFrames.isEmpty() ? fallbackFrames : preferredFrames;
    if (frames.isEmpty()) {
        return;
    }

    struct CoreFramePoint {
        int loop = -1;
        int frame_index = -1;
        double public_time_s = 0.0;
        QJsonObject preview;
    };

    std::map<int, CoreFramePoint> bestFrameByLoop;
    for (const QJsonValue& frameValue : frames) {
        if (!frameValue.isObject()) {
            continue;
        }
        const QJsonObject frame = frameValue.toObject();
        const QJsonObject preview = platformBallisticPreview(frame);
        if (preview.isEmpty()) {
            continue;
        }
        if (!preview.contains(QStringLiteral("components.ballistic.h")) ||
            !preview.contains(QStringLiteral("components.ballistic.ma"))) {
            continue;
        }

        CoreFramePoint point;
        point.loop = frame.value(QStringLiteral("loop_iteration_index"))
            .toInt(frame.value(QStringLiteral("step_index")).toInt(frame.value(QStringLiteral("frame_index")).toInt(-1)));
        point.frame_index = frame.value(QStringLiteral("frame_index")).toInt(-1);
        point.public_time_s = frame.value(QStringLiteral("sample_time_s"))
            .toDouble(frame.value(QStringLiteral("runtime_time_s")).toDouble(static_cast<double>(point.loop)));
        point.preview = preview;

        auto it = bestFrameByLoop.find(point.loop);
        if (it == bestFrameByLoop.end() ||
            point.frame_index > it->second.frame_index ||
            (point.frame_index == it->second.frame_index && point.public_time_s >= it->second.public_time_s)) {
            bestFrameByLoop[point.loop] = point;
        }
    }

    if (bestFrameByLoop.empty()) {
        return;
    }

    const std::vector<std::pair<QString, QString>> metricKeys = {
        { QStringLiteral("platform.public_time_s"), QString::fromUtf8("平台时刻") },
        { QStringLiteral("components.ballistic.time_s"), displayMetricLabel(QStringLiteral("posterior.components.ballistic.time_s")) },
        { QStringLiteral("components.ballistic.h"), displayMetricLabel(QStringLiteral("posterior.components.ballistic.h")) },
        { QStringLiteral("components.ballistic.ma"), displayMetricLabel(QStringLiteral("posterior.components.ballistic.ma")) },
        { QStringLiteral("components.ballistic.alpha"), displayMetricLabel(QStringLiteral("posterior.components.ballistic.alpha")) },
        { QStringLiteral("components.ballistic.q"), displayMetricLabel(QStringLiteral("posterior.components.ballistic.q")) }
    };

    std::vector<std::vector<double>> nextSeries(metricKeys.size());
    for (const auto& item : bestFrameByLoop) {
        const CoreFramePoint& point = item.second;
        nextSeries[0].push_back(point.public_time_s);
        for (std::size_t metricIndex = 1; metricIndex < metricKeys.size(); ++metricIndex) {
            const QJsonValue value = point.preview.value(metricKeys[metricIndex].first);
            nextSeries[metricIndex].push_back(value.isDouble() ? value.toDouble() : 0.0);
        }
    }

    sensorss = std::move(nextSeries);

    const QString previousX = ui.Ballistic_X->currentText();
    const QString previousY = ui.Ballistic_Y->currentText();
    const QSignalBlocker blockX(ui.Ballistic_X);
    const QSignalBlocker blockY(ui.Ballistic_Y);
    ui.Ballistic_X->clear();
    ui.Ballistic_Y->clear();
    for (const auto& metric : metricKeys) {
        ui.Ballistic_X->addItem(metric.second, metric.first);
        ui.Ballistic_Y->addItem(metric.second, metric.first);
    }

    const int restoredX = ui.Ballistic_X->findText(previousX);
    const int restoredY = ui.Ballistic_Y->findText(previousY);
    ui.Ballistic_X->setCurrentIndex(restoredX >= 0 ? restoredX : 0);
    ui.Ballistic_Y->setCurrentIndex(restoredY >= 0 ? restoredY : std::min(2, ui.Ballistic_Y->count() - 1));

    if (chartInif && ui.Ballistic_X->count() > 0 && ui.Ballistic_Y->count() > 0) {
        chartInif->setTitleY(ui.Ballistic_Y->currentText());
        chartInif->updateChartXYData(
            sensorss[static_cast<std::size_t>(ui.Ballistic_X->currentIndex())],
            sensorss[static_cast<std::size_t>(ui.Ballistic_Y->currentIndex())],
            ui.Ballistic_Y->currentText() + " / " + ui.Ballistic_X->currentText());
    }
}

void EnvPredictorUI::layoutVtk(QVector<bool> visibility) {
    if ( vtkField.size() != visibility.size()) {
        qWarning() << "vtkField与visibility大小不匹配或vtkField为空";
        return;
    }

    QScrollArea* scrollArea = ui.filedShowWidget->findChild<QScrollArea*>();
    if (!scrollArea) {
        qWarning() << "未找到 QScrollArea";
        return;
    }
    QWidget* chartContainer = scrollArea->widget();
    if (!chartContainer) {
        chartContainer = new QWidget();
        scrollArea->setWidget(chartContainer);
        scrollArea->setWidgetResizable(true);
    }

    visibilityVtks = visibility;

    QLayout* oldLayout = chartContainer->layout();
    if (oldLayout) {
        QLayoutItem* item;
        while ((item = oldLayout->takeAt(0)) != nullptr) {
            if (item->widget())
                item->widget()->setParent(nullptr);
            delete item;
        }
        delete oldLayout;
    }

    QGridLayout* newLayout = new QGridLayout(chartContainer);
    newLayout->setSpacing(10);
    newLayout->setContentsMargins(0, 0, 0, 0);
    for (size_t i = 0; i < ui.windowNumspinBox->value(); i++)
        newLayout->setColumnStretch(i, 1);

    int visibleCount = 0;
    for (size_t i = 0; i < vtkField.size(); ++i) {
        VTKSingleDialog* vtkd = vtkField[i];
        if (!vtkd) {
            qWarning() << "vtkField[" << i << "] 是空指针，已跳过";
            continue;
        }

        if (visibility[i]) {
            vtkd->show();
            QWidget* container = new QWidget(chartContainer);
            QVBoxLayout* vLayout = new QVBoxLayout(container);
            vLayout->setSpacing(5);
            vLayout->setContentsMargins(0, 0, 0, 0);
            QLabel* titleLabel = new QLabel(QString::fromStdString(vtkd->windowTitle), container);
            titleLabel->setStyleSheet("font-weight: bold; color: #333;");
            vLayout->addWidget(titleLabel);
            vtkd->setParent(container);
            vLayout->addWidget(vtkd);
            int row = visibleCount / ui.windowNumspinBox->value();
            int col = visibleCount % ui.windowNumspinBox->value();

            for (size_t i = 0; i < ui.windowNumspinBox->value(); i++)
                newLayout->setColumnStretch(i, 1);

            newLayout->addWidget(container, row, col);

            visibleCount++;
        }
        else {
            vtkd->hide();
            vtkd->setParent(nullptr);
        }
    }
    chartContainer->setLayout(newLayout);
    chartContainer->updateGeometry();
    scrollArea->updateGeometry();
}

void EnvPredictorUI::redrawAllMarkers(QVector<bool> visibility) {
    if (chartMarkers.size() != visibility.size())
        return;
    QScrollArea* scrollArea = ui.widget_4->findChild<QScrollArea*>();
    if (!scrollArea)
        return;

    QWidget* chartContainer = scrollArea->widget();
    if (!chartContainer)
        return;
    visibilitys = visibility;
    // 2. 删除旧布局
    QLayout* oldLayout = chartContainer->layout();
    if (oldLayout) {
        QLayoutItem* item;
        while ((item = oldLayout->takeAt(0)) != nullptr) {
            delete item;
        }
        delete oldLayout;
    }

    // 3. 新建布局
    QGridLayout* newLayout = new QGridLayout(chartContainer);
    newLayout->setSpacing(10);
    newLayout->setContentsMargins(0, 0, 0, 0);
    for (size_t i = 0; i < ui.numHorWindowSpin->value(); i++)
        newLayout->setColumnStretch(i, 1);


    // 4. 重新布局显示的 chart
    int visibleCount = 0;
    for (size_t i = 0; i < chartMarkers.size(); ++i)
    {
        if (visibility[i]) {
            chartMarkers[i]->show();

                QWidget* container = new QWidget(chartContainer);
                QVBoxLayout* vLayout = new QVBoxLayout(container);
                vLayout->setSpacing(5);
                vLayout->setContentsMargins(0, 0, 0, 0);
                chartMarkers[i]->setParent(container);
                vLayout->addWidget(chartMarkers[i]);


                int row = visibleCount / ui.numHorWindowSpin->value();
                int col = visibleCount % ui.numHorWindowSpin->value();

                for (size_t i = 0; i < ui.numHorWindowSpin->value(); i++)
                    newLayout->setColumnStretch(i, 1);

                newLayout->addWidget(container, row, col);

                visibleCount++;


            //int row = visibleCount /  ui.numHorWindowSpin->value();
            //int col = visibleCount %  ui.numHorWindowSpin->value();
            //newLayout->addWidget(chartMarkers[i], row, col);

            //visibleCount++;
        }
        else {
            chartMarkers[i]->hide();
        }
    }

    chartContainer->setLayout(newLayout);
    chartContainer->updateGeometry();
}

void EnvPredictorUI::initModelTest() {
    //if (!runtime_view_) {
    //    qWarning() << "runtime view未初始化，跳过测试模型视图";
    //    return;
    //}
    //int forCount = 0;
    //{
    //    sensorNames.clear();
    //    markerPoints.clear();
    //    vtkSizeTimer->start(500);
    //    ui.sensorCheckWidget->installEventFilter(this);
    //
    //
    //    const std::string wholeMeshPath =
    //        QDir::toNativeSeparators(QDir::current().filePath("ele_in_whole.txt")).toStdString();
    //    const std::string surfaceMeshPath =
    //        QDir::toNativeSeparators(QDir::current().filePath("ele_surf_whole.txt")).toStdString();
    //    vtkMarker = new VTKSingleDialog("111", wholeMeshPath, ui.sensorCheckWidget);
    //    vtkMarker->setRuntimeView(runtime_view_);
    //    vtkMarker->isModelLoaded = vtkMarker->loadModelData();
    //    vtkMarker->flg = 0;
    //    vtkMarker->countFlg = 0;
    //    vtkMarker->appendModel("111", surfaceMeshPath);
    //    vtkMarker->show();
    //    std::vector<std::array<double, 3>> markerPoint;
    //
    //    for (size_t i = 0; i < runtime_view_->sensors.size(); i++)
    //    {
    //        markerPoint.clear();
    //        markerPoint.push_back({ -19.401,546.340,36.326 });
    //        markerPoint.push_back({ -15.7340, -794.78, 70.841 });
    //        markerPoint.push_back({ -77.591, 1534.000, 253.960 });
    //        markerPoints.push_back(markerPoint);
    //        sensorNames.push_back("222");
    //    }
    //    ui.sensorBox->addItems(sensorNames);
    //    ui.sensorBox->setCurrentIndex(0);
    //    //vtkMarker->setMarkerPoints(markerPoints[0]);
    //    vtkMarker->setMarkerPoints({
    //        {0, 0, 0},
    //        {100, 0, 0},
    //        {0, 100, 0}
    //        });
    //
    //    QVBoxLayout* layout = new QVBoxLayout(ui.sensorCheckWidget);
    //    layout->setContentsMargins(0, 0, 0, 0);
    //    layout->addWidget(vtkMarker);
    //    ui.sensorCheckWidget->setLayout(layout);
    //}
}

EnvPredictorUI::~EnvPredictorUI()
{
    prepareForShutdown();
}

void EnvPredictorUI::closeEvent(QCloseEvent* event)
{
    prepareForShutdown();
    QMainWindow::closeEvent(event);
}

void EnvPredictorUI::prepareForShutdown()
{
    if (shutdown_prepared_) {
        return;
    }
    shutdown_prepared_ = true;

    if (vtkSizeTimer) {
        vtkSizeTimer->stop();
    }
    if (vtkXYZSizeTimer) {
        vtkXYZSizeTimer->stop();
    }
    if (frameRateTimer) {
        frameRateTimer->stop();
    }
    if (platformRenderTimer_) {
        platformRenderTimer_->stop();
    }
    stopGraphRuntimeRunner_();
    if (controller_backend_) {
#ifdef ONLINE
        if (runtime_initialized_ && controllerBackendCanRun()) {
            try {
                if (!controller_backend_->stop_online_run()) {
                    platformUiLogWarning( "UI shutdown: online run stop/finalize reported failure.");
                }
            }
            catch (const std::exception& e) {
                platformUiLogError( "UI shutdown: online run stop/finalize failed: %s", e.what());
            }
            runtime_initialized_ = false;
        }
#endif
        controller_backend_->clear_callbacks();
    }
}
//// ----------------------
//// 颜色映射切换
//// ----------------------
void EnvPredictorUI::onTimerTimeout()
{
    currentAxis = (currentAxis + 1) % 3;
    updateColorMapping();
}

/**
* @brief 初始化传感器标记窗口
*/
void EnvPredictorUI::initSensorMarkerWidget()
{
    sensorNames.clear();
    markerPoints.clear();
    sensorsss.clear();
    visibilitys.clear();
    ui.sensorBox->clear();
    connect(ui.sensorBox,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this,
            &EnvPredictorUI::on_comboBox_4_currentIndexChanged,
            Qt::UniqueConnection);
    vtkSizeTimer->start(500);
    ui.sensorCheckWidget->installEventFilter(this);
    if (runtime_view_->meshes.empty())
    {
        qWarning() << "runtime view中没有mesh信息";
        return;
    }
    //创建VTK标记窗口
    vtkMarker = new VTKSingleDialog(runtime_view_->meshes[0].name,MODELSTLDIR + runtime_view_->meshes[0].path,ui.sensorCheckWidget);
    vtkMarker->setRuntimeView(runtime_view_);
    int sensorFieldIndex = 2;
    if (sensorFieldIndex < 0 || static_cast<std::size_t>(sensorFieldIndex) >= runtime_view_->fields.size()) {
        sensorFieldIndex = 0;
        if (!runtime_view_->meshes[0].subject_indices.empty()) {
            sensorFieldIndex = runtime_view_->meshes[0].subject_indices.front();
        }
    }
    if (sensorFieldIndex < 0 || static_cast<std::size_t>(sensorFieldIndex) >= runtime_view_->fields.size()) {
        sensorFieldIndex = 0;
    }
    vtkMarker->flg = sensorFieldIndex;
    vtkMarker->isModelLoaded = vtkMarker->loadModelData();//加载外壳
    vtkMarker->countFlg = 0;
    vtkMarker->RemoveActor();
    //追加内部结构
    // 加载标记点
    loadPlatformSensorResources();
    if (!platformSensorLayouts_.empty()) {
        sensorsss.resize(platformSensorLayouts_.size());
        for (const auto& s : platformSensorLayouts_)
        {
            markerPoints.push_back(s.nodes);
            sensorNames.push_back(s.display_name.isEmpty() ? s.resource_id : s.display_name);
        }
    }
    else
    if (!runtime_view_->sensors.empty()) {
        sensorsss.resize(runtime_view_->sensors.size());
        for (const auto& s : runtime_view_->sensors)
        {
            std::vector<std::array<double, 3>> points;
            for (const auto& node : s.nodes)
            {
                points.push_back({ node.x, node.y, node.z });
            }
            markerPoints.push_back(points);
            sensorNames.push_back(QString::fromStdString(s.name));
        }
    }

    if (!markerPoints.empty())
    {
        for (size_t i = 0; i < markerPoints[0].size(); i++)
            visibilitys.push_back(1);
    }

    ui.sensorBox->addItems(sensorNames);
    if (!sensorNames.empty())
        vtkMarker->setMarkerPoints(markerPoints[0]);//设置传感器标记点


    // 设置布局
    if (!ui.sensorCheckWidget->layout())
    {
        QVBoxLayout* layout = new QVBoxLayout(ui.sensorCheckWidget);
        layout->setContentsMargins(0, 0, 0, 0);
        ui.sensorCheckWidget->setLayout(layout);
    }
    ui.sensorCheckWidget->layout()->addWidget(vtkMarker);
    connect(vtkMarker, &VTKSingleDialog::allMarkersVisibilityUpdated, this, &EnvPredictorUI::redrawAllMarkers);
    if (!platformSensorFrames_.empty()) {
        updatePlatformSensorDisplayForLoop(std::max(0, platformSensorFrames_.front().loop));
    }
}
/**
* @brief 初始化物理场
*/
void EnvPredictorUI::initFieldWidget()
{
    initPlatformFieldWidget();
    return;
    int forCount = 0;
    QVector<bool> visibilitys;
    for (auto infos : runtime_view_->meshes) {
        for (auto info : infos.subject_indices) {
            forCount = 0;
            std::string s;
            switch (info)
            {
            case 0:
                s = "压力（Pa）";
                forCount = 1;
                break;
            case 1:
                s = "热流（W/m2）";
                forCount = 1;
                break;
            case 2:
                s = "应力（MPa）";
                forCount = 1;
                break;
            case 3:
                s = "温度（℃）";
                forCount = 1;
                break;
            default:
                break;
            }
            // 查找滚动区域容器
            fieldScrollArea = ui.filedShowWidget->findChild<QScrollArea*>();
            vtkContainer = fieldScrollArea ? fieldScrollArea->widget() : nullptr;
            if (fieldScrollArea) fieldScrollArea->installEventFilter(this);
            if (vtkContainer) vtkContainer->installEventFilter(this);
            // 创建物理场VTK窗口
            for (size_t i = 0; i < forCount; i++) {
                VTKSingleDialog* vtkd = new VTKSingleDialog(infos.name + s, MODELSTLDIR + infos.path, vtkContainer);
                vtkd->flg = info;
                vtkd->setRuntimeView(runtime_view_);
                vtkd->isModelLoaded = vtkd->loadModelData();

                vtkd->countFlg = i;
                vtkd->setMinimumHeight(300);
                vtkField.push_back(vtkd);
                vtkd->setWindowTitle(QString::fromStdString(infos.name + s));
                connect(this, &EnvPredictorUI::dataChanged, vtkd, &VTKSingleDialog::updateVTKData);
                visibilitys.push_back(true);
            }
        }
    }
    // 启用列数控制
    ui.windowNumspinBox->setEnabled(1);
    ui.numHorWindowSpin->setEnabled(1);
    layoutVtk(visibilitys);
}

/**
* @brief 初始化物理场控制
*/
void EnvPredictorUI::initFIeldControlPanel()
{
    initPlatformFieldControlPanel();
    return;
    //清理
    QLayout* oldLayout = ui.widget_5->layout();
    if (oldLayout) {
        QLayoutItem* item;
        while ((item = oldLayout->takeAt(0)) != nullptr) {
            if (item->widget()) {
                item->widget()->deleteLater();
            }
            delete item;
        }
        delete oldLayout;
    }
    //创建滚动区域
    QScrollArea* scrollArea = new QScrollArea(ui.widget_5);
    scrollArea->setWidgetResizable(true);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    QWidget* scrollContentWidget = new QWidget(scrollArea);

    int columns = 6;
    QGridLayout* checkLayout = new QGridLayout(scrollContentWidget);
    checkLayout->setSpacing(5);
    checkLayout->setContentsMargins(5, 5, 5, 5);

    checkBoxes.clear();
    // 为每个物理场创建复选框
    for (int i = 0; i < static_cast<int>(visibilitys.size()); ++i) {
        QCheckBox* check = new QCheckBox(QString::fromStdString(vtkField[i]->windowTitle), scrollContentWidget);
        check->setChecked(true);
        checkBoxes.push_back(check);

        int row = i / columns;
        int col = i % columns;
        checkLayout->addWidget(check, row, col);

        connect(check, &QCheckBox::toggled, this, &EnvPredictorUI::onCheckBoxToggled);
    }

    for (int col = 0; col < columns; ++col)
        checkLayout->setColumnStretch(col, 1);
    scrollArea->setWidget(scrollContentWidget);

    QVBoxLayout* mainLayout = new QVBoxLayout(ui.widget_5);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->addWidget(scrollArea);

    ui.widget_5->setLayout(mainLayout);
}

void EnvPredictorUI::initPlatformFieldWidget()
{
    auto* hostLayout = ui.filedShowWidget->layout();
    if (!hostLayout) {
        hostLayout = new QVBoxLayout(ui.filedShowWidget);
        hostLayout->setContentsMargins(0, 0, 0, 0);
        ui.filedShowWidget->setLayout(hostLayout);
    }

    fieldScrollArea = ui.filedShowWidget->findChild<QScrollArea*>();
    if (!fieldScrollArea) {
        fieldScrollArea = new QScrollArea(ui.filedShowWidget);
        fieldScrollArea->setWidgetResizable(true);
        hostLayout->addWidget(fieldScrollArea);
    }

    vtkContainer = fieldScrollArea->widget();
    if (!vtkContainer) {
        vtkContainer = new QWidget(fieldScrollArea);
        fieldScrollArea->setWidget(vtkContainer);
        fieldScrollArea->setWidgetResizable(true);
    }

    if (auto* oldLayout = vtkContainer->layout()) {
        QLayoutItem* item = nullptr;
        while ((item = oldLayout->takeAt(0)) != nullptr) {
            if (item->widget()) {
                item->widget()->setParent(nullptr);
            }
            delete item;
        }
        delete oldLayout;
    }

    platformFieldGridLayout_ = new QGridLayout(vtkContainer);
    platformFieldGridLayout_->setSpacing(10);
    platformFieldGridLayout_->setContentsMargins(0, 0, 0, 0);
    vtkContainer->setLayout(platformFieldGridLayout_);
    ui.windowNumspinBox->setEnabled(true);
    ui.numHorWindowSpin->setEnabled(true);
    connect(ui.windowNumspinBox, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) {
        layoutPlatformFieldWidgets();
    }, Qt::UniqueConnection);
}

void EnvPredictorUI::initPlatformFieldControlPanel()
{
    if (auto* oldLayout = ui.widget_5->layout()) {
        QLayoutItem* item = nullptr;
        while ((item = oldLayout->takeAt(0)) != nullptr) {
            if (item->widget()) {
                item->widget()->deleteLater();
            }
            delete item;
        }
        delete oldLayout;
    }

    ui.widget_5->setMinimumHeight(0);
    ui.widget_5->setMaximumHeight(0);
    ui.widget_5->hide();
    ui.startBtn->setText(QStringLiteral("开始/继续"));
    ui.startBtn->setToolTip(QStringLiteral("启动或继续平台推流；暂停后点击这里恢复时间轴轮询和画面更新"));
    ui.pauseBtn->setToolTip(QStringLiteral("暂停平台推流到界面：停止读取 run_timeline 和发布新帧，RuntimeHost 进程保持运行"));
    ui.resetBtn->setText(QStringLiteral("中止/复位"));
    ui.resetBtn->setToolTip(QStringLiteral("中止当前平台运行：停止 RuntimeHost 进程和界面推流，下一次开始会重新初始化"));

    auto* timelineLayout = qobject_cast<QVBoxLayout*>(ui.groupBox_42->layout());
    if (!timelineLayout) {
        timelineLayout = new QVBoxLayout(ui.groupBox_42);
        timelineLayout->setContentsMargins(6, 4, 6, 4);
        ui.groupBox_42->setLayout(timelineLayout);
    }

    if (auto* oldTimelineControls = ui.groupBox_42->findChild<QWidget*>(QStringLiteral("platformTimelineControls"))) {
        timelineLayout->removeWidget(oldTimelineControls);
        delete oldTimelineControls;
    }

    auto* timelineControls = new QWidget(ui.groupBox_42);
    timelineControls->setObjectName(QStringLiteral("platformTimelineControls"));
    auto* root = new QHBoxLayout(timelineControls);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(8);

    root->addWidget(new QLabel(QStringLiteral("平台分支"), timelineControls));
    platformBranchCombo_ = new QComboBox(timelineControls);
    platformBranchCombo_->setMinimumWidth(0);
    platformBranchCombo_->setMinimumContentsLength(16);
    platformBranchCombo_->setMaximumWidth(420);
    platformBranchCombo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    platformBranchCombo_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    platformBranchCombo_->setToolTip(QStringLiteral("当前查看的运行分支；对象固定加载，不需要在这里切换对象"));
    root->addWidget(platformBranchCombo_, 1);

    platformFollowLatestCheck_ = new QCheckBox(QStringLiteral("跟随最新"), timelineControls);
    platformFollowLatestCheck_->setChecked(true);
    platformFollowLatestCheck_->setToolTip(QStringLiteral("在线运行时始终显示最新帧；取消后可拖动进度回放历史帧"));
    root->addWidget(platformFollowLatestCheck_);

    platformFrameSlider_ = ui.missionSlider;
    platformFrameSlider_->setTracking(false);
    platformFrameSlider_->setRange(0, 0);

    root->addWidget(new QLabel(QStringLiteral("帧"), timelineControls));
    platformFrameProgressLabel_ = new QLabel(QStringLiteral("0/0"), timelineControls);
    platformFrameProgressLabel_->setMinimumWidth(110);
    platformFrameProgressLabel_->setAlignment(Qt::AlignCenter);
    platformFrameProgressLabel_->setStyleSheet(QStringLiteral(
        "QLabel{padding:4px;color:#0f172a;background:#f1f5f9;border:1px solid #cbd5e1;border-radius:4px;}"));
    root->addWidget(platformFrameProgressLabel_);

    platformFrameStatusLabel_ = new QLabel(QStringLiteral("等待平台 run_timeline_index.json"), timelineControls);
    platformFrameStatusLabel_->setWordWrap(false);
    platformFrameStatusLabel_->setMinimumWidth(170);
    platformFrameStatusLabel_->setMaximumWidth(260);
    platformFrameStatusLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    platformFrameStatusLabel_->setStyleSheet(QStringLiteral(
        "QLabel{padding:4px;color:#475569;background:#f8fafc;border:1px solid #e5e7eb;border-radius:4px;}"));
    root->addWidget(platformFrameStatusLabel_);
    timelineControls->setLayout(root);
    timelineLayout->insertWidget(1, timelineControls);
    platformFieldCheckContainer_ = nullptr;
    platformFieldCheckLayout_ = nullptr;

    connect(platformBranchCombo_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &EnvPredictorUI::onPlatformBranchChanged,
            Qt::UniqueConnection);
    if (platformFrameSlider_) {
        connect(platformFrameSlider_, &QSlider::valueChanged,
                this, &EnvPredictorUI::onPlatformFrameSliderChanged,
                Qt::UniqueConnection);
    }
    connect(platformFollowLatestCheck_, &QCheckBox::toggled,
            this, &EnvPredictorUI::onPlatformFollowLatestToggled,
            Qt::UniqueConnection);
}


void EnvPredictorUI::initParameterTable()
{
    // Platform mode owns field/sensor views through runtime resources. The
    // legacy ballistic table assumes a fixed six-row parameter layout.
    ui.Ballistic_X->clear();
    ui.Ballistic_Y->clear();
    tableItemContents.clear();
    historyMax.clear();
    sensorss.clear();
    ui.paramCompareTableWidget->clear();
    ui.paramCompareTableWidget->setRowCount(0);
    ui.paramCompareTableWidget->setColumnCount(0);
    connect(ui.Ballistic_X, SIGNAL(currentIndexChanged(int)),
        this, SLOT(comboBoxInfCurrentIndexChanged(int)), Qt::UniqueConnection);
    connect(ui.Ballistic_Y, SIGNAL(currentIndexChanged(int)),
        this, SLOT(comboBoxInfCurrentIndexChanged(int)), Qt::UniqueConnection);
    platformUiLogInfo( "Platform mode: skipped legacy parameter table initialization.");
    return;

    int flg = 0, flg2 = 1;
    tableItemContents.clear();
    tableItemContents.resize(runtime_view_->parameters.size() + 1);
    tableItemContents[0] = { "值","历史最大值", "上界","冗余度", "类别", "单位", "描述" };
    historyMax.clear();
    historyMax.push_back(0.0);
    for (size_t index = 0; index < runtime_view_->parameters.size(); ++index) {
        const auto& param = runtime_view_->parameters[index];
        flg++;
        flg2 = 1 + static_cast<int>(index);
        tableItemContents[flg2].push_back(QString::fromStdString(param.display_name));
        tableItemContents[flg2].push_back(QString::fromStdString(param.value_text));

        tableItemContents[flg2].push_back(QString::fromStdString(param.value_text));
        historyMax.push_back(param.numeric_value);

        tableItemContents[flg2].push_back(QString::fromStdString(param.hard_max_text));

        tableItemContents[flg2].push_back(QString::number(
            (param.hard_max.value_or(param.numeric_value) - param.numeric_value)));


        tableItemContents[flg2].push_back(QString::fromStdString(param.category));
        tableItemContents[flg2].push_back(QString::fromStdString(param.unit));
        tableItemContents[flg2].push_back(QString::fromStdString(param.description));
    }
    sensorss.resize(flg);

    for (size_t i = 1; i < tableItemContents.size(); i++) {
        ui.Ballistic_X->addItem(tableItemContents[i][0]);
        ui.Ballistic_Y->addItem(tableItemContents[i][0]);
    }
    // 清空特定行不需要显示的列
    const auto clearLegacyCell = [this](size_t row, size_t column) {
        if (row < tableItemContents.size() && column < tableItemContents[row].size()) {
            tableItemContents[row][column] = "";
        }
    };
    clearLegacyCell(1, 2);
    clearLegacyCell(1, 3);
    clearLegacyCell(1, 4);
    clearLegacyCell(2, 2);
    clearLegacyCell(2, 3);
    clearLegacyCell(2, 4);

    clearLegacyCell(3, 2);
    clearLegacyCell(3, 4);
    clearLegacyCell(4, 2);
    clearLegacyCell(4, 4);
    clearLegacyCell(5, 2);
    clearLegacyCell(5, 4);
    clearLegacyCell(6, 2);
    clearLegacyCell(6, 4);


    fillTableWidget(ui.paramCompareTableWidget, tableItemContents);
    qDebug() << tableItemContents;
    connect(ui.Ballistic_X, SIGNAL(currentIndexChanged(int)),
        this, SLOT(comboBoxInfCurrentIndexChanged(int)));

    connect(ui.Ballistic_Y, SIGNAL(currentIndexChanged(int)),
        this, SLOT(comboBoxInfCurrentIndexChanged(int)));
}

/**
* @brief 初始化图表
*/
void EnvPredictorUI::initScatterChart()
{
    chartInif = new ChartSingleDialog("", ui.Ballistic_Chart);
    chartInif->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    QVBoxLayout* layout = new QVBoxLayout(ui.Ballistic_Chart);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(chartInif);
    comboBoxInfCurrentIndexChanged(0);
}

/**
* @brief 初始化飞行状态窗口
*/
void EnvPredictorUI::initFlightAttitudeWidget()
{
    vtkXYZDlg = new VTKSingleDialog("", "", ui.flightAttitudeWidget);
    vtkXYZDlg->setRotationAnimationTimer();
    std::string stlFilePath = MODELFLIGHTSTLDIR;
    if (!vtkXYZDlg->loadSTLModel(stlFilePath)) {
        qWarning() << "STL模型加载失败：" << QString::fromStdString(stlFilePath);
    }
}
//
//// ----------------------
//// 更新颜色映射
//// ----------------------
void EnvPredictorUI::updateColorMapping()
{

    //axisValues.reserve(stlVertices.size());

    //switch (currentAxis) {
    //case 0:
    //    for (const auto& pt : stlVertices) axisValues.append(pt.x());
    //    break;
    //case 1:
    //    for (const auto& pt : stlVertices) axisValues.append(pt.y());
    //    break;
    //case 2:
    //    for (const auto& pt : stlVertices) axisValues.append(pt.z());
    //    break;
    //}

    //// 单独应用颜色映射
    //cloudChart->applyColorMapping(axisValues, 12);
    //frameCount++;
}

void EnvPredictorUI::renderByPointValues(const std::vector<double>& pointValues) {
    // 线程安全检查：确保在UI线程执行
    if (QThread::currentThread() != this->thread()) {
        QMetaObject::invokeMethod(this, "renderByPointValues",
            Qt::QueuedConnection,
            Q_ARG(const std::vector<double>&, pointValues));
        return;
    }

    // 核心对象有效性校验
    if (!dynamicPoints || !vertexValues || !surface || !mapper || !renderWindow) {
        qCritical() << "VTK核心对象未初始化，渲染中止";
        return;
    }

    // 数据有效性校验
    size_t vertexCount = dynamicPoints->GetNumberOfPoints();
    if (pointValues.size() != vertexCount || vertexCount == 0) {
        qWarning() << "顶点数据无效，渲染失败";
        return;
    }

    // 计算顶点值范围
    double minVal = VTK_DOUBLE_MAX, maxVal = VTK_DOUBLE_MIN;
    for (double val : pointValues) {
        if (std::isnan(val) || std::isinf(val)) val = 0.0; // 替换无效值
        minVal = std::min(minVal, val);
        maxVal = std::max(maxVal, val);
    }
    // 处理范围异常
    if (minVal >= maxVal) {
        minVal = std::max(0.0, minVal - 0.1);
        maxVal = maxVal + 0.1;
    }

    // 更新顶点属性值
    vertexValues->SetNumberOfTuples(vertexCount);
    for (vtkIdType i = 0; i < static_cast<vtkIdType>(vertexCount); ++i) {
        double val = pointValues[i];
        vertexValues->SetValue(i, std::isnan(val) ? (minVal + maxVal) / 2 : val);
    }
    vertexValues->Modified();

    // 更新模型数据
    surface->GetPointData()->SetScalars(vertexValues);
    surface->Modified();

    // 仅在范围变化时更新颜色表（减少计算）
    if (std::abs(minVal - lastMin) > 1e-6 || std::abs(maxVal - lastMax) > 1e-6) {
        colorTable->SetRange(minVal, maxVal);
        colorTable->SetTableRange(minVal, maxVal);
        colorTable->Build();
        colorTable->Modified();
        mapper->SetScalarRange(minVal, maxVal);
        lastMin = minVal;
        lastMax = maxVal;

        // 同步更新颜色条
        if (scalarBar) {
            scalarBar->SetLookupTable(colorTable);
            scalarBar->Modified();
        }
    }
    mapper->Modified();

    // 单次渲染
    renderWindow->Render();
}
//
//// ----------------------
//// 帧率显示更新
//// ----------------------
void EnvPredictorUI::updateFrameRateDisplay()
{
    qint64 elapsedTime = frameTimer->elapsed();
    if (elapsedTime > 0) {
        currentFps = (frameCount * 1000.0) / elapsedTime;
    }

    QString axisName;
    switch (currentAxis) {
    case 0: axisName = "X轴"; break;
    case 1: axisName = "Y轴"; break;
    case 2: axisName = "Z轴"; break;
    }

    //ui->label->setText(
    //    QString("帧率: %1 fps | 颜色映射: %2")
    //    .arg(currentFps, 0, 'f', 1)
    //    .arg(axisName)
    //);

    frameCount = 0;
    frameTimer->restart();
}

void EnvPredictorUI::onItemClicked(QTreeWidgetItem* item, int column)
{
    if (item->childCount() > 0)     return;
    QString leafNodeText = item->text(column);

    QTreeWidgetItem* currentItem = item;
    QTreeWidgetItem* rootItem = currentItem;

    while (currentItem->parent() != nullptr) {
        rootItem = currentItem->parent();
        currentItem = rootItem;
    }

    // 4. 获取根节点文本
    QString rootNodeText = rootItem->text(column);
    if (rootNodeText == "学科反演模型")
    {
        ui.stackedWidget_2->setCurrentIndex(0);

        if (leafNodeText == "⽓动压⼒场")     ui.stackedWidget_2->setCurrentIndex(0);
        else if (leafNodeText == "⽓动热流场")ui.stackedWidget_2->setCurrentIndex(5);
        else if (leafNodeText == "结构应变场")ui.stackedWidget_2->setCurrentIndex(6);
        else if (leafNodeText == "结构温度场")ui.stackedWidget_2->setCurrentIndex(7);

    }
    else if(rootNodeText == "学科预测模型")
    {

        if (leafNodeText == "⽓动压⼒场")    ui.stackedWidget_2->setCurrentIndex(1);
        else if(leafNodeText == "⽓动热流场")ui.stackedWidget_2->setCurrentIndex(4);
        else                                 ui.stackedWidget_2->setCurrentIndex(3);
    }
    else
    {
        ui.stackedWidget_2->setCurrentIndex(2);
    }
}

namespace flightenv::platform_ui::internal {

QString findWorkspacePath(const QString& relativePath)
{
    QDir dir(QDir::currentPath());
    for (int i = 0; i < 8; ++i) {
        const QString candidate = dir.filePath(relativePath);
        if (QFile::exists(candidate)) {
            return candidate;
        }
        if (!dir.cdUp()) {
            break;
        }
    }
    return QString();
}

QString findNewestSummary(const QString& relativeDir, const QString& fileName)
{
    const QString root = findWorkspacePath(relativeDir);
    if (root.isEmpty()) {
        return QString();
    }
    QString newest;
    QDateTime newestTime;
    QDirIterator it(root, QStringList() << fileName, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QFileInfo info(path);
        if (newest.isEmpty() || info.lastModified() > newestTime) {
            newest = path;
            newestTime = info.lastModified();
        }
    }
    return newest;
}

QJsonObject readJsonObject(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    return doc.isObject() ? doc.object() : QJsonObject{};
}

QString jsonText(const QJsonObject& object, const QString& key, const QString& fallback = QStringLiteral("-"))
{
    const QJsonValue value = object.value(key);
    if (value.isString()) {
        return value.toString();
    }
    if (value.isDouble()) {
        return QString::number(value.toDouble(), 'g', 10);
    }
    if (value.isBool()) {
        return value.toBool() ? QStringLiteral("是") : QStringLiteral("否");
    }
    return fallback;
}

QString jsonNumberText(const QJsonObject& object, const QString& key, const QString& fallback = QStringLiteral("-"))
{
    const QJsonValue value = object.value(key);
    return value.isDouble() ? QString::number(value.toDouble(), 'g', 10) : fallback;
}

QString objectPackagePath(const QString& relativePath)
{
    return workspacePath(QStringLiteral("flightenv-object-reentry-vehicle/") + relativePath);
}

QString objectResourcePath(const QString& pathOrUri)
{
    if (pathOrUri.isEmpty()) {
        return QStringLiteral("-");
    }
    if (pathOrUri.startsWith(QStringLiteral("object://"))) {
        return objectPackagePath(pathOrUri.mid(QStringLiteral("object://").size()));
    }
    if (QDir::isAbsolutePath(pathOrUri)) {
        return QDir::toNativeSeparators(pathOrUri);
    }
    if (pathOrUri.startsWith(QStringLiteral("_deps/")) ||
        pathOrUri.startsWith(QStringLiteral("_local_artifacts/"))) {
        return workspacePath(pathOrUri);
    }
    return objectPackagePath(pathOrUri);
}

QString canonicalMetricLabel(const QString& label)
{
    if (label.endsWith(QStringLiteral("components.ballistic.time_s"))) return QStringLiteral("posterior.components.ballistic.time_s");
    if (label.endsWith(QStringLiteral("components.ballistic.h"))) return QStringLiteral("posterior.components.ballistic.h");
    if (label.endsWith(QStringLiteral("components.ballistic.ma"))) return QStringLiteral("posterior.components.ballistic.ma");
    if (label.endsWith(QStringLiteral("components.ballistic.alpha"))) return QStringLiteral("posterior.components.ballistic.alpha");
    if (label.endsWith(QStringLiteral("components.ballistic.q"))) return QStringLiteral("posterior.components.ballistic.q");
    if (label.endsWith(QStringLiteral("field.pressure.max"))) return QStringLiteral("field.pressure.max");
    if (label.endsWith(QStringLiteral("field.heatflux.max"))) return QStringLiteral("field.heatflux.max");
    if (label.endsWith(QStringLiteral("field.strain.max"))) return QStringLiteral("field.strain.max");
    if (label.endsWith(QStringLiteral("field.temperature.max"))) return QStringLiteral("field.temperature.max");
    if (label.endsWith(QStringLiteral("field.damage.next.max"))) return QStringLiteral("field.damage.next.max");
    if (label.endsWith(QStringLiteral("field.ablation.next.max"))) return QStringLiteral("field.ablation.next.max");
    if (label.endsWith(QStringLiteral("field.rul.min"))) return QStringLiteral("field.rul.min");
    if (label.endsWith(QStringLiteral("step.public_time_s"))) return QStringLiteral("step.public_time_s");
    if (label.endsWith(QStringLiteral("step.failed_nodes"))) return QStringLiteral("step.failed_nodes");
    return label;
}

QString displayMetricLabel(const QString& label)
{
    if (label == QStringLiteral("posterior.components.ballistic.time_s")) return QStringLiteral("弹道时间");
    if (label == QStringLiteral("posterior.components.ballistic.h")) return QStringLiteral("弹道高度");
    if (label == QStringLiteral("posterior.components.ballistic.ma")) return QStringLiteral("马赫数");
    if (label == QStringLiteral("posterior.components.ballistic.alpha")) return QStringLiteral("攻角");
    if (label == QStringLiteral("posterior.components.ballistic.q")) return QStringLiteral("动压");
    if (label == QStringLiteral("field.pressure.max")) return QStringLiteral("最大压力");
    if (label == QStringLiteral("field.heatflux.max")) return QStringLiteral("最大热流");
    if (label == QStringLiteral("field.strain.max")) return QStringLiteral("最大应变/应力");
    if (label == QStringLiteral("field.temperature.max")) return QStringLiteral("最高温度");
    if (label == QStringLiteral("field.damage.next.max")) return QStringLiteral("最大结构损伤");
    if (label == QStringLiteral("field.ablation.next.max")) return QStringLiteral("最大防热烧蚀");
    if (label == QStringLiteral("field.rul.min")) return QStringLiteral("最小剩余寿命");
    if (label == QStringLiteral("step.public_time_s")) return QStringLiteral("平台当前时间");
    if (label == QStringLiteral("step.failed_nodes")) return QStringLiteral("异常算子数");
    return label;
}

QString displayMetricUnit(const QString& label, const QString& unit)
{
    if (!unit.isEmpty()) return unit;
    if (label == QStringLiteral("posterior.components.ballistic.time_s")) return QStringLiteral("s");
    if (label == QStringLiteral("posterior.components.ballistic.h")) return QStringLiteral("m");
    if (label == QStringLiteral("posterior.components.ballistic.ma")) return QStringLiteral("-");
    if (label == QStringLiteral("posterior.components.ballistic.alpha")) return QStringLiteral("rad");
    if (label == QStringLiteral("posterior.components.ballistic.q")) return QStringLiteral("Pa");
    return unit;
}

bool shouldDisplaySeriesMetric(const QString& label)
{
    return label == QStringLiteral("posterior.components.ballistic.time_s") ||
        label == QStringLiteral("posterior.components.ballistic.h") ||
        label == QStringLiteral("posterior.components.ballistic.ma") ||
        label == QStringLiteral("posterior.components.ballistic.alpha") ||
        label == QStringLiteral("posterior.components.ballistic.q") ||
        label == QStringLiteral("field.pressure.max") ||
        label == QStringLiteral("field.heatflux.max") ||
        label == QStringLiteral("field.strain.max") ||
        label == QStringLiteral("field.temperature.max") ||
        label == QStringLiteral("field.damage.next.max") ||
        label == QStringLiteral("field.ablation.next.max") ||
        label == QStringLiteral("field.rul.min") ||
        label == QStringLiteral("step.public_time_s") ||
        label == QStringLiteral("step.failed_nodes");
}

bool isDisplayableScalarValue(const QJsonValue& value)
{
    if (value.isDouble() || value.isBool()) {
        return true;
    }
    if (!value.isString()) {
        return false;
    }
    const QString text = value.toString();
    return !text.startsWith(QStringLiteral("runtime://")) &&
        !text.contains(QStringLiteral("typed-buffer"), Qt::CaseInsensitive) &&
        text.size() <= 120;
}

QString scalarValueText(const QJsonValue& value)
{
    if (value.isDouble()) {
        return QString::number(value.toDouble(), 'g', 10);
    }
    if (value.isBool()) {
        return value.toBool() ? QStringLiteral("是") : QStringLiteral("否");
    }
    if (value.isString()) {
        return value.toString();
    }
    return QStringLiteral("-");
}

QString workflowStageLabel(const QString& stageId)
{
    if (stageId == QStringLiteral("state_transition")) return QStringLiteral("状态转移");
    if (stageId == QStringLiteral("observation_equation")) return QStringLiteral("观测方程");
    if (stageId == QStringLiteral("filter_algorithm")) return QStringLiteral("滤波融合");
    if (stageId == QStringLiteral("posterior_field_reconstruction")) return QStringLiteral("多场重构/累积");
    if (stageId == QStringLiteral("failure_qoi")) return QStringLiteral("风险与寿命评估");
    return stageId;
}

QString operatorDisplayName(const QString& operatorRef, const QString& nodeId)
{
    const QString key = operatorRef + QStringLiteral(" ") + nodeId;
    if (key.contains(QStringLiteral("ballistic"), Qt::CaseInsensitive)) return QStringLiteral("弹道状态更新");
    if (key.contains(QStringLiteral("pressure_coef"), Qt::CaseInsensitive)) return QStringLiteral("压力系数预测");
    if (key.contains(QStringLiteral("heatflux_coef"), Qt::CaseInsensitive)) return QStringLiteral("热流系数预测");
    if (key.contains(QStringLiteral("structure_coef"), Qt::CaseInsensitive)) return QStringLiteral("结构系数预测");
    if (key.contains(QStringLiteral("pressure_field"), Qt::CaseInsensitive)) return QStringLiteral("压力场重构");
    if (key.contains(QStringLiteral("heatflux_field"), Qt::CaseInsensitive)) return QStringLiteral("热流场重构");
    if (key.contains(QStringLiteral("strain_field"), Qt::CaseInsensitive)) return QStringLiteral("应变场重构");
    if (key.contains(QStringLiteral("temperature_field"), Qt::CaseInsensitive)) return QStringLiteral("温度场重构");
    if (key.contains(QStringLiteral("damage"), Qt::CaseInsensitive)) return QStringLiteral("结构损伤累积");
    if (key.contains(QStringLiteral("ablation"), Qt::CaseInsensitive)) return QStringLiteral("防热烧蚀累积");
    if (key.contains(QStringLiteral("particle_filter"), Qt::CaseInsensitive)) return QStringLiteral("粒子滤波融合");
    if (key.contains(QStringLiteral("pressure_sensor"), Qt::CaseInsensitive)) return QStringLiteral("压力传感器映射");
    if (key.contains(QStringLiteral("heatflux_sensor"), Qt::CaseInsensitive)) return QStringLiteral("热流传感器映射");
    if (key.contains(QStringLiteral("strain_sensor"), Qt::CaseInsensitive)) return QStringLiteral("应变传感器映射");
    if (key.contains(QStringLiteral("temperature_sensor"), Qt::CaseInsensitive)) return QStringLiteral("温度传感器映射");
    if (key.contains(QStringLiteral("ballistic_observation"), Qt::CaseInsensitive)) return QStringLiteral("弹道观测映射");
    if (key.contains(QStringLiteral("shell_failure"), Qt::CaseInsensitive)) return QStringLiteral("外壳失效评估");
    if (key.contains(QStringLiteral("structure_failure"), Qt::CaseInsensitive)) return QStringLiteral("结构失效评估");
    if (key.contains(QStringLiteral("remaining_life"), Qt::CaseInsensitive)) return QStringLiteral("剩余寿命评估");
    return nodeId.isEmpty() ? operatorRef : nodeId;
}

void setTableText(QTableWidget* table, int row, int col, const QString& text)
{
    auto* item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    table->setItem(row, col, item);
}

QString findWorkspaceRoot()
{
    QDir dir(QDir::currentPath());
    for (int i = 0; i < 8; ++i) {
        if (QFile::exists(dir.filePath(QStringLiteral("FlightEnvMultiRepo.sln")))) {
            return QDir::toNativeSeparators(dir.absolutePath());
        }
        if (!dir.cdUp()) {
            break;
        }
    }
    return QDir::toNativeSeparators(QDir::currentPath());
}

QString workspacePath(const QString& relativePath)
{
    return QDir::toNativeSeparators(QDir(findWorkspaceRoot()).filePath(relativePath));
}

QString joinStdStrings(const std::vector<std::string>& values, const int maxItems = 4)
{
    QStringList parts;
    const int n = std::min(static_cast<int>(values.size()), maxItems);
    for (int i = 0; i < n; ++i) {
        parts << QString::fromUtf8(values[static_cast<std::size_t>(i)].c_str());
    }
    if (static_cast<int>(values.size()) > maxItems) {
        parts << QString::fromUtf8("+%1").arg(static_cast<int>(values.size()) - maxItems);
    }
    return parts.join(QStringLiteral(", "));
}

QString liveStatusText(const std::string& status)
{
    const QString value = QString::fromUtf8(status.c_str());
    if (value == QStringLiteral("pending")) return QString::fromUtf8("等待");
    if (value == QStringLiteral("running")) return QString::fromUtf8("运行中");
    if (value == QStringLiteral("ok")) return QString::fromUtf8("完成");
    if (value == QStringLiteral("failed")) return QString::fromUtf8("失败");
    if (value == QStringLiteral("disabled")) return QString::fromUtf8("禁用");
    if (value == QStringLiteral("missing")) return QString::fromUtf8("等待状态文件");
    return value.isEmpty() ? QString::fromUtf8("未知") : value;
}

QString liveStatusStyle(const QString& status)
{
    if (status == QStringLiteral("running")) {
        return QStringLiteral("QGroupBox{border:1px solid #2563eb;border-radius:6px;margin-top:8px;background:#eff6ff;} QGroupBox::title{subcontrol-origin:margin;left:8px;padding:0 4px;color:#1d4ed8;font-weight:bold;}");
    }
    if (status == QStringLiteral("ok")) {
        return QStringLiteral("QGroupBox{border:1px solid #16a34a;border-radius:6px;margin-top:8px;background:#f0fdf4;} QGroupBox::title{subcontrol-origin:margin;left:8px;padding:0 4px;color:#166534;font-weight:bold;}");
    }
    if (status == QStringLiteral("failed")) {
        return QStringLiteral("QGroupBox{border:1px solid #dc2626;border-radius:6px;margin-top:8px;background:#fef2f2;} QGroupBox::title{subcontrol-origin:margin;left:8px;padding:0 4px;color:#991b1b;font-weight:bold;}");
    }
    return QStringLiteral("QGroupBox{border:1px solid #cbd5e1;border-radius:6px;margin-top:8px;background:#f8fafc;} QGroupBox::title{subcontrol-origin:margin;left:8px;padding:0 4px;color:#334155;font-weight:bold;}");
}

QLabel* namedLabel(QWidget* parent, const char* name)
{
    return parent ? parent->findChild<QLabel*>(QString::fromLatin1(name)) : nullptr;
}

QString compactPathText(const std::string& value, const int tailChars = 68)
{
    QString text = QString::fromUtf8(value.c_str());
    text = QDir::toNativeSeparators(text);
    if (text.size() <= tailChars) {
        return text;
    }
    return QStringLiteral("...") + text.right(tailChars);
}

QString formatNumber(const double value, const int precision = 4)
{
    if (!std::isfinite(value)) {
        return QStringLiteral("-");
    }
    return QString::number(value, 'g', precision);
}

QString jsonString(const nlohmann::json& j, const char* key, const QString& fallback = QStringLiteral("-"))
{
    const auto it = j.find(key);
    if (it == j.end() || it->is_null()) {
        return fallback;
    }
    if (it->is_string()) {
        return QString::fromUtf8(it->get<std::string>().c_str());
    }
    if (it->is_boolean()) {
        return it->get<bool>() ? QStringLiteral("true") : QStringLiteral("false");
    }
    if (it->is_number_float()) {
        return formatNumber(it->get<double>());
    }
    if (it->is_number_integer()) {
        return QString::number(it->get<long long>());
    }
    if (it->is_number_unsigned()) {
        return QString::number(static_cast<qulonglong>(it->get<unsigned long long>()));
    }
    return QString::fromUtf8(it->dump().c_str());
}

double jsonDouble(const nlohmann::json& j, const char* key, const double fallback = 0.0)
{
    const auto it = j.find(key);
    if (it == j.end() || !it->is_number()) {
        return fallback;
    }
    return it->get<double>();
}

QTableWidget* makeGraphTable(QWidget* parent, const QStringList& headers)
{
    auto* table = new QTableWidget(0, headers.size(), parent);
    table->setHorizontalHeaderLabels(headers);
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setAlternatingRowColors(true);
    table->setWordWrap(false);
    return table;
}

void clearGraphTable(QTableWidget* table)
{
    if (table) {
        table->setRowCount(0);
    }
}

void appendGraphRow(QTableWidget* table, const QStringList& values)
{
    if (!table) {
        return;
    }
    const int row = table->rowCount();
    table->insertRow(row);
    const int cols = std::min(table->columnCount(), static_cast<int>(values.size()));
    for (int col = 0; col < cols; ++col) {
        setTableText(table, row, col, values[col]);
    }
}

void setLabelText(QLabel* label, const QString& text);

std::optional<nlohmann::json> parseJsonText(const std::string& text)
{
    if (text.empty()) {
        return std::nullopt;
    }
    try {
        return nlohmann::json::parse(text);
    }
    catch (...) {
        return std::nullopt;
    }
}

QString jsonValueSummary(const nlohmann::json& value, const int maxItems = 4)
{
    if (value.is_string()) {
        return QString::fromUtf8(value.get<std::string>().c_str());
    }
    if (value.is_boolean()) {
        return value.get<bool>() ? QStringLiteral("true") : QStringLiteral("false");
    }
    if (value.is_number_float()) {
        return formatNumber(value.get<double>());
    }
    if (value.is_number_integer()) {
        return QString::number(value.get<long long>());
    }
    if (value.is_number_unsigned()) {
        return QString::number(static_cast<qulonglong>(value.get<unsigned long long>()));
    }
    if (value.is_array()) {
        QStringList parts;
        const int n = std::min(static_cast<int>(value.size()), maxItems);
        for (int i = 0; i < n; ++i) {
            parts << jsonValueSummary(value[static_cast<std::size_t>(i)], 1);
        }
        if (static_cast<int>(value.size()) > maxItems) {
            parts << QString::fromUtf8("+%1").arg(static_cast<int>(value.size()) - maxItems);
        }
        return parts.join(QStringLiteral(", "));
    }
    if (value.is_object()) {
        QStringList parts;
        int count = 0;
        for (auto it = value.begin(); it != value.end() && count < maxItems; ++it, ++count) {
            parts << QString::fromUtf8("%1=%2")
                .arg(QString::fromUtf8(it.key().c_str()), jsonValueSummary(it.value(), 1));
        }
        if (static_cast<int>(value.size()) > maxItems) {
            parts << QString::fromUtf8("+%1").arg(static_cast<int>(value.size()) - maxItems);
        }
        return parts.join(QStringLiteral("; "));
    }
    return QStringLiteral("-");
}

QString jsonFieldSummary(
    const std::optional<nlohmann::json>& object,
    const char* key,
    const QString& fallback = QStringLiteral("-"))
{
    if (!object || !object->is_object()) {
        return fallback;
    }
    const auto it = object->find(key);
    if (it == object->end() || it->is_null()) {
        return fallback;
    }
    const QString text = jsonValueSummary(*it);
    return text.isEmpty() ? fallback : text;
}

std::map<std::string, launchsupport::ModelAssetDTO> modelMapById(
    const std::vector<launchsupport::ModelAssetDTO>& models)
{
    std::map<std::string, launchsupport::ModelAssetDTO> byId;
    for (const auto& model : models) {
        byId[model.model_id] = model;
    }
    return byId;
}

QString modelTypeForBinding(
    const launchsupport::ModelBindingDTO& binding,
    const std::map<std::string, launchsupport::ModelAssetDTO>& modelsById)
{
    const auto it = modelsById.find(binding.model_id);
    if (it == modelsById.end()) {
        return QStringLiteral("-");
    }
    return QString::fromUtf8(it->second.model_type.c_str());
}

QString bindingModelSummary(
    const std::vector<launchsupport::ModelBindingDTO>& bindings,
    const std::map<std::string, launchsupport::ModelAssetDTO>& modelsById,
    const std::string& objectId)
{
    QStringList parts;
    for (const auto& binding : bindings) {
        if (!binding.enabled || binding.object_id != objectId) {
            continue;
        }
        const QString modelType = modelTypeForBinding(binding, modelsById);
        parts << QString::fromUtf8("%1:%2(p%3)")
            .arg(modelType, QString::fromUtf8(binding.model_id.c_str()))
            .arg(binding.priority);
    }
    return parts.isEmpty() ? QStringLiteral("-") : parts.join(QStringLiteral("; "));
}

QString firstModelForType(
    const std::vector<launchsupport::ModelBindingDTO>& bindings,
    const std::map<std::string, launchsupport::ModelAssetDTO>& modelsById,
    const std::string& objectId,
    const QString& modelType)
{
    int bestPriority = std::numeric_limits<int>::min();
    QString best;
    for (const auto& binding : bindings) {
        if (!binding.enabled || binding.object_id != objectId) {
            continue;
        }
        if (modelTypeForBinding(binding, modelsById) != modelType) {
            continue;
        }
        if (binding.priority > bestPriority) {
            bestPriority = binding.priority;
            best = QString::fromUtf8(binding.model_id.c_str());
        }
    }
    return best.isEmpty() ? QStringLiteral("-") : best;
}

QString envelopeStatus(const std::string& envelopeJson)
{
    const auto parsed = parseJsonText(envelopeJson);
    if (!parsed || !parsed->is_object()) {
        return envelopeJson.empty() ? QStringLiteral("-") : compactPathText(envelopeJson, 80);
    }
    QStringList parts;
    for (const char* key : {"status", "database_asset", "geometry_asset", "pod_asset", "bpnn_asset"}) {
        const auto value = jsonFieldSummary(parsed, key);
        if (value != QStringLiteral("-")) {
            parts << QString::fromUtf8("%1=%2").arg(QString::fromUtf8(key), value);
        }
    }
    return parts.isEmpty() ? jsonValueSummary(*parsed, 3) : parts.join(QStringLiteral("; "));
}

void populateCatalogWorkbench(
    const QString& catalogPathText,
    QLabel* summaryLabel,
    QTableWidget* objectTable,
    QTableWidget* bindingTable,
    QTableWidget* modelTable)
{
    clearGraphTable(objectTable);
    clearGraphTable(bindingTable);
    clearGraphTable(modelTable);

    const QString catalogPath = catalogPathText.trimmed();
    if (catalogPath.isEmpty() || !QFileInfo::exists(catalogPath)) {
        setLabelText(summaryLabel, QString::fromUtf8("对象画像：catalog 不存在，无法加载对象/模型资产绑定。"));
        return;
    }

    try {
        launchsupport::PlatformCatalogReader reader;
        reader.open(
            std::filesystem::path(catalogPath.toStdWString()),
            std::filesystem::path(findWorkspaceRoot().toStdWString()));
        auto objects = reader.read_objects();
        auto models = reader.read_models();
        auto bindings = reader.read_bindings();
        const auto modelsById = modelMapById(models);

        std::sort(bindings.begin(), bindings.end(), [](const auto& lhs, const auto& rhs) {
            if (lhs.object_id != rhs.object_id) return lhs.object_id < rhs.object_id;
            if (lhs.enabled != rhs.enabled) return lhs.enabled > rhs.enabled;
            return lhs.priority > rhs.priority;
        });

        int enabledBindingCount = 0;
        for (const auto& binding : bindings) {
            if (binding.enabled) {
                ++enabledBindingCount;
            }
            const auto it = modelsById.find(binding.model_id);
            const QString type = it == modelsById.end()
                ? QStringLiteral("-")
                : QString::fromUtf8("%1 / %2")
                    .arg(QString::fromUtf8(it->second.model_type.c_str()),
                         QString::fromUtf8(it->second.physics_domain.c_str()));
            appendGraphRow(bindingTable, {
                QString::fromUtf8("%1 / %2").arg(
                    QString::fromUtf8(binding.object_id.c_str()),
                    QString::fromUtf8(binding.binding_id.c_str())),
                type,
                QString::fromUtf8(binding.model_id.c_str()),
                QString::fromUtf8(binding.valid_phase.c_str()),
                QString::fromUtf8("priority=%1, %2")
                    .arg(binding.priority)
                    .arg(binding.enabled ? QString::fromUtf8("启用") : QString::fromUtf8("禁用")),
                QString::fromUtf8(binding.config_json.c_str())
            });
        }

        for (const auto& model : models) {
            appendGraphRow(modelTable, {
                QString::fromUtf8(model.model_id.c_str()),
                QString::fromUtf8("%1 / %2").arg(
                    QString::fromUtf8(model.model_type.c_str()),
                    QString::fromUtf8(model.physics_domain.c_str())),
                QString::fromUtf8("%1 / %2").arg(
                    QString::fromUtf8(model.runtime_type.c_str()),
                    QString::fromUtf8(model.version.c_str())),
                compactPathText(model.artifact_ref),
                envelopeStatus(model.applicable_envelope_json)
            });
        }

        for (const auto& object : objects) {
            const auto metadata = parseJsonText(object.metadata_json);
            const QString geometryAssets = jsonFieldSummary(metadata, "geometry_assets", QString::fromUtf8(object.geometry_ref.c_str()));
            const QString databaseAssets = jsonFieldSummary(metadata, "database_assets");
            const QString modelGroups = jsonFieldSummary(metadata, "model_asset_groups");
            appendGraphRow(objectTable, {
                QString::fromUtf8("%1 / %2").arg(
                    QString::fromUtf8(object.object_id.c_str()),
                    QString::fromUtf8(object.name.c_str())),
                geometryAssets,
                QString::fromUtf8("数据库=%1；模型组=%2").arg(databaseAssets, modelGroups),
                bindingModelSummary(bindings, modelsById, object.object_id),
                QString::fromUtf8("catalog 对象粒度：%1，状态=%2").arg(
                    QString::fromUtf8(object.object_type.c_str()),
                    QString::fromUtf8(object.status.c_str()))
            });

            // 当前 catalog 仍以 vehicle 为主对象；这里按网格和 subject 语义把对象画像展开，
            // 让 UI 能表达“外壳/内部结构使用不同场、传感器和损伤模型”的平台目标。
            appendGraphRow(objectTable, {
                QString::fromUtf8("%1 / 外壳气动热环境").arg(QString::fromUtf8(object.object_id.c_str())),
                QString::fromUtf8("surface_mesh: %1").arg(jsonFieldSummary(metadata, "geometry_assets")),
                QString::fromUtf8("P=压力场，K=热流场；对应外表面传感器/数据库观测"),
                QString::fromUtf8("trajectory=%1；field=%2；damage=%3；life=%4").arg(
                    firstModelForType(bindings, modelsById, object.object_id, QStringLiteral("trajectory")),
                    firstModelForType(bindings, modelsById, object.object_id, QStringLiteral("field_prediction")),
                    firstModelForType(bindings, modelsById, object.object_id, QStringLiteral("damage_assessment")),
                    firstModelForType(bindings, modelsById, object.object_id, QStringLiteral("life_assessment"))),
                QString::fromUtf8("用于气动压力/热流 QoI、外壳热防护损伤与寿命场。")
            });
            appendGraphRow(objectTable, {
                QString::fromUtf8("%1 / 内部结构热-力响应").arg(QString::fromUtf8(object.object_id.c_str())),
                QString::fromUtf8("internal_mesh: %1").arg(jsonFieldSummary(metadata, "geometry_assets")),
                QString::fromUtf8("S=应变场，T=温度场；对应内部结构传感器/数据库观测"),
                QString::fromUtf8("field=%1；damage=%2；life=%3").arg(
                    firstModelForType(bindings, modelsById, object.object_id, QStringLiteral("field_prediction")),
                    firstModelForType(bindings, modelsById, object.object_id, QStringLiteral("damage_assessment")),
                    firstModelForType(bindings, modelsById, object.object_id, QStringLiteral("life_assessment"))),
                QString::fromUtf8("用于内部热-力场重构、结构累积损伤与剩余寿命场。")
            });
        }

        setLabelText(summaryLabel,
            QString::fromUtf8("对象画像：objects=%1，models=%2，bindings=%3（启用 %4）。当前 UI 从 catalog 读取真实对象/模型资产绑定，并按外壳/内部结构语义展开显示。")
                .arg(static_cast<int>(objects.size()))
                .arg(static_cast<int>(models.size()))
                .arg(static_cast<int>(bindings.size()))
                .arg(enabledBindingCount));
    }
    catch (const std::exception& e) {
        setLabelText(summaryLabel,
            QString::fromUtf8("对象画像读取失败：%1").arg(QString::fromUtf8(e.what())));
    }
}

QString equationRelationText(const QString& graphName, const launchsupport::GraphOperatorNodeView& node)
{
    const QString type = QString::fromUtf8(node.operator_type.c_str());
    if (graphName.contains(QString::fromUtf8("在线滤波"))) {
        if (type == QStringLiteral("state_transition")) {
            return QString::fromUtf8("状态转移方程：由上一 posterior 和 runtime_step 得到预测先验 state.predicted。");
        }
        if (type == QStringLiteral("observation_equation")) {
            return QString::fromUtf8("观测方程：把传感器/数据库观测映射到 observation.predicted。");
        }
        if (type == QStringLiteral("filter_algorithm")) {
            return QString::fromUtf8("滤波算法：同时消费状态转移先验和观测方程输出，更新 state.posterior；不是简单顺序串行替代关系。");
        }
    }
    if (type == QStringLiteral("state_transition")) {
        return QString::fromUtf8("预测大算子：从当前在线状态向未来推进，可挂弹道、场系数预测等子算子。");
    }
    if (type == QStringLiteral("qoi_equation")) {
        return QString::fromUtf8("QoI 大算子：把未来状态/场系数还原为可展示场、损伤累计、失效判据和剩余寿命场。");
    }
    if (type == QStringLiteral("field_reconstruction")) {
        return QString::fromUtf8("场重构子算子：P/K/S/T 系数到真实飞船网格场值。");
    }
    if (type == QStringLiteral("field_merge")) {
        return QString::fromUtf8("场合并子算子：把多个 subject/field 输出合并为统一 QoI 输入。");
    }
    return QString::fromUtf8("由算子模板声明输入、输出和执行方式，可替换为 DLL 或外部节点。");
}

void appendEquationRowsFromView(
    QTableWidget* table,
    const QString& graphName,
    const std::vector<launchsupport::GraphOperatorNodeView>& nodes)
{
    int index = 0;
    for (const auto& node : nodes) {
        appendGraphRow(table, {
            graphName,
            QString::fromUtf8("%1. %2").arg(++index).arg(QString::fromUtf8(node.node_id.c_str())),
            joinStdStrings(node.inputs, 6),
            joinStdStrings(node.outputs, 6),
            equationRelationText(graphName, node)
        });
    }
}

void appendConceptualOnlineEquationRows(QTableWidget* table)
{
    appendGraphRow(table, {
        QString::fromUtf8("在线滤波子图"),
        QString::fromUtf8("state_transition"),
        QStringLiteral("state.posterior + runtime_step"),
        QStringLiteral("state.predicted"),
        QString::fromUtf8("状态转移方程产生先验。")
    });
    appendGraphRow(table, {
        QString::fromUtf8("在线滤波子图"),
        QString::fromUtf8("observation_equation"),
        QStringLiteral("sensor.observation + runtime_step"),
        QStringLiteral("observation.predicted"),
        QString::fromUtf8("观测方程产生传感器预测/似然输入。")
    });
    appendGraphRow(table, {
        QString::fromUtf8("在线滤波子图"),
        QString::fromUtf8("filter_algorithm"),
        QStringLiteral("state.predicted + observation.predicted + sensor.observation"),
        QStringLiteral("state.posterior"),
        QString::fromUtf8("滤波算法同时使用状态转移和观测方程。")
    });
}

void appendConceptualPredictionEquationRows(QTableWidget* table)
{
    appendGraphRow(table, {
        QString::fromUtf8("预测/QoI 子图"),
        QString::fromUtf8("state_transition"),
        QStringLiteral("state.initial + control.forecast"),
        QStringLiteral("state.future"),
        QString::fromUtf8("未来预测从当前在线状态开始推进，内部可选弹道/场系数子算子。")
    });
    appendGraphRow(table, {
        QString::fromUtf8("预测/QoI 子图"),
        QString::fromUtf8("qoi_equation"),
        QStringLiteral("state.future + damage.current"),
        QStringLiteral("field.forecast + damage.forecast + life.field"),
        QString::fromUtf8("QoI 负责场重构、损伤累计、失效判据和剩余寿命场。")
    });
}

void appendConceptualEquationRows(QTableWidget* table)
{
    appendConceptualOnlineEquationRows(table);
    appendConceptualPredictionEquationRows(table);
}

void setLabelText(QLabel* label, const QString& text)
{
    if (label) {
        label->setText(text);
    }
}

std::optional<nlohmann::json> readJsonFileNlohmann(const std::filesystem::path& path, QString* error)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) {
            *error = QString::fromUtf8("无法打开 JSON：%1").arg(QString::fromStdWString(path.wstring()));
        }
        return std::nullopt;
    }
    try {
        return nlohmann::json::parse(in);
    }
    catch (const std::exception& e) {
        if (error) {
            *error = QString::fromUtf8(e.what());
        }
        return std::nullopt;
    }
}

std::optional<nlohmann::json> parsePortPayload(
    const std::vector<launchsupport::GraphResultPortView>& ports,
    const std::string& portName,
    QString* error)
{
    const auto it = std::find_if(ports.begin(), ports.end(), [&](const auto& port) {
        return port.port_name == portName;
    });
    if (it == ports.end() || it->payload_json.empty()) {
        if (error) {
            *error = QString::fromUtf8("缺少 result port：%1").arg(QString::fromUtf8(portName.c_str()));
        }
        return std::nullopt;
    }
    try {
        return nlohmann::json::parse(it->payload_json);
    }
    catch (const std::exception& e) {
        if (error) {
            *error = QString::fromUtf8("%1: %2")
                .arg(QString::fromUtf8(portName.c_str()), QString::fromUtf8(e.what()));
        }
        return std::nullopt;
    }
}

QString subjectText(const contracts::SubjectType subject)
{
    return QString::fromUtf8(contracts::to_string(subject).c_str());
}

struct ValueStats {
    std::size_t count = 0;
    double min = std::numeric_limits<double>::infinity();
    double max = -std::numeric_limits<double>::infinity();
    double sum = 0.0;
};

void addStats(ValueStats& stats, const double value)
{
    if (!std::isfinite(value)) {
        return;
    }
    ++stats.count;
    stats.min = std::min(stats.min, value);
    stats.max = std::max(stats.max, value);
    stats.sum += value;
}

QString statsText(const ValueStats& stats)
{
    if (stats.count == 0) {
        return QStringLiteral("-");
    }
    const double mean = stats.sum / static_cast<double>(stats.count);
    return QString::fromUtf8("min=%1  max=%2  mean=%3")
        .arg(formatNumber(stats.min), formatNumber(stats.max), formatNumber(mean));
}

contracts::SubjectType selectedSubject(QComboBox* combo, const contracts::SubjectType fallback)
{
    if (!combo) {
        return fallback;
    }
    const QString token = combo->currentData().toString();
    if (token.isEmpty()) {
        return fallback;
    }
    return contracts::subject_type_from_string(token.toStdString());
}

QComboBox* makeSubjectCombo(QWidget* parent, const QString& tooltip)
{
    auto* combo = new QComboBox(parent);
    combo->addItem(QStringLiteral("压力 P"), QStringLiteral("P"));
    combo->addItem(QStringLiteral("热流 K"), QStringLiteral("K"));
    combo->addItem(QStringLiteral("应力 S"), QStringLiteral("S"));
    combo->addItem(QStringLiteral("温度 T"), QStringLiteral("T"));
    combo->setToolTip(tooltip);
    return combo;
}

contracts::TrajectoryPredictionFrame trajectoryFrameFromGraphJson(const nlohmann::json& value)
{
    contracts::TrajectoryPredictionFrame frame;
    if (value.is_object()) {
        frame = value.get<contracts::TrajectoryPredictionFrame>();
    }
    frame.samples.clear();
    const auto samples = value.value("samples", nlohmann::json::array());
    for (const auto& sample : samples) {
        const auto wrapped = sample.find("trajectory_sample");
        if (wrapped != sample.end() && wrapped->is_object()) {
            frame.samples.push_back(wrapped->get<contracts::TrajectorySampleDTO>());
        }
        else if (sample.is_object()) {
            frame.samples.push_back(sample.get<contracts::TrajectorySampleDTO>());
        }
    }
    if (frame.horizon_s <= 0.0 && !frame.samples.empty()) {
        frame.horizon_s = frame.samples.back().time_s;
    }
    if (frame.step_s <= 0.0 && frame.samples.size() >= 2) {
        frame.step_s = std::max(0.0, frame.samples[1].time_s - frame.samples[0].time_s);
    }
    return frame;
}

contracts::FieldBundleDTO fieldBundleFromForecastStep(
    const contracts::FieldForecastStepDTO& step,
    const contracts::SubjectType subject)
{
    contracts::FieldBundleDTO bundle;
    for (const auto& tensor : step.fields) {
        if (tensor.subject != subject) {
            continue;
        }
        contracts::SubjectFieldDTO item;
        item.subject = tensor.subject;
        item.taskpoint_id = tensor.taskpoint_id;
        item.values = tensor.values;
        bundle.items.push_back(std::move(item));
    }
    return bundle;
}

std::optional<contracts::FieldBundleDTO> latestFieldBundleForSubject(
    const contracts::FieldForecastFrame& frame,
    const contracts::SubjectType subject)
{
    for (auto it = frame.steps.rbegin(); it != frame.steps.rend(); ++it) {
        auto bundle = fieldBundleFromForecastStep(*it, subject);
        if (!bundle.items.empty()) {
            return bundle;
        }
    }
    return std::nullopt;
}

std::optional<contracts::FieldBundleDTO> latestDamageFieldForSubject(
    const contracts::DamageForecastFrame& frame,
    const contracts::SubjectType subject)
{
    for (auto it = frame.steps.rbegin(); it != frame.steps.rend(); ++it) {
        contracts::FieldBundleDTO bundle;
        for (const auto& item : it->cumulative_damage_field.items) {
            if (item.subject == subject) {
                bundle.items.push_back(item);
            }
        }
        if (!bundle.items.empty()) {
            return bundle;
        }
    }
    return std::nullopt;
}

std::optional<contracts::FieldBundleDTO> fieldBundleForSubject(
    const contracts::FieldBundleDTO& frame,
    const contracts::SubjectType subject)
{
    contracts::FieldBundleDTO bundle;
    for (const auto& item : frame.items) {
        if (item.subject == subject) {
            bundle.items.push_back(item);
        }
    }
    return bundle.items.empty() ? std::nullopt : std::make_optional(std::move(bundle));
}

std::optional<contracts::FieldBundleDTO> deriveLifeFieldForSubject(
    const contracts::DamageForecastFrame& damage,
    const contracts::LifeAssessmentFrame& life,
    const contracts::SubjectType subject)
{
    auto damageBundle = latestDamageFieldForSubject(damage, subject);
    if (!damageBundle.has_value()) {
        return std::nullopt;
    }

    const double scale = life.rul_s > 0.0 ? life.rul_s : 1.0;
    contracts::FieldBundleDTO lifeBundle;
    for (const auto& source : damageBundle->items) {
        contracts::SubjectFieldDTO item;
        item.subject = source.subject;
        item.taskpoint_id = source.taskpoint_id;
        item.values.reserve(source.values.size());
        for (const double value : source.values) {
            const double damage01 = std::clamp(std::isfinite(value) ? value : 0.0, 0.0, 1.0);
            item.values.push_back(std::max(0.0, 1.0 - damage01) * scale);
        }
        lifeBundle.items.push_back(std::move(item));
    }
    return lifeBundle.items.empty() ? std::nullopt : std::make_optional(std::move(lifeBundle));
}

std::optional<contracts::RuntimeSnapshotDTO> readRuntimeSnapshotDto(
    const std::filesystem::path& path,
    QString* error)
{
    const auto json = readJsonFileNlohmann(path, error);
    if (!json.has_value()) {
        return std::nullopt;
    }
    try {
        return json->get<contracts::RuntimeSnapshotDTO>();
    }
    catch (const std::exception& e) {
        if (error) {
            *error = QString::fromUtf8(e.what());
        }
        return std::nullopt;
    }
}

std::map<std::string, std::size_t> resultPortSizes(
    const std::vector<launchsupport::GraphResultPortView>& ports)
{
    std::map<std::string, std::size_t> sizes;
    for (const auto& port : ports) {
        sizes[port.port_name] = port.payload_json.size();
    }
    return sizes;
}

std::size_t outputBytesForPorts(
    const std::vector<std::string>& ports,
    const std::map<std::string, std::size_t>& sizes)
{
    std::size_t total = 0;
    for (const auto& port : ports) {
        const auto it = sizes.find(port);
        if (it != sizes.end()) {
            total += it->second;
        }
    }
    return total;
}

QString bytesText(const std::size_t bytes)
{
    if (bytes >= 1024u * 1024u) {
        return QString::fromUtf8("%1 MB").arg(static_cast<double>(bytes) / (1024.0 * 1024.0), 0, 'f', 2);
    }
    if (bytes >= 1024u) {
        return QString::fromUtf8("%1 KB").arg(static_cast<double>(bytes) / 1024.0, 0, 'f', 1);
    }
    return QString::fromUtf8("%1 B").arg(static_cast<qulonglong>(bytes));
}

QString throughputText(const std::size_t bytes, const std::int64_t durationMs)
{
    if (durationMs <= 0 || bytes == 0) {
        return QStringLiteral("-");
    }
    const double seconds = static_cast<double>(durationMs) / 1000.0;
    const double mbps = static_cast<double>(bytes) / (1024.0 * 1024.0) / seconds;
    return QString::fromUtf8("%1 MB/s").arg(mbps, 0, 'f', 3);
}

} // namespace flightenv::platform_ui::internal

void EnvPredictorUI::on_spinBox_valueChanged(int arg1)
{
    QScrollArea* scrollArea = ui.filedShowWidget->findChild<QScrollArea*>();
    if (!scrollArea) {
        qWarning() << "未找到 QScrollArea";
        return;
    }
    if (visibilityVtks.isEmpty())
        return;
    QWidget* chartContainer = scrollArea->widget();
    if (!chartContainer) {
        chartContainer = new QWidget();
        scrollArea->setWidget(chartContainer);
        scrollArea->setWidgetResizable(true);
    }
    QLayout* oldLayout = chartContainer->layout();
    if (oldLayout) {
        QLayoutItem* item;
        while ((item = oldLayout->takeAt(0)) != nullptr) {
            if (item->widget())
                item->widget()->setParent(nullptr);
            delete item;
        }
        delete oldLayout;
    }

    QGridLayout* newLayout = new QGridLayout(chartContainer);
    newLayout->setSpacing(10);
    newLayout->setContentsMargins(0, 0, 0, 0);
    for (size_t i = 0; i < arg1; i++)
        newLayout->setColumnStretch(i, 1);

    int visibleCount = 0;
    for (size_t i = 0; i < vtkField.size(); ++i) {
        VTKSingleDialog* vtkd = vtkField[i];
        if (!vtkd) {
            qWarning() << "vtkField[" << i << "] 是空指针，已跳过";
            continue;
        }

        if (visibilityVtks[i])
        {
            QWidget* container = new QWidget(chartContainer);
            QVBoxLayout* vLayout = new QVBoxLayout(container);
            vLayout->setSpacing(5);
            vLayout->setContentsMargins(0, 0, 0, 0);
            QLabel* titleLabel = new QLabel(QString::fromStdString(vtkd->windowTitle), container);
            titleLabel->setStyleSheet("font-weight: bold; color: #333;");
            vLayout->addWidget(titleLabel);
            vtkd->setParent(container);
            vLayout->addWidget(vtkd);
            int row = visibleCount / arg1;
            int col = visibleCount % arg1;

            for (size_t i = 0; i < arg1; i++)
                newLayout->setColumnStretch(i, 1);

            newLayout->addWidget(container, row, col);

            visibleCount++;
        }
    }
    chartContainer->setLayout(newLayout);
    chartContainer->updateGeometry();
    scrollArea->updateGeometry();
}

void EnvPredictorUI::on_spinBox_2_valueChanged(int arg1) {

    QScrollArea* scrollArea = ui.widget_4->findChild<QScrollArea*>();
    if (!scrollArea)
        return;

    QWidget* chartContainer = scrollArea->widget();
    if (!chartContainer)
        return;
    // 2. 删除旧布局
    QLayout* oldLayout = chartContainer->layout();
    if (oldLayout) {
        QLayoutItem* item;
        while ((item = oldLayout->takeAt(0)) != nullptr) {
            delete item;
        }
        delete oldLayout;
    }

    // 3. 新建布局
    QGridLayout* newLayout = new QGridLayout(chartContainer);
    newLayout->setSpacing(10);
    newLayout->setContentsMargins(0, 0, 0, 0);

    for (size_t i = 0; i < arg1; i++)
        newLayout->setColumnStretch(i, 1);

    // 4. 重新布局显示的 chart
    int visibleCount = 0;
    for (size_t i = 0; i < chartMarkers.size(); ++i)
    {
        if (visibilitys[i]) {
            QWidget* container = new QWidget(chartContainer);
            QVBoxLayout* vLayout = new QVBoxLayout(container);
            vLayout->setSpacing(5);
            vLayout->setContentsMargins(0, 0, 0, 0);
            chartMarkers[i]->setParent(container);
            vLayout->addWidget(chartMarkers[i]);


            int row = visibleCount / arg1;
            int col = visibleCount % arg1;


            newLayout->addWidget(container, row, col);
            visibleCount++;
        }
    }

    chartContainer->setLayout(newLayout);
    chartContainer->updateGeometry();
}

void EnvPredictorUI::on_startBtn_clicked() {//开始

#ifdef ONLINE
    try {
        if (controllerBackendCanRun()) {
            if (!runtime_initialized_) {
                applyPlatformRunConfigFromUi_();
                if (!platformRunConfigApplyOk_) {
                    platformUiLogError( "UI start aborted: platform run config was not applied.");
                    return;
                }
            }
            if (!runtime_initialized_) {
                const int init_rc = interactionData();
                if (init_rc != 0) {
                    platformUiLogError( "UI start aborted: runtime initialization failed.");
                    return;
                }
            }

            controller_backend_->resume_streaming();
            platformStreamingPaused_ = false;
            ui.startBtn->setText(QStringLiteral("开始/继续"));
            if (platformFrameStatusLabel_) {
                platformFrameStatusLabel_->setText(QStringLiteral("平台推流中"));
            }
        }
    } catch (const std::exception& e) {
        platformUiLogError( "UI start failed: %s", e.what());
    } catch (...) {
        platformUiLogError( "UI start failed: unknown exception");
    }
#endif

}

void EnvPredictorUI::on_pauseBtn_clicked() {//暂停

#ifdef ONLINE
    if (controller_backend_) {
        controller_backend_->pause_streaming();
        platformStreamingPaused_ = true;
        ui.startBtn->setText(QStringLiteral("继续"));
        if (platformFrameStatusLabel_) {
            platformFrameStatusLabel_->setText(QStringLiteral("已暂停推流，点击继续恢复"));
        }
    }
#endif
    return;
}

void EnvPredictorUI::on_resetBtn_clicked() {//复位
    try {
        if (controller_backend_) {
            controller_backend_->stop_online_run();
        }
    } catch (const std::exception& e) {
        platformUiLogError( "UI stop/reset failed: %s", e.what());
    } catch (...) {
        platformUiLogError( "UI stop/reset failed: unknown exception");
    }
    runtime_initialized_ = false;
    platformStreamingPaused_ = false;
    platform_snapshot_ = {};
    platformFrameLoops_.clear();
    platformCurrentBranchId_.clear();
    platformCurrentLoop_ = -1;
    platformLastRenderedFrameSignature_.clear();
    platformFieldControlSignature_.clear();
    platformLastRunTimelinePath_.clear();
    platformLastRunTimelineMtimeMs_ = -1;
    platformLastRunTimelineSize_ = -1;
    platformLastCoreParameterLoop_ = -1000000000;
    if (platformFrameSlider_) {
        const QSignalBlocker blocker(platformFrameSlider_);
        platformFrameSlider_->setRange(0, 0);
        platformFrameSlider_->setValue(0);
    }
    if (platformFrameProgressLabel_) {
        platformFrameProgressLabel_->setText(QStringLiteral("0/0"));
    }
    if (platformFrameStatusLabel_) {
        platformFrameStatusLabel_->setText(QStringLiteral("已中止，点击开始重新运行"));
    }
    ui.startBtn->setText(QStringLiteral("开始/继续"));
    platformUiLogInfo( "Platform UI stop/reset requested from timeline panel.");
    return;
    vtkXYZDlg = new VTKSingleDialog("", "", ui.flightAttitudeWidget);
    vtkXYZDlg->setRotationAnimationTimer();
    std::string stlFilePath = MODELFLIGHTSTLDIR;
    if (!vtkXYZDlg->loadSTLModel(stlFilePath)) {
        qWarning() << "STL模型加载失败：" << QString::fromStdString(stlFilePath);
    }
    vtkXYZDlg->initAngleDisplay();
    vtkXYZDlg->setInitialViewAngle(0, -45, 0);

    currentX = currentY = currentZ = 0;

    vtkXYZDlg->RemoveActor();
    vtkXYZDlg->show();

    vtkXYZSizeTimer = new QTimer(this);
    connect(vtkXYZSizeTimer, &QTimer::timeout, this, &EnvPredictorUI::setAngle);
    vtkXYZSizeTimer->setInterval(100);
    vtkXYZSizeTimer->start();

}

void EnvPredictorUI::setAngle()
{
    //const double degreeToRad = vtkMath::Pi() / 180.0;
    //currentY += degreeToRad;
    vtkXYZDlg->setModelRotation(currentX, currentY, currentZ);
}


void EnvPredictorUI::on_trainBtn_clicked() {//反演模型训练
    try {
        if (!runtime_initialized_) {
            applyPlatformRunConfigFromUi_();
            if (!platformRunConfigApplyOk_) {
                platformUiLogError( "UI train/init aborted: platform run config was not applied.");
                return;
            }
        }
        const int init_rc = interactionData();
        if (init_rc != 0) {
            platformUiLogError( "UI train/init aborted: runtime initialization failed.");
        }
    } catch (const std::exception& e) {
        platformUiLogError( "UI train/init failed: %s", e.what());
    } catch (...) {
        platformUiLogError( "UI train/init failed: unknown exception");
    }
}

void EnvPredictorUI::change3DValue(const launchsupport::PredictionViewModel& DTO) {
    (void)DTO;
}

void EnvPredictorUI::change3DRanges(QVector<double> rMin, QVector<double> rMax) {

    //isSetRange = TRUE;
    //vets.clear();
    //for (size_t i = 0; i < rMin.size(); i++)
    //{
    //    QString rangeStr = QString("[%1 %2]").arg(rMin[i], 0, 'f', 2).arg(rMax[i], 0, 'f', 2);
    //    vets.append(rangeStr);
    //}if (!axisValues.isEmpty()) {
    //    cloudChart->applyColorMapping(axisValues, vets);
    //    vetc = createGradientColors(12);
    //    displayColorSequenceVertically(ui.filedShowWidget, vetc, vets, QSize(30, 30), 0, true);
    //}
}

void EnvPredictorUI::on_comboBox_4_currentIndexChanged(int index) {
    chartMarkers.clear(); // 清空旧的指针列表
    if (!vtkMarker || index < 0 || static_cast<std::size_t>(index) >= markerPoints.size())
        return;
    sensorflg = index;
    rebuildSensorCurveCharts_(index, true);
    platformSensorDisplayIndex_ = index;
    return;

    if (!chartScrollArea) {
        chartScrollArea = new QScrollArea(ui.widget_4);
        chartScrollArea->setWidgetResizable(true);
        chartScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        chartScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        chartScrollArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        chartScrollArea->setStyleSheet("QScrollArea { border: none; }");
        chartScrollArea->setContentsMargins(0, 0, 0, 0);

        chartContainerWidget = new QWidget(chartScrollArea);
        chartScrollArea->setWidget(chartContainerWidget);

        QLayout* hostLayout = ui.widget_4->layout();
        if (!hostLayout) {
            auto* createdLayout = new QVBoxLayout(ui.widget_4);
            createdLayout->setSpacing(0);
            createdLayout->setContentsMargins(0, 0, 0, 0);
            hostLayout = createdLayout;
        }
        hostLayout->addWidget(chartScrollArea);
    }

    if (!chartContainerWidget->layout()) {
        auto* contentLayout = new QVBoxLayout(chartContainerWidget);
        contentLayout->setSpacing(0);
        contentLayout->setContentsMargins(0, 0, 0, 0);
    }

    QLayout* contentLayout = chartContainerWidget->layout();

    if (!platformSensorTable_) {
        platformSensorTable_ = new QTableWidget(chartContainerWidget);
        platformSensorTable_->setObjectName(QStringLiteral("platformSensorTable"));
        contentLayout->addWidget(platformSensorTable_);
    }

    auto* table = platformSensorTable_;
    const int rowCount = static_cast<int>(markerPoints[index].size());
    const bool sensorShapeChanged =
        platformSensorDisplayIndex_ != index ||
        table->rowCount() != rowCount ||
        table->columnCount() != 5;
    if (sensorShapeChanged) {
        table->setColumnCount(5);
        table->setRowCount(rowCount);
    }
    table->setHorizontalHeaderLabels({
        QString::fromUtf8("点号"),
        QStringLiteral("X"),
        QStringLiteral("Y"),
        QStringLiteral("Z"),
        QString::fromUtf8("最新值")
    });
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);

    const std::vector<std::vector<double>>* latestRows = nullptr;
    if (static_cast<std::size_t>(index) < sensorsss.size() && !sensorsss[static_cast<std::size_t>(index)].empty()) {
        latestRows = &sensorsss[static_cast<std::size_t>(index)].back();
    }

    auto setItem = [table](int row, int col, const QString& text) {
        auto* item = table->item(row, col);
        if (!item) {
            item = new QTableWidgetItem();
            table->setItem(row, col, item);
        }
        item->setText(text);
        item->setTextAlignment(Qt::AlignCenter);
    };

    for (int row = 0; row < rowCount; ++row) {
        const auto& p = markerPoints[index][static_cast<std::size_t>(row)];
        setItem(row, 0, QString::number(row + 1));
        setItem(row, 1, QString::number(p[0], 'f', 3));
        setItem(row, 2, QString::number(p[1], 'f', 3));
        setItem(row, 3, QString::number(p[2], 'f', 3));

        QString valueText = QStringLiteral("-");
        if (latestRows && static_cast<std::size_t>(row) < latestRows->size()) {
            QStringList parts;
            for (const double value : latestRows->at(static_cast<std::size_t>(row))) {
                parts << QString::number(value, 'g', 8);
            }
            if (!parts.isEmpty()) {
                valueText = parts.join(QStringLiteral(", "));
            }
        }
        setItem(row, 4, valueText);
    }
    if (sensorShapeChanged) {
        table->resizeColumnsToContents();
    }

    visibilitys.clear();
    for (int i = 0; i < rowCount; ++i) {
        visibilitys.push_back(true);
    }
    if (sensorShapeChanged) {
        vtkMarker->setMarkerPoints(markerPoints[index]);
        platformSensorDisplayIndex_ = index;
    }
    return;
    disconnect(vtkMarker, &VTKSingleDialog::allMarkersVisibilityUpdated, this, &EnvPredictorUI::redrawAllMarkers);
    sensorflg = index;

    if (!chartScrollArea) {
        chartScrollArea = new QScrollArea(ui.widget_4);
        chartScrollArea->setWidgetResizable(true);
        chartScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        chartScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        chartScrollArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        chartScrollArea->setStyleSheet("QScrollArea { border: none; }");
        chartScrollArea->setContentsMargins(0, 0, 0, 0);

        chartContainerWidget = new QWidget();
        //chartContainerWidget->setStyleSheet("background-color: white;");
        chartScrollArea->setWidget(chartContainerWidget);

        // 设置 ui.widget_4 的布局，将 scrollArea 添加进去
        QVBoxLayout* widget4Layout = new QVBoxLayout(ui.widget_4);
        widget4Layout->setSpacing(0);
        widget4Layout->setContentsMargins(0, 0, 0, 0);
        widget4Layout->addWidget(chartScrollArea);
    }

    // 清空并删除 chartContainerWidget 中旧的布局和控件
    QLayout* oldLayout = chartContainerWidget->layout();
    if (oldLayout) {
        QLayoutItem* item;
        while ((item = oldLayout->takeAt(0)) != nullptr) {
            if (item->widget()) {
                item->widget()->deleteLater(); // 删除控件
            }
            delete item; // 删除布局项
        }
        delete oldLayout; // 删除旧布局
    }

    // 创建新的网格布局
    QGridLayout* newLayout = new QGridLayout(chartContainerWidget);
    newLayout->setSpacing(0); // 间距设为0，最紧凑
    newLayout->setContentsMargins(0, 0, 0, 0);
    for (size_t i = 0; i < ui.numHorWindowSpin->value(); i++)
        newLayout->setColumnStretch(i, 1);

    int chartCount = markerPoints[index].size();
    visibilitys.clear();
    for (size_t i = 0; i < chartCount; ++i) {
        ChartSingleDialog* chartDlg = new ChartSingleDialog(
            QString::number(i + 1) + "\nX:" + QString::number(markerPoints[index][i][0], 'f', 2)
            + " Y:" + QString::number(markerPoints[index][i][1], 'f', 2) + " Z:" + QString::number(markerPoints[index][i][2], 'f', 2),
            chartContainerWidget // 指定父控件
        );
        chartDlg->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        //chartDlg->setFixedHeight(80);
        chartDlg->setContentsMargins(0, 0, 0, 0);
        chartDlg->setStyleSheet("border: none;"); // 移除边框

        int row = static_cast<int>(i) / ui.numHorWindowSpin->value();
        int col = static_cast<int>(i) % ui.numHorWindowSpin->value();
        if (chartCount == 1) {
            newLayout->addWidget(chartDlg, 0, 0, 1, 2);
        }
        else {
            newLayout->addWidget(chartDlg, row, col);
        }
        visibilitys.push_back(1);
        chartMarkers.push_back(chartDlg);
    }

    // 将新布局设置到容器上
    chartContainerWidget->setLayout(newLayout);

    // --- 核心修改：强制更新几何布局 ---
    chartContainerWidget->updateGeometry();
    chartScrollArea->updateGeometry();
    ui.widget_4->updateGeometry(); // 可选，但有时能解决顶层布局问题

    vtkMarker->setMarkerPoints(markerPoints[index]);
    if (static_cast<std::size_t>(sensorflg) < sensorsss.size()) {
        for (size_t i = 0; i < sensorsss[sensorflg].size(); i++) {
            try {
                updateCharts(visibilitys, transpose(sensorsss[sensorflg][i]));
            } catch (const std::exception& e) {
                platformUiLogWarning( "sensor replay chart update skipped: %s", e.what());
            }
        }
    }

    connect(vtkMarker, &VTKSingleDialog::allMarkersVisibilityUpdated, this, &EnvPredictorUI::redrawAllMarkers);
}
