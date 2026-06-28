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
#include <cstdint>
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

namespace {

rclcpp::Logger envUiLogger()
{
    return rclcpp::get_logger("field_array_toggle_client");
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
#ifdef FLIGHTENV_USE_PLATFORM_BACKEND
    return true;
#else
    return rclcpp::ok();
#endif
}

#ifdef FLIGHTENV_USE_PLATFORM_BACKEND
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
#endif

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
        RCLCPP_INFO(envUiLogger(), "%s output:\n%s", label.toStdString().c_str(), output.toStdString().c_str());
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
        RCLCPP_INFO(envUiLogger(), "Platform UI replay mode: %s", replayRunDir.string().c_str());
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
    RCLCPP_INFO(
        envUiLogger(),
        "Platform UI live mode: RuntimeHost=%s chain_dir=%s",
        runtimeHostExe.string().c_str(),
        chainDir.string().c_str());
    return options;
}

std::shared_ptr<launchsupport::IControllerBackend> createControllerBackend(
    const std::shared_ptr<launchsupport::ControllerViewAdapter>& legacyController)
{
#ifdef FLIGHTENV_USE_PLATFORM_BACKEND
    (void)legacyController;
    return std::make_shared<launchsupport::PlatformControllerBackend>(platformBackendOptions());
#else
    if (!legacyController) {
        throw std::runtime_error("Legacy controller backend requires a ControllerViewAdapter");
    }
    return std::make_shared<launchsupport::LegacyRosControllerBackend>(legacyController);
#endif
}

std::shared_ptr<launchsupport::ControllerViewAdapter> createLegacyController(
    const std::shared_ptr<StreamController>& node,
    const std::shared_ptr<launchsupport::LaunchSession<StreamController>>& session)
{
#ifdef FLIGHTENV_USE_PLATFORM_BACKEND
    (void)node;
    (void)session;
    return {};
#else
    return std::make_shared<launchsupport::ControllerViewAdapter>(node, session);
#endif
}

} // namespace


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

EnvPredictorUI::EnvPredictorUI(
    QWidget* parent,
    std::shared_ptr<StreamController> node,
    std::shared_ptr<launchsupport::LaunchSession<StreamController>> session)
    : QMainWindow(parent),
    currentAxis(2),
    frameCount(0),
    currentFps(0.0),
    Node(node),
    session_(std::move(session)),
    legacy_controller_(createLegacyController(Node, session_)),
    controller_backend_(createControllerBackend(legacy_controller_)),
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
    ui.tabWidget_main->setCurrentIndex(1);
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
#ifdef FLIGHTENV_USE_PLATFORM_BACKEND
            RCLCPP_WARN(envUiLogger(), "Platform controller backend initialized without legacy RuntimeViewModel.");
            initPlatformFieldWidget();
            initPlatformFieldControlPanel();
            return true;
#else
            RCLCPP_ERROR(envUiLogger(), "Controller runtime initialization failed: runtime view is empty");
            return false;
#endif
        }
        RCLCPP_INFO(
            envUiLogger(),
            "Controller runtime view ready: fields=%zu meshes=%zu sensors=%zu",
            runtime_view_->fields.size(),
            runtime_view_->meshes.size(),
            runtime_view_->sensors.size());
        initModel();
        return true;
    }
    catch (const std::exception& e) 
    {
        RCLCPP_ERROR(envUiLogger(), "Controller runtime initialization failed: %s", e.what());
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
#ifndef FLIGHTENV_USE_PLATFORM_BACKEND
    callbacks.onPrediction = [this](const launchsupport::PredictionViewModel& d) {
        handlePrediction(d);
    };
#endif
    callbacks.onSensor = [this](const launchsupport::SensorViewModel& d) {
#ifdef FLIGHTENV_USE_PLATFORM_BACKEND
        QPointer<EnvPredictorUI> self(this);
        QMetaObject::invokeMethod(this, [self, d]() {
            if (self) {
                self->handleSensorView(d);
            }
        }, Qt::QueuedConnection);
        return;
#endif
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
#ifdef FLIGHTENV_USE_PLATFORM_BACKEND
    bindPlatformSnapshotCallback();
#endif
}

/**
 * @brief 注册预测结果回调
 * 当后台收到预测结果时，Controller 会调用 handlePrediction
 */
void EnvPredictorUI::bindPredictionCallback()
{
    legacy_controller_->set_on_prediction([this](const launchsupport::PredictionViewModel& d) {
        handlePrediction(d);
        });
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
    //注册传感器回调
    legacy_controller_->set_on_sensor([&](const launchsupport::SensorViewModel& d) {
        sensorPkts.push_back(d);
        std::vector <double> sens;
        std::vector < std::vector <double>> senss;
        if (sensorsss.empty()) return;
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
        });
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

#ifdef FLIGHTENV_USE_PLATFORM_BACKEND
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
            RCLCPP_WARN(envUiLogger(), "platform sensor chart update skipped: %s", e.what());
        }
    });

    LOG(INFO) << "platform sensor frame cached for controller display";
    return;
#endif

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
            RCLCPP_WARN(envUiLogger(), "sensor chart update skipped: %s", e.what());
        }
    });

    LOG(INFO) << "platform/ROS sensor frame applied to controller charts";
}

void EnvPredictorUI::bindStateCallback()
{
    legacy_controller_->set_on_state([](const contracts::StateFrame& d) {
        LOG(INFO) << "监听到飞行状态发出信号";
        });
}
void EnvPredictorUI::bindRuntimeCallback()
{
    legacy_controller_->set_on_runtime([&](const launchsupport::RuntimeViewModel& s) {
        LOG(INFO) << "监听到初始化完成，发出快照信息";
        runtime_view_ = std::make_shared<launchsupport::RuntimeViewModel>(s);
        });
}
#endif

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
    RCLCPP_INFO(envUiLogger(), "UI initModel step: sensor markers begin");
    initSensorMarkerWidget();//标记点窗口
    RCLCPP_INFO(envUiLogger(), "UI initModel step: sensor markers done");
    refreshRuntimeChainPage_();
    RCLCPP_INFO(envUiLogger(), "UI initModel step: field widget begin");
    initFieldWidget();//物理场
    RCLCPP_INFO(envUiLogger(), "UI initModel step: field widget done");
    RCLCPP_INFO(envUiLogger(), "UI initModel step: field control begin");
    initFIeldControlPanel();//fieldcontrol
    RCLCPP_INFO(envUiLogger(), "UI initModel step: field control done");
    RCLCPP_INFO(envUiLogger(), "UI initModel step: parameter table begin");
    initParameterTable();//ROS信息参数表格
    RCLCPP_INFO(envUiLogger(), "UI initModel step: parameter table done");
    RCLCPP_INFO(envUiLogger(), "UI initModel step: scatter chart begin");
    initScatterChart();//初始化图标
    RCLCPP_INFO(envUiLogger(), "UI initModel step: scatter chart done");
    RCLCPP_INFO(envUiLogger(), "UI initModel step: flight attitude begin");
    initFlightAttitudeWidget();//初始化飞行状态窗口
    RCLCPP_INFO(envUiLogger(), "UI initModel step: flight attitude done");
    
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
                RCLCPP_WARN(envUiLogger(), "sensor replay chart update skipped: %s", e.what());
            }
        }
    }
}

void EnvPredictorUI::refreshPlatformCoreParameterChart_(const QJsonObject& timelineRoot)
{
#ifndef FLIGHTENV_USE_PLATFORM_BACKEND
    (void)timelineRoot;
    return;
#else
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
#endif
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
#ifdef FLIGHTENV_USE_PLATFORM_BACKEND
    if (platformRenderTimer_) {
        platformRenderTimer_->stop();
    }
#endif
    stopGraphRuntimeRunner_();
    if (controller_backend_) {
#ifdef ONLINE
        if (runtime_initialized_ && controllerBackendCanRun()) {
            try {
                if (!controller_backend_->stop_online_run()) {
                    RCLCPP_WARN(envUiLogger(), "UI shutdown: online run stop/finalize reported failure.");
                }
            }
            catch (const std::exception& e) {
                RCLCPP_ERROR(envUiLogger(), "UI shutdown: online run stop/finalize failed: %s", e.what());
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
#ifdef FLIGHTENV_USE_PLATFORM_BACKEND
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
#endif
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
#ifdef FLIGHTENV_USE_PLATFORM_BACKEND
    if (!platformSensorFrames_.empty()) {
        updatePlatformSensorDisplayForLoop(std::max(0, platformSensorFrames_.front().loop));
    }
#endif
}
/**
* @brief 初始化物理场
*/
void EnvPredictorUI::initFieldWidget()
{
#ifdef FLIGHTENV_USE_PLATFORM_BACKEND
    initPlatformFieldWidget();
    return;
#endif
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
#ifdef FLIGHTENV_USE_PLATFORM_BACKEND
    initPlatformFieldControlPanel();
    return;
#endif
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

#ifdef FLIGHTENV_USE_PLATFORM_BACKEND
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

void EnvPredictorUI::bindPlatformSnapshotCallback()
{
    auto platformBackend = std::dynamic_pointer_cast<launchsupport::PlatformControllerBackend>(controller_backend_);
    if (!platformBackend) {
        return;
    }

    platformBackend->set_on_platform_snapshot([this](const launchsupport::PlatformRunSnapshotView& snapshot) {
        QPointer<EnvPredictorUI> self(this);
        QMetaObject::invokeMethod(this, [self, snapshot]() {
            if (self) {
                self->handlePlatformSnapshot(snapshot);
            }
        }, Qt::QueuedConnection);
    });

    const auto latest = platformBackend->latest_platform_snapshot();
    if (!latest.run_dir.empty() || !latest.timeline_points.empty() || !latest.field_artifacts.empty()) {
        handlePlatformSnapshot(latest);
    }
}

QString EnvPredictorUI::platformBestBranchId() const
{
    std::map<QString, int> counts;
    std::map<QString, int> latestLoop;
    for (const auto& field : platform_snapshot_.field_artifacts) {
        if (!isRenderablePlatformField(field)) {
            continue;
        }
        const QString branch = QString::fromStdString(field.branch_id);
        counts[branch] += 1;
        latestLoop[branch] = std::max(latestLoop[branch], field.loop_iteration_index);
    }
    if (counts.empty()) {
        return QString::fromStdString(platform_snapshot_.primary_branch_id);
    }
    QString best;
    int bestScore = -1;
    int bestLoop = -1;
    for (const auto& item : counts) {
        const QString branch = item.first;
        int score = item.second;
        if (branch.contains(QStringLiteral("realtime_prediction"))) {
            score += 100000;
        }
        if (score > bestScore || (score == bestScore && latestLoop[branch] > bestLoop)) {
            best = branch;
            bestScore = score;
            bestLoop = latestLoop[branch];
        }
    }
    return best;
}

std::vector<int> EnvPredictorUI::platformLoopsForBranch(const QString& branchId) const
{
    std::set<int> loops;
    for (const auto& field : platform_snapshot_.field_artifacts) {
        if (!isRenderablePlatformField(field)) {
            continue;
        }
        if (QString::fromStdString(field.branch_id) != branchId) {
            continue;
        }
        loops.insert(field.loop_iteration_index);
    }
    return std::vector<int>(loops.begin(), loops.end());
}

int EnvPredictorUI::platformLatestLoopForBranch(const QString& branchId) const
{
    const auto loops = platformLoopsForBranch(branchId);
    return loops.empty() ? -1 : loops.back();
}

std::vector<launchsupport::PlatformFieldArtifactView> EnvPredictorUI::platformFieldsForCurrentFrame() const
{
    std::vector<launchsupport::PlatformFieldArtifactView> fields;
    for (const auto& field : platform_snapshot_.field_artifacts) {
        if (!isRenderablePlatformField(field)) {
            continue;
        }
        if (QString::fromStdString(field.branch_id) != platformCurrentBranchId_) {
            continue;
        }
        if (field.loop_iteration_index != platformCurrentLoop_) {
            continue;
        }
        fields.push_back(field);
    }
    std::sort(fields.begin(), fields.end(), [](const auto& left, const auto& right) {
        const auto l = std::tie(left.component_id, left.field_name, left.port_id, left.node_id);
        const auto r = std::tie(right.component_id, right.field_name, right.port_id, right.node_id);
        return l < r;
    });
    return fields;
}

void EnvPredictorUI::handlePlatformSnapshot(const launchsupport::PlatformRunSnapshotView& snapshot)
{
    platform_snapshot_ = snapshot;
    refreshPlatformFieldNavigation();
    updatePlatformCoreParameterTable();
    if (platformFollowLatestCheck_ == nullptr || platformFollowLatestCheck_->isChecked()) {
        selectPlatformLatestFrame();
    } else {
        updatePlatformFrameProgressLabel();
    }
}

void EnvPredictorUI::refreshPlatformFieldNavigation()
{
    if (!platformBranchCombo_ || !platformFrameSlider_) {
        return;
    }

    platformUpdatingUi_ = true;
    const QSignalBlocker branchBlocker(platformBranchCombo_);
    const QSignalBlocker sliderBlocker(platformFrameSlider_);
    const QString previousBranch = platformCurrentBranchId_;
    platformBranchCombo_->clear();

    std::set<QString> branchesWithFields;
    for (const auto& field : platform_snapshot_.field_artifacts) {
        if (isRenderablePlatformField(field)) {
            branchesWithFields.insert(QString::fromStdString(field.branch_id));
        }
    }
    for (const QString& branchId : branchesWithFields) {
        platformBranchCombo_->addItem(branchId, branchId);
    }

    QString branch = previousBranch;
    if (branch.isEmpty() || branchesWithFields.find(branch) == branchesWithFields.end()) {
        branch = platformBestBranchId();
    }
    const int branchIndex = platformBranchCombo_->findData(branch);
    if (branchIndex >= 0) {
        platformBranchCombo_->setCurrentIndex(branchIndex);
        platformCurrentBranchId_ = branch;
    } else {
        platformCurrentBranchId_.clear();
    }

    platformFrameLoops_ = platformLoopsForBranch(platformCurrentBranchId_);
    if (platformFrameLoops_.empty()) {
        platformFrameSlider_->setRange(0, 0);
        platformCurrentLoop_ = -1;
    } else {
        const int sliderMax = static_cast<int>(platformFrameLoops_.size()) - 1;
        platformFrameSlider_->setRange(0, sliderMax);
        auto it = std::find(platformFrameLoops_.begin(), platformFrameLoops_.end(), platformCurrentLoop_);
        if (it == platformFrameLoops_.end()) {
            platformCurrentLoop_ = platformFrameLoops_.back();
            it = std::prev(platformFrameLoops_.end());
        }
        const int sliderValue = static_cast<int>(std::distance(platformFrameLoops_.begin(), it));
        platformFrameSlider_->setValue(sliderValue);
    }

    platformUpdatingUi_ = false;
    updatePlatformFrameProgressLabel();
}

void EnvPredictorUI::selectPlatformLatestFrame()
{
    if (platformCurrentBranchId_.isEmpty()) {
        platformCurrentBranchId_ = platformBestBranchId();
    }
    const int latestLoop = platformLatestLoopForBranch(platformCurrentBranchId_);
    if (latestLoop >= 0) {
        platformCurrentLoop_ = latestLoop;
    }
    if (platformFrameSlider_ && !platformFrameLoops_.empty()) {
        const auto it = std::find(platformFrameLoops_.begin(), platformFrameLoops_.end(), platformCurrentLoop_);
        if (it != platformFrameLoops_.end()) {
            const QSignalBlocker blocker(platformFrameSlider_);
            const int sliderValue = static_cast<int>(std::distance(platformFrameLoops_.begin(), it));
            platformFrameSlider_->setValue(sliderValue);
        }
    }
    updatePlatformFrameProgressLabel();
    schedulePlatformCurrentFrameRender();
}

void EnvPredictorUI::updatePlatformFrameProgressLabel()
{
    if (!platformFrameProgressLabel_) {
        return;
    }
    if (platformFrameLoops_.empty() || platformCurrentLoop_ < 0) {
        platformFrameProgressLabel_->setText(QStringLiteral("0/0"));
        if (platformFrameSlider_) {
            const QSignalBlocker blocker(platformFrameSlider_);
            platformFrameSlider_->setRange(0, 0);
            platformFrameSlider_->setValue(0);
        }
        return;
    }

    auto it = std::find(platformFrameLoops_.begin(), platformFrameLoops_.end(), platformCurrentLoop_);
    if (it == platformFrameLoops_.end()) {
        platformFrameProgressLabel_->setText(
            QStringLiteral("-/%1  loop=%2")
                .arg(static_cast<int>(platformFrameLoops_.size()))
                .arg(platformCurrentLoop_));
        return;
    }

    const int index = static_cast<int>(std::distance(platformFrameLoops_.begin(), it)) + 1;
    const int zeroBasedIndex = index - 1;
    if (platformFrameSlider_) {
        const QSignalBlocker blocker(platformFrameSlider_);
        platformFrameSlider_->setRange(0, static_cast<int>(platformFrameLoops_.size()) - 1);
        platformFrameSlider_->setValue(zeroBasedIndex);
    }
    const QDateTime missionEpoch(QDate(2026, 1, 1), QTime(0, 0, 0));
    ui.missionTimeEdit->setDateTime(missionEpoch.addSecs(std::max(0, platformCurrentLoop_)));
    platformFrameProgressLabel_->setText(
        QStringLiteral("%1/%2  loop=%3")
            .arg(index)
            .arg(static_cast<int>(platformFrameLoops_.size()))
            .arg(platformCurrentLoop_));
}

void EnvPredictorUI::schedulePlatformCurrentFrameRender()
{
    if (!platformRenderTimer_) {
        platformRenderTimer_ = new QTimer(this);
        platformRenderTimer_->setSingleShot(true);
        platformRenderTimer_->setInterval(120);
        connect(platformRenderTimer_, &QTimer::timeout, this, [this]() {
            renderPlatformCurrentFrame();
        });
    }
    platformRenderTimer_->start();
}

void EnvPredictorUI::requestPlatformFieldArtifactValuesAsync(
    const std::filesystem::path& artifactPath,
    const QString& cachePath,
    const qint64 mtimeMs,
    const qint64 sizeBytes)
{
    const QString requestKey = QStringLiteral("%1|%2|%3")
        .arg(cachePath)
        .arg(mtimeMs)
        .arg(sizeBytes);
    if (platformFieldValuePending_.find(requestKey) != platformFieldValuePending_.end()) {
        return;
    }
    platformFieldValuePending_.insert(requestKey);

    QPointer<EnvPredictorUI> self(this);
    QThreadPool::globalInstance()->start(QRunnable::create(
        [self, artifactPath, cachePath, requestKey, mtimeMs, sizeBytes]() {
            QString readError;
            std::vector<double> values = readPlatformFieldArtifactValues(artifactPath, &readError);
            if (!self) {
                return;
            }
            QMetaObject::invokeMethod(self.data(), [self, cachePath, requestKey, mtimeMs, sizeBytes,
                                                    values = std::move(values), readError]() mutable {
                if (!self) {
                    return;
                }
                PlatformFieldValueCacheEntry entry;
                entry.mtime_ms = mtimeMs;
                entry.size_bytes = sizeBytes;
                entry.values = std::move(values);
                entry.error = readError;
                self->platformFieldValueCache_[cachePath] = std::move(entry);
                self->platformFieldValuePending_.erase(requestKey);
                self->platformLastRenderedFrameSignature_.clear();
                self->schedulePlatformCurrentFrameRender();
            }, Qt::QueuedConnection);
        }));
}

std::vector<double> EnvPredictorUI::cachedPlatformFieldArtifactValues(
    const launchsupport::PlatformFieldArtifactView& field,
    QString* error,
    bool* loading)
{
    if (loading) {
        *loading = false;
    }
    const QString path = platformPathText(field.artifact_path);
    const QFileInfo info(path);
    const qint64 mtimeMs = info.exists() ? info.lastModified().toMSecsSinceEpoch() : -1;
    const qint64 sizeBytes = info.exists() ? info.size() : -1;

    auto found = platformFieldValueCache_.find(path);
    if (found != platformFieldValueCache_.end() &&
        found->second.mtime_ms == mtimeMs &&
        found->second.size_bytes == sizeBytes) {
        if (error) {
            *error = found->second.error;
        }
        return found->second.values;
    }

    requestPlatformFieldArtifactValuesAsync(field.artifact_path, path, mtimeMs, sizeBytes);
    if (loading) {
        *loading = true;
    }
    if (found != platformFieldValueCache_.end() && found->second.error.isEmpty()) {
        if (error) {
            error->clear();
        }
        return found->second.values;
    }
    if (error) {
        error->clear();
    }
    return {};
}

void EnvPredictorUI::updatePlatformCoreParameterTable()
{
    if (platform_snapshot_.run_dir.empty() || !ui.paramCompareTableWidget) {
        return;
    }

    const QString platformManifestPath = platformPathText(platform_snapshot_.run_dir / "series_manifest.json");
    const QString platformTimelinePath = platform_snapshot_.run_timeline_index_path.empty()
        ? platformPathText(platform_snapshot_.run_dir / "run_timeline_index.json")
        : platformPathText(platform_snapshot_.run_timeline_index_path);
    const QFileInfo platformManifestInfo(platformManifestPath);
    const QFileInfo platformTimelineInfo(platformTimelinePath);
    const qint64 platformManifestMtimeMs = platformManifestInfo.exists()
        ? platformManifestInfo.lastModified().toMSecsSinceEpoch()
        : -1;
    const qint64 platformManifestSize = platformManifestInfo.exists()
        ? platformManifestInfo.size()
        : -1;
    const qint64 platformTimelineMtimeMs = platformTimelineInfo.exists()
        ? platformTimelineInfo.lastModified().toMSecsSinceEpoch()
        : -1;
    const qint64 platformTimelineSize = platformTimelineInfo.exists()
        ? platformTimelineInfo.size()
        : -1;
    if (platformManifestPath == platformLastSeriesManifestPath_ &&
        platformManifestMtimeMs == platformLastSeriesManifestMtimeMs_ &&
        platformManifestSize == platformLastSeriesManifestSize_ &&
        platformTimelinePath == platformLastRunTimelinePath_ &&
        platformTimelineMtimeMs == platformLastRunTimelineMtimeMs_ &&
        platformTimelineSize == platformLastRunTimelineSize_ &&
        platformCurrentLoop_ == platformLastCoreParameterLoop_ &&
        platform_snapshot_.qois.empty()) {
        return;
    }
    platformLastSeriesManifestPath_ = platformManifestPath;
    platformLastSeriesManifestMtimeMs_ = platformManifestMtimeMs;
    platformLastSeriesManifestSize_ = platformManifestSize;
    platformLastRunTimelinePath_ = platformTimelinePath;
    platformLastRunTimelineMtimeMs_ = platformTimelineMtimeMs;
    platformLastRunTimelineSize_ = platformTimelineSize;
    platformLastCoreParameterLoop_ = platformCurrentLoop_;

    const int targetLoop = platformCurrentLoop_;
    const auto jsonLoop = [](const QJsonObject& object) {
        return object.value(QStringLiteral("loop_iteration_index")).toInt(
            object.value(QStringLiteral("loop")).toInt(
                object.value(QStringLiteral("step_index")).toInt(-1)));
    };
    const auto jsonFrameIndex = [](const QJsonObject& object) {
        return object.value(QStringLiteral("frame_index")).toInt(-1);
    };
    const auto jsonTimeSeconds = [](const QJsonObject& object) {
        return object.value(QStringLiteral("sample_time_s")).toDouble(
            object.value(QStringLiteral("runtime_time_s")).toDouble(
                object.value(QStringLiteral("time_s")).toDouble(0.0)));
    };
    const auto isBetterTimelineCandidate = [targetLoop](
        const int loop,
        const int frameIndex,
        const double time,
        const int bestLoop,
        const int bestFrame,
        const double bestTime) {
        if (targetLoop >= 0) {
            const int distance = loop >= 0 ? std::abs(loop - targetLoop) : std::numeric_limits<int>::max();
            const int bestDistance = bestLoop >= 0 ? std::abs(bestLoop - targetLoop) : std::numeric_limits<int>::max();
            if (distance != bestDistance) {
                return distance < bestDistance;
            }
        }
        return loop > bestLoop ||
            (loop == bestLoop && frameIndex > bestFrame) ||
            (loop == bestLoop && frameIndex == bestFrame && time >= bestTime);
    };

    std::vector<std::vector<QString>> platformRows;
    std::map<QString, bool> displayedMetricLabels;
    platformRows.push_back({
        QStringLiteral("当前值"),
        QStringLiteral("指标ID"),
        QStringLiteral("分组"),
        QStringLiteral("单位"),
        QStringLiteral("来源"),
        QStringLiteral("说明")
    });

    if (platformManifestInfo.exists()) {
        QFile file(platformManifestPath);
        if (file.open(QIODevice::ReadOnly)) {
            QJsonParseError parseError{};
            const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
            if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
                const QJsonArray seriesArray = doc.object().value(QStringLiteral("series")).toArray();
                platformRows.reserve(static_cast<std::size_t>(seriesArray.size()) + 1);
                for (const QJsonValue& seriesValue : seriesArray) {
                    if (!seriesValue.isObject()) {
                        continue;
                    }
                    const QJsonObject series = seriesValue.toObject();
                    const QString label = series.value(QStringLiteral("label")).toString(
                        series.value(QStringLiteral("series_id")).toString(QStringLiteral("未命名平台指标")));
                    const QString metricLabel = canonicalMetricLabel(label);
                    if (!shouldDisplaySeriesMetric(metricLabel)) {
                        continue;
                    }
                    const QString seriesId = series.value(QStringLiteral("series_id")).toString();
                    const QString branch = series.value(QStringLiteral("branch_id")).toString();
                    const QString unit = displayMetricUnit(
                        metricLabel,
                        series.value(QStringLiteral("unit")).toString());
                    const QJsonArray points = series.value(QStringLiteral("points")).toArray();
                    if (points.isEmpty()) {
                        continue;
                    }

                    QJsonValue latestValue;
                    QString latestTime;
                    int latestLoop = std::numeric_limits<int>::min();
                    int latestFrame = std::numeric_limits<int>::min();
                    double latestTimeSeconds = -std::numeric_limits<double>::infinity();
                    for (const QJsonValue& pointValue : points) {
                        if (!pointValue.isObject()) {
                            continue;
                        }
                        const QJsonObject point = pointValue.toObject();
                        const QJsonValue value = point.value(QStringLiteral("value"));
                        if (value.isUndefined()) {
                            continue;
                        }
                        const int loop = jsonLoop(point);
                        const int frameIndex = jsonFrameIndex(point);
                        const double time = jsonTimeSeconds(point);
                        if (latestValue.isUndefined() ||
                            isBetterTimelineCandidate(loop, frameIndex, time, latestLoop, latestFrame, latestTimeSeconds)) {
                            latestValue = value;
                            latestLoop = loop;
                            latestFrame = frameIndex;
                            latestTimeSeconds = time;
                            if (point.contains(QStringLiteral("time_s")) ||
                                point.contains(QStringLiteral("sample_time_s")) ||
                                point.contains(QStringLiteral("runtime_time_s"))) {
                                latestTime = QString::number(time, 'g', 8);
                            }
                        }
                    }
                    if (latestValue.isUndefined()) {
                        continue;
                    }
                    if (!isDisplayableScalarValue(latestValue)) {
                        continue;
                    }

                    platformRows.push_back({
                        displayMetricLabel(metricLabel),
                        scalarValueText(latestValue),
                        seriesId.isEmpty() ? metricLabel : seriesId,
                        branch.isEmpty() ? QStringLiteral("当前运行") : branch,
                        unit,
                        QStringLiteral("平台时间序列"),
                        latestTime.isEmpty()
                            ? QStringLiteral("平台直接汇总的标量")
                            : QStringLiteral("平台直接汇总的标量；time=%1s").arg(latestTime)
                    });
                    displayedMetricLabels[metricLabel] = true;
                }
            }
        }
    }

    const QJsonObject timelineRoot = readJsonObject(platformTimelinePath);
    refreshPlatformCoreParameterChart_(timelineRoot);
    const auto appendLatestBallisticPreviewFromArray = [&](const QJsonArray& frames) {
        QJsonObject latestPreview;
        QString latestBranch;
        QString latestNode;
        int latestLoop = std::numeric_limits<int>::min();
        int latestFrame = std::numeric_limits<int>::min();
        double latestTime = -std::numeric_limits<double>::infinity();

        for (const QJsonValue& frameValue : frames) {
            if (!frameValue.isObject()) {
                continue;
            }
            const QJsonObject frame = frameValue.toObject();
            const QJsonObject preview = platformBallisticPreview(frame);
            if (preview.isEmpty() ||
                !preview.contains(QStringLiteral("components.ballistic.h")) ||
                !preview.contains(QStringLiteral("components.ballistic.ma"))) {
                continue;
            }

            const int loop = jsonLoop(frame);
            const int frameIndex = jsonFrameIndex(frame);
            const double time = jsonTimeSeconds(frame);
            if (latestPreview.isEmpty() ||
                isBetterTimelineCandidate(loop, frameIndex, time, latestLoop, latestFrame, latestTime)) {
                latestPreview = preview;
                latestBranch = frame.value(QStringLiteral("branch_id")).toString(QStringLiteral("main.online"));
                latestNode = frame.value(QStringLiteral("node_id")).toString();
                latestLoop = loop;
                latestFrame = frameIndex;
                latestTime = time;
            }
        }

        if (latestPreview.isEmpty()) {
            return;
        }

        const QString group = latestBranch.isEmpty() ? QStringLiteral("main.online") : latestBranch;
        const QString note = QStringLiteral("online posterior preview: frame=%1 loop=%2 time=%3s node=%4")
            .arg(latestFrame)
            .arg(latestLoop)
            .arg(QString::number(latestTime, 'g', 8),
                 latestNode.isEmpty() ? QStringLiteral("-") : latestNode);
        const auto appendFallback = [&](const QString& rawLabel,
                                        const QString& previewKey,
                                        const QString& metricId,
                                        const QString& unit) {
            const QString metricLabel = canonicalMetricLabel(rawLabel);
            if (displayedMetricLabels.find(metricLabel) != displayedMetricLabels.end()) {
                return;
            }
            const QJsonValue value = latestPreview.value(previewKey);
            if (!shouldDisplaySeriesMetric(metricLabel) || !isDisplayableScalarValue(value)) {
                return;
            }
            platformRows.push_back({
                displayMetricLabel(metricLabel),
                scalarValueText(value),
                metricId,
                group,
                displayMetricUnit(metricLabel, unit),
                QStringLiteral("run_timeline_index/ballistic_state"),
                note
            });
            displayedMetricLabels[metricLabel] = true;
        };

        appendFallback(QStringLiteral("components.ballistic.time_s"),
            QStringLiteral("components.ballistic.time_s"),
            QStringLiteral("online.posterior.components.ballistic.time_s"),
            QStringLiteral("s"));
        appendFallback(QStringLiteral("components.ballistic.h"),
            QStringLiteral("components.ballistic.h"),
            QStringLiteral("online.posterior.components.ballistic.h"),
            QStringLiteral("m"));
        appendFallback(QStringLiteral("components.ballistic.ma"),
            QStringLiteral("components.ballistic.ma"),
            QStringLiteral("online.posterior.components.ballistic.ma"),
            QStringLiteral("-"));
        appendFallback(QStringLiteral("components.ballistic.alpha"),
            QStringLiteral("components.ballistic.alpha"),
            QStringLiteral("online.posterior.components.ballistic.alpha"),
            QStringLiteral("rad"));
        appendFallback(QStringLiteral("components.ballistic.q"),
            QStringLiteral("components.ballistic.q"),
            QStringLiteral("online.posterior.components.ballistic.q"),
            QStringLiteral("Pa"));
    };
    appendLatestBallisticPreviewFromArray(timelineRoot.value(QStringLiteral("online_frames")).toArray());
    appendLatestBallisticPreviewFromArray(timelineRoot.value(QStringLiteral("timeline_points")).toArray());

    std::map<QString, launchsupport::PlatformQoiView> latestQoiByPort;
    for (const auto& qoi : platform_snapshot_.qois) {
        const QString portId = QString::fromStdString(qoi.port_id);
        if (portId.isEmpty()) {
            continue;
        }
        const auto it = latestQoiByPort.find(portId);
        if (it == latestQoiByPort.end() || qoi.public_time_s >= it->second.public_time_s) {
            latestQoiByPort[portId] = qoi;
        }
    }

    for (const auto& item : latestQoiByPort) {
        const auto& qoi = item.second;
        const QString portId = QString::fromStdString(qoi.port_id);
        const QString group = portId == QStringLiteral("qoi.shell_failure")
            ? QStringLiteral("外壳风险")
            : portId == QStringLiteral("qoi.structure_failure")
                ? QStringLiteral("结构风险")
                : portId == QStringLiteral("qoi.remaining_life")
                    ? QStringLiteral("寿命评估")
                    : QStringLiteral("QoI");
        const QString runtimeOutputsPath = platformPathText(qoi.source_run_dir / "runtime_outputs.json");
        const QJsonObject outputsRoot = readJsonObject(runtimeOutputsPath);
        const QJsonObject nodeObject = outputsRoot.value(QStringLiteral("outputs"))
            .toObject()
            .value(QString::fromStdString(qoi.node_id))
            .toObject();
        const QJsonObject portObject = nodeObject.value(QStringLiteral("outputs"))
            .toObject()
            .value(portId)
            .toObject();
        if (portObject.isEmpty()) {
            continue;
        }

        const auto appendQoiScalar = [&](const QString& label,
                                         const QString& metricId,
                                         const QJsonValue& value,
                                         const QString& note) {
            if (!isDisplayableScalarValue(value)) {
                return;
            }
            platformRows.push_back({
                label,
                scalarValueText(value),
                metricId,
                group,
                QString(),
                QStringLiteral("QoI端口"),
                note
            });
        };

        appendQoiScalar(
            group + QStringLiteral("风险"),
            portId + QStringLiteral(".risk"),
            portObject.value(QStringLiteral("risk")),
            QStringLiteral("平台QoI算子输出；time=%1s").arg(QString::number(qoi.public_time_s, 'g', 8)));
        appendQoiScalar(
            group + QStringLiteral("是否失效"),
            portId + QStringLiteral(".failed"),
            portObject.value(QStringLiteral("failed")),
            QStringLiteral("平台QoI算子输出"));
        appendQoiScalar(
            group + QStringLiteral("控制指标"),
            portId + QStringLiteral(".governing_metric"),
            portObject.value(QStringLiteral("governing_metric")),
            QStringLiteral("平台QoI算子输出"));
        appendQoiScalar(
            group + QStringLiteral("关键位置"),
            portId + QStringLiteral(".critical_node_id"),
            portObject.value(QStringLiteral("critical_node_id")),
            QStringLiteral("平台QoI算子输出"));
    }

    const QString progressPath = platformPathText(platform_snapshot_.run_dir / "mainline_progress.json");
    const QJsonObject progress = readJsonObject(progressPath);
    if (!progress.isEmpty()) {
        const QJsonObject online = progress.value(QStringLiteral("online")).toObject();
        const QJsonObject prediction = progress.value(QStringLiteral("prediction")).toObject();
        const auto appendProgressScalar = [&](const QString& name,
                                              const QString& id,
                                              const QJsonValue& value,
                                              const QString& group) {
            if (!isDisplayableScalarValue(value)) {
                return;
            }
            platformRows.push_back({
                name,
                scalarValueText(value),
                id,
                group,
                QString(),
                QStringLiteral("平台进度"),
                QStringLiteral("mainline_progress.json")
            });
        };
        appendProgressScalar(QStringLiteral("在线帧数"), QStringLiteral("online.completed_frames"),
            online.value(QStringLiteral("completed_frames")), QStringLiteral("运行进度"));
        appendProgressScalar(QStringLiteral("请求帧数"), QStringLiteral("online.requested_frames"),
            online.value(QStringLiteral("requested_frames")), QStringLiteral("运行进度"));
        appendProgressScalar(QStringLiteral("已完成预测分支"), QStringLiteral("prediction.completed_runs"),
            prediction.value(QStringLiteral("completed_runs")), QStringLiteral("分支预测"));
        appendProgressScalar(QStringLiteral("失败预测分支"), QStringLiteral("prediction.failed_runs"),
            prediction.value(QStringLiteral("failed_runs")), QStringLiteral("分支预测"));
        appendProgressScalar(QStringLiteral("当前阶段"), QStringLiteral("stage"),
            progress.value(QStringLiteral("stage")), QStringLiteral("运行状态"));
        appendProgressScalar(QStringLiteral("运行状态"), QStringLiteral("status"),
            progress.value(QStringLiteral("status")), QStringLiteral("运行状态"));
    }

    if (platformRows.size() == 1) {
        platformRows.push_back({
            QStringLiteral("平台指标"),
            QStringLiteral("等待平台产出可展示标量"),
            QStringLiteral("series_manifest/qoi/runtime_progress"),
            QString::fromStdString(platform_snapshot_.primary_branch_id),
            QString(),
            QString::fromStdString(platform_snapshot_.run_id),
            platformManifestPath
        });
    }

    fillTableWidget(ui.paramCompareTableWidget, platformRows);
    return;
}

void EnvPredictorUI::renderPlatformCurrentFrame()
{
    if (!runtime_view_) {
        if (platformFrameStatusLabel_) {
            platformFrameStatusLabel_->setText(QStringLiteral("缺少对象 runtime_snapshot.json，无法映射平台 field artifact"));
        }
        return;
    }

    const auto fields = platformFieldsForCurrentFrame();
    QStringList signatureParts;
    signatureParts << platformCurrentBranchId_ << QString::number(platformCurrentLoop_);
    for (const auto& field : fields) {
        const QString path = platformPathText(field.artifact_path);
        const QFileInfo info(path);
        signatureParts << platformFieldIdentityKey(field)
                       << path
                       << QString::number(info.exists() ? info.lastModified().toMSecsSinceEpoch() : -1)
                       << QString::number(info.exists() ? info.size() : -1);
    }
    const QString frameSignature = signatureParts.join(QLatin1Char('|'));
    if (frameSignature == platformLastRenderedFrameSignature_) {
        updatePlatformSensorDisplayForLoop(platformCurrentLoop_);
        return;
    }
    platformLastRenderedFrameSignature_ = frameSignature;

    platformFieldOrder_.clear();

    for (const auto& field : fields) {
        const QString key = platformFieldIdentityKey(field);
        platformFieldOrder_.push_back(key);
        if (platformFieldVisibility_.find(key) == platformFieldVisibility_.end()) {
            platformFieldVisibility_[key] = true;
        }

        auto*& widget = platformFieldWidgets_[key];
        if (widget == nullptr) {
            widget = new flightenv::ui::demo::VtkModelFieldWidget(vtkContainer);
            widget->setMinimumHeight(300);
            widget->setAssetRoot(workspacePath(QStringLiteral("_deps/example")));
            widget->setRuntimeSnapshot(runtime_view_->snapshot);
        }

        QString readError;
        bool loading = false;
        const std::vector<double> values = cachedPlatformFieldArtifactValues(field, &readError, &loading);
        if (loading && values.empty()) {
            widget->setStatusMessage(widget->hasRenderedValues()
                                         ? QStringLiteral("云图数据后台加载中，保留上一帧显示...")
                                         : QStringLiteral("云图数据后台加载中..."));
            continue;
        }
        if (!readError.isEmpty()) {
            widget->clearField(readError);
            continue;
        }
        if (field.node_count > 0 &&
            (values.size() < static_cast<std::size_t>(field.node_count) ||
             values.size() % static_cast<std::size_t>(field.node_count) != 0)) {
            widget->clearField(QStringLiteral("field artifact values cannot map to node_count: values=%1 node_count=%2")
                                   .arg(values.size())
                                   .arg(static_cast<qlonglong>(field.node_count)));
            continue;
        }

        widget->renderPlatformFieldArtifact(
            field,
            values,
            0,
            QStringLiteral("%1 · loop %2").arg(platformFieldTitle(field)).arg(field.loop_iteration_index));
    }

    const QString controlSignature = QStringList(platformFieldOrder_.begin(), platformFieldOrder_.end()).join(QLatin1Char('\n'));
    if (controlSignature != platformFieldControlSignature_) {
        platformFieldControlSignature_ = controlSignature;
        rebuildPlatformFieldVisibilityControls();
        layoutPlatformFieldWidgets();
    }
    renderGraphPlatformFieldArtifacts_();
    int sensorLoop = platformCurrentLoop_;
    if (sensorLoop < 0) {
        for (const auto& point : platform_snapshot_.timeline_points) {
            sensorLoop = std::max(sensorLoop, point.loop_iteration_index);
        }
    }
    updatePlatformSensorDisplayForLoop(sensorLoop);

    if (platformFrameStatusLabel_) {
        platformFrameStatusLabel_->setText(
            QStringLiteral("当前帧：%1；场：%2")
                .arg(platformCurrentLoop_ >= 0 ? QString::number(platformCurrentLoop_ + 1) : QStringLiteral("-"))
                .arg(fields.size()));
    }
}

void EnvPredictorUI::layoutPlatformFieldWidgets()
{
    if (!platformFieldGridLayout_) {
        return;
    }

    QLayoutItem* item = nullptr;
    while ((item = platformFieldGridLayout_->takeAt(0)) != nullptr) {
        delete item;
    }

    const int columns = std::max(1, ui.windowNumspinBox->value());
    for (int col = 0; col < columns; ++col) {
        platformFieldGridLayout_->setColumnStretch(col, 1);
    }

    int visibleCount = 0;
    for (const QString& key : platformFieldOrder_) {
        auto it = platformFieldWidgets_.find(key);
        if (it == platformFieldWidgets_.end() || it->second == nullptr) {
            continue;
        }
        const bool visible = platformFieldVisibility_.find(key) == platformFieldVisibility_.end()
            ? true
            : platformFieldVisibility_[key];
        if (!visible) {
            it->second->hide();
            continue;
        }
        if (it->second->parentWidget() != vtkContainer) {
            it->second->setParent(vtkContainer);
        }
        const int row = visibleCount / columns;
        const int col = visibleCount % columns;
        platformFieldGridLayout_->addWidget(it->second, row, col);
        it->second->show();
        ++visibleCount;
    }
    if (vtkContainer) {
        vtkContainer->updateGeometry();
    }
    if (fieldScrollArea) {
        fieldScrollArea->updateGeometry();
    }
}

void EnvPredictorUI::rebuildPlatformFieldVisibilityControls()
{
    if (!platformFieldCheckLayout_) {
        return;
    }
    QLayoutItem* item = nullptr;
    while ((item = platformFieldCheckLayout_->takeAt(0)) != nullptr) {
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }

    const int columns = 3;
    int row = 0;
    int col = 0;
    for (const QString& key : platformFieldOrder_) {
        const auto widgetIt = platformFieldWidgets_.find(key);
        if (widgetIt == platformFieldWidgets_.end() || widgetIt->second == nullptr) {
            continue;
        }
        QString label = key;
        const int firstSeparator = label.indexOf(QLatin1Char('|'));
        if (firstSeparator >= 0) {
            label = label.left(firstSeparator);
        }
        auto* check = new QCheckBox(label, platformFieldCheckContainer_);
        check->setProperty("fieldKey", key);
        check->setChecked(platformFieldVisibility_.find(key) == platformFieldVisibility_.end()
            ? true
            : platformFieldVisibility_[key]);
        connect(check, &QCheckBox::toggled, this, &EnvPredictorUI::onPlatformFieldVisibilityChanged);
        platformFieldCheckLayout_->addWidget(check, row, col);
        ++col;
        if (col >= columns) {
            col = 0;
            ++row;
        }
    }
}

void EnvPredictorUI::onPlatformBranchChanged(int index)
{
    if (platformUpdatingUi_ || !platformBranchCombo_) {
        return;
    }
    platformCurrentBranchId_ = platformBranchCombo_->itemData(index).toString();
    platformFrameLoops_ = platformLoopsForBranch(platformCurrentBranchId_);
    if (platformFrameLoops_.empty()) {
        platformCurrentLoop_ = -1;
    } else if (platformFollowLatestCheck_ && platformFollowLatestCheck_->isChecked()) {
        platformCurrentLoop_ = platformFrameLoops_.back();
    } else if (std::find(platformFrameLoops_.begin(), platformFrameLoops_.end(), platformCurrentLoop_) == platformFrameLoops_.end()) {
        platformCurrentLoop_ = platformFrameLoops_.back();
    }
    refreshPlatformFieldNavigation();
    schedulePlatformCurrentFrameRender();
}

void EnvPredictorUI::onPlatformFrameSliderChanged(int value)
{
    if (platformUpdatingUi_ || value < 0 || value >= static_cast<int>(platformFrameLoops_.size())) {
        return;
    }
    if (platformFollowLatestCheck_ && platformFollowLatestCheck_->isChecked()) {
        const QSignalBlocker blocker(platformFollowLatestCheck_);
        platformFollowLatestCheck_->setChecked(false);
    }
    platformCurrentLoop_ = platformFrameLoops_[static_cast<std::size_t>(value)];
    updatePlatformFrameProgressLabel();
    updatePlatformCoreParameterTable();
    schedulePlatformCurrentFrameRender();
}

void EnvPredictorUI::onPlatformFollowLatestToggled(bool checked)
{
    if (platformUpdatingUi_) {
        return;
    }
    if (checked) {
        selectPlatformLatestFrame();
    }
}

void EnvPredictorUI::onPlatformFieldVisibilityChanged()
{
    auto* check = qobject_cast<QCheckBox*>(sender());
    if (!check) {
        return;
    }
    const QString key = check->property("fieldKey").toString();
    if (key.isEmpty()) {
        return;
    }
    platformFieldVisibility_[key] = check->isChecked();
    layoutPlatformFieldWidgets();
}

void EnvPredictorUI::loadPlatformSensorResources()
{
    loadPlatformSensorLayouts();
    loadPlatformSensorFrames();
}

void EnvPredictorUI::loadPlatformSensorLayouts()
{
    if (platformSensorLayoutsLoaded_) {
        return;
    }
    platformSensorLayoutsLoaded_ = true;
    platformSensorLayouts_.clear();

    const auto configuredObjectRoot = environmentPath("FLIGHTENV_PLATFORM_OBJECT_ROOT");
    const auto objectRoot = configuredObjectRoot.empty()
        ? (workspaceRootFromEnvironment() / "flightenv-object-reentry-vehicle")
        : configuredObjectRoot;
    const auto manifestPath = objectRoot / "object" / "twin_object.json";

    QFile manifestFile(platformPathText(manifestPath));
    if (!manifestFile.open(QIODevice::ReadOnly)) {
        qWarning() << "platform sensor layouts skipped: cannot read object manifest"
                   << platformPathText(manifestPath);
        return;
    }

    QJsonParseError manifestError{};
    const QJsonDocument manifestDoc = QJsonDocument::fromJson(manifestFile.readAll(), &manifestError);
    if (manifestError.error != QJsonParseError::NoError || !manifestDoc.isObject()) {
        qWarning() << "platform sensor layouts skipped: object manifest JSON parse failed"
                   << manifestError.errorString();
        return;
    }

    std::map<QString, QJsonObject> layoutArtifacts;
    const QJsonArray resources = manifestDoc.object().value(QStringLiteral("resources")).toArray();
    for (const QJsonValue& resourceValue : resources) {
        const QJsonObject resource = resourceValue.toObject();
        if (resource.value(QStringLiteral("resource_type")).toString() != QStringLiteral("sensor_layout")) {
            continue;
        }

        const QString artifactPathText = resource.value(QStringLiteral("layout_artifact_path")).toString();
        const QString layoutId = resource.value(QStringLiteral("layout_artifact_id"))
            .toString(resource.value(QStringLiteral("resource_id")).toString());
        const QString channelPrefix = resource.value(QStringLiteral("channel_prefix")).toString();
        if (artifactPathText.isEmpty() || layoutId.isEmpty() || channelPrefix.isEmpty()) {
            continue;
        }

        QJsonObject artifactRoot;
        const auto artifactIt = layoutArtifacts.find(artifactPathText);
        if (artifactIt == layoutArtifacts.end()) {
            const std::filesystem::path artifactPath = objectRoot / artifactPathText.toStdString();
            QFile artifactFile(platformPathText(artifactPath));
            if (!artifactFile.open(QIODevice::ReadOnly)) {
                qWarning() << "platform sensor layout artifact skipped"
                           << platformPathText(artifactPath);
                continue;
            }
            QJsonParseError artifactError{};
            const QJsonDocument artifactDoc = QJsonDocument::fromJson(artifactFile.readAll(), &artifactError);
            if (artifactError.error != QJsonParseError::NoError || !artifactDoc.isObject()) {
                qWarning() << "platform sensor layout artifact JSON parse failed"
                           << artifactError.errorString();
                continue;
            }
            artifactRoot = artifactDoc.object();
            layoutArtifacts[artifactPathText] = artifactRoot;
        } else {
            artifactRoot = artifactIt->second;
        }

        QJsonObject layoutObject;
        const QJsonArray layouts = artifactRoot.value(QStringLiteral("layouts")).toArray();
        for (const QJsonValue& layoutValue : layouts) {
            const QJsonObject candidate = layoutValue.toObject();
            if (candidate.value(QStringLiteral("resource_id")).toString() == layoutId) {
                layoutObject = candidate;
                break;
            }
        }
        if (layoutObject.isEmpty()) {
            qWarning() << "platform sensor layout id missing from artifact" << layoutId;
            continue;
        }

        PlatformSensorLayoutView layout;
        layout.resource_id = layoutId;
        layout.display_name = layoutObject.value(QStringLiteral("display_name")).toString(layoutId);
        layout.channel_prefix = layoutObject.value(QStringLiteral("channel_prefix")).toString(channelPrefix);
        layout.value_dim = std::max(1, layoutObject.value(QStringLiteral("value_dim"))
            .toInt(resource.value(QStringLiteral("value_dim")).toInt(1)));
        try {
            layout.subject = contracts::subject_type_from_string(
                layoutObject.value(QStringLiteral("subject")).toString(QStringLiteral("P")).toStdString());
        } catch (const std::exception&) {
            layout.subject = contracts::SubjectType::P;
        }

        const QJsonArray nodes = layoutObject.value(QStringLiteral("nodes")).toArray();
        layout.nodes.reserve(static_cast<std::size_t>(nodes.size()));
        for (const QJsonValue& nodeValue : nodes) {
            const QJsonObject node = nodeValue.toObject();
            layout.nodes.push_back({
                node.value(QStringLiteral("x")).toDouble(),
                node.value(QStringLiteral("y")).toDouble(),
                node.value(QStringLiteral("z")).toDouble()
            });
        }

        if (!layout.nodes.empty()) {
            platformSensorLayouts_.push_back(std::move(layout));
        }
    }
}

void EnvPredictorUI::loadPlatformSensorFrames()
{
    if (platformSensorFramesLoaded_) {
        return;
    }
    platformSensorFramesLoaded_ = true;
    platformSensorFrames_.clear();
    if (platformSensorLayouts_.empty()) {
        return;
    }

    const auto configuredObjectRoot = environmentPath("FLIGHTENV_PLATFORM_OBJECT_ROOT");
    const auto objectRoot = configuredObjectRoot.empty()
        ? (workspaceRootFromEnvironment() / "flightenv-object-reentry-vehicle")
        : configuredObjectRoot;
    const auto configuredStream = environmentPath("FLIGHTENV_PLATFORM_EXTERNAL_OBSERVATION_STREAM");
    const auto streamPath = configuredStream.empty()
        ? (objectRoot / "fixtures" / "sensor_stream_db70.json")
        : configuredStream;

    QFile streamFile(platformPathText(streamPath));
    if (!streamFile.open(QIODevice::ReadOnly)) {
        qWarning() << "platform sensor stream skipped: cannot read object stream"
                   << platformPathText(streamPath);
        return;
    }

    QJsonParseError streamError{};
    const QJsonDocument streamDoc = QJsonDocument::fromJson(streamFile.readAll(), &streamError);
    if (streamError.error != QJsonParseError::NoError || !streamDoc.isObject()) {
        qWarning() << "platform sensor stream JSON parse failed" << streamError.errorString();
        return;
    }

    const QJsonArray frames = streamDoc.object().value(QStringLiteral("frames")).toArray();
    platformSensorFrames_.reserve(static_cast<std::size_t>(frames.size()));
    for (const QJsonValue& frameValue : frames) {
        const QJsonObject frameRoot = frameValue.toObject();
        const QJsonObject frame = frameRoot.value(QStringLiteral("frame")).toObject();
        const QJsonArray sensorIds = frame.value(QStringLiteral("sensor_ids")).toArray();
        const QJsonArray values = frame.value(QStringLiteral("values")).toArray();
        if (sensorIds.isEmpty() || values.isEmpty()) {
            continue;
        }

        PlatformSensorFrameView out;
        out.loop = frameRoot.value(QStringLiteral("loop_iteration_index"))
            .toInt(frame.value(QStringLiteral("loop_iteration_index")).toInt(frameRoot.value(QStringLiteral("frame_index")).toInt(-1)));
        const double sampleTimeS = frameRoot.value(QStringLiteral("sample_time_s"))
            .toDouble(frame.value(QStringLiteral("sample_time_s")).toDouble(0.0));
        out.stamp_ns = static_cast<qint64>(sampleTimeS * 1000000000.0);

        const int count = std::min(sensorIds.size(), values.size());
        for (int i = 0; i < count; ++i) {
            const QString sensorId = sensorIds.at(i).toString();
            if (sensorId.isEmpty() || !values.at(i).isDouble()) {
                continue;
            }
            for (const auto& layout : platformSensorLayouts_) {
                if (sensorId.startsWith(layout.channel_prefix + QStringLiteral("_"))) {
                    out.values_by_resource[layout.resource_id].push_back(values.at(i).toDouble());
                    break;
                }
            }
        }

        if (!out.values_by_resource.empty()) {
            platformSensorFrames_.push_back(std::move(out));
        }
    }

    std::sort(platformSensorFrames_.begin(), platformSensorFrames_.end(),
        [](const PlatformSensorFrameView& left, const PlatformSensorFrameView& right) {
            return left.loop < right.loop;
        });
}

void EnvPredictorUI::updatePlatformSensorDisplayForLoop(int loop)
{
    if (loop < 0) {
        return;
    }
    loadPlatformSensorResources();
    if (platformSensorLayouts_.empty() || platformSensorFrames_.empty()) {
        return;
    }

    const bool resetHistory =
        platformSensorAppliedLoop_ < 0 ||
        loop < platformSensorAppliedLoop_ ||
        sensorsss.size() != platformSensorLayouts_.size();
    if (resetHistory) {
        sensorsss.assign(platformSensorLayouts_.size(), {});
        platformSensorAppliedLoop_ = -1;
        if (!markerPoints.empty()) {
            const int selected = std::clamp(sensorflg, 0, static_cast<int>(markerPoints.size()) - 1);
            on_comboBox_4_currentIndexChanged(selected);
        }
    }

    for (const auto& frame : platformSensorFrames_) {
        if (frame.loop <= platformSensorAppliedLoop_) {
            continue;
        }
        if (frame.loop > loop) {
            break;
        }

        launchsupport::SensorViewModel view;
        view.stamp_ns = static_cast<contracts::TimestampNs>(frame.stamp_ns);
        view.key = frame.loop;
        view.dto.stamp_ns = view.stamp_ns;
        view.dto.key = view.key;
        view.channels.reserve(platformSensorLayouts_.size());
        view.dto.channels.reserve(platformSensorLayouts_.size());

        for (const auto& layout : platformSensorLayouts_) {
            const auto valuesIt = frame.values_by_resource.find(layout.resource_id);
            const std::vector<double> emptyValues;
            const std::vector<double>& values =
                valuesIt == frame.values_by_resource.end() ? emptyValues : valuesIt->second;

            contracts::SensorChannelDTO dtoChannel;
            dtoChannel.subject = layout.subject;
            dtoChannel.taskpoint_id = frame.loop;
            dtoChannel.values = values;
            view.dto.channels.push_back(dtoChannel);

            launchsupport::SubjectValuesView channel;
            channel.subject = layout.subject;
            channel.subject_index = static_cast<int>(layout.subject);
            channel.taskpoint_id = frame.loop;
            const size_t stride = static_cast<size_t>(std::max(1, layout.value_dim));
            const size_t expectedNodes = layout.nodes.size();
            channel.values_by_node.reserve(expectedNodes);
            for (size_t nodeIndex = 0; nodeIndex < expectedNodes; ++nodeIndex) {
                const size_t begin = nodeIndex * stride;
                std::vector<double> row;
                row.reserve(stride);
                for (size_t component = 0; component < stride; ++component) {
                    const size_t valueIndex = begin + component;
                    row.push_back(valueIndex < values.size() ? values[valueIndex] : 0.0);
                }
                channel.values_by_node.push_back(std::move(row));
            }
            view.channels.push_back(std::move(channel));
        }

        handleSensorView(view);
        platformSensorAppliedLoop_ = frame.loop;
    }
}
#endif

/**
* @brief 初始化ROS信息参数表格
*/
void EnvPredictorUI::initParameterTable()
{
#ifdef FLIGHTENV_USE_PLATFORM_BACKEND
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
    RCLCPP_INFO(envUiLogger(), "Platform mode: skipped legacy parameter table initialization.");
    return;
#endif

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
#ifdef FLIGHTENV_USE_PLATFORM_BACKEND
    comboBoxInfCurrentIndexChanged(0);
#endif
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

namespace {

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

} // namespace

void EnvPredictorUI::buildRuntimeChainPage_()
{
    if (pageRuntimeChain_) {
        return;
    }

    pageRuntimeChain_ = new QWidget(ui.tabWidget_main);
    auto* root = new QVBoxLayout(pageRuntimeChain_);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);

    auto* summaryRow = new QWidget(pageRuntimeChain_);
    auto* summaryLayout = new QHBoxLayout(summaryRow);
    summaryLayout->setContentsMargins(0, 0, 0, 0);
    summaryLayout->setSpacing(8);

    lbObjectSummary_ = new QLabel(summaryRow);
    lbObjectSummary_->setWordWrap(true);
    lbObjectSummary_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    lbObjectSummary_->setStyleSheet(QStringLiteral(
        "QLabel{padding:8px;background:#eef6f6;border:1px solid #9cc9c9;border-radius:4px;font-weight:600;}"));
    summaryLayout->addWidget(lbObjectSummary_, 1);

    auto* objectActionBox = new QGroupBox(QStringLiteral("模型初始化"), summaryRow);
    objectActionBox->setMaximumWidth(260);
    auto* objectActionLayout = new QVBoxLayout(objectActionBox);
    objectActionLayout->setContentsMargins(8, 8, 8, 8);
    objectActionLayout->setSpacing(6);
    objectTrainButton_ = new QPushButton(QStringLiteral("训练 / 初始化模型"), objectActionBox);
    objectTrainButton_->setToolTip(QStringLiteral("执行对象模型初始化或训练准备；运行控制仍在模拟演示页的任务时间轴面板中完成"));
    objectActionLayout->addWidget(objectTrainButton_);
    auto* objectTrainHint = new QLabel(QStringLiteral("对象模型相关操作放在对象信息页；在线开始、暂停、复位只保留在任务时间轴面板。"), objectActionBox);
    objectTrainHint->setWordWrap(true);
    objectTrainHint->setStyleSheet(QStringLiteral("QLabel{color:#64748b;font-size:11px;}"));
    objectActionLayout->addWidget(objectTrainHint);
    summaryLayout->addWidget(objectActionBox);
    root->addWidget(summaryRow);
    connect(objectTrainButton_, &QPushButton::clicked, this, &EnvPredictorUI::on_trainBtn_clicked, Qt::UniqueConnection);

    lbObjectFlow_ = new QLabel(pageRuntimeChain_);
    lbObjectFlow_->setWordWrap(true);
    lbObjectFlow_->setStyleSheet(QStringLiteral(
        "QLabel{padding:8px;background:#ffffff;border:1px solid #d1d5db;border-radius:4px;}"));
    root->addWidget(lbObjectFlow_);

    auto* scroll = new QScrollArea(pageRuntimeChain_);
    scroll->setWidgetResizable(true);
    auto* content = new QWidget(scroll);
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(8);
    scroll->setWidget(content);
    root->addWidget(scroll, 1);

    auto* topSplitter = new QSplitter(Qt::Horizontal, content);
    tblObjectBasic_ = makeGraphTable(topSplitter, {
        QStringLiteral("项目"), QStringLiteral("内容"), QStringLiteral("来源")
    });
    tblObjectMeshes_ = makeGraphTable(topSplitter, {
        QStringLiteral("网格"), QStringLiteral("部件"), QStringLiteral("文件"), QStringLiteral("说明")
    });
    topSplitter->addWidget(tblObjectBasic_);
    topSplitter->addWidget(tblObjectMeshes_);
    topSplitter->setStretchFactor(0, 1);
    topSplitter->setStretchFactor(1, 2);
    contentLayout->addWidget(topSplitter);

    auto* sensorGroup = new QGroupBox(QStringLiteral("传感器布置"), content);
    auto* sensorLayout = new QHBoxLayout(sensorGroup);
    sensorLayout->setContentsMargins(8, 8, 8, 8);
    sensorLayout->setSpacing(8);
    auto* sensorLeftPanel = new QWidget(sensorGroup);
    auto* sensorLeftLayout = new QVBoxLayout(sensorLeftPanel);
    sensorLeftLayout->setContentsMargins(0, 0, 0, 0);
    sensorLeftLayout->setSpacing(6);
    auto* meshRow = new QHBoxLayout();
    meshRow->setSpacing(6);
    meshRow->addWidget(new QLabel(QStringLiteral("显示网格"), sensorLeftPanel));
    objectMeshCombo_ = new QComboBox(sensorLeftPanel);
    objectMeshCombo_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    objectMeshCombo_->setToolTip(QStringLiteral("选择传感器位置三维图里显示的一套对象网格，避免多套网格叠加"));
    meshRow->addWidget(objectMeshCombo_, 1);
    sensorLeftLayout->addLayout(meshRow);

    auto* sensorLayoutRow = new QHBoxLayout();
    sensorLayoutRow->setSpacing(6);
    sensorLayoutRow->addWidget(new QLabel(QStringLiteral("传感器组"), sensorLeftPanel));
    objectSensorLayoutCombo_ = new QComboBox(sensorLeftPanel);
    objectSensorLayoutCombo_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    objectSensorLayoutCombo_->setToolTip(QStringLiteral("按数据库 fieldnodeset / 对象包 sensor layout 选择一组真实传感器点位显示"));
    sensorLayoutRow->addWidget(objectSensorLayoutCombo_, 1);
    sensorLeftLayout->addLayout(sensorLayoutRow);

    objectSensorVtkHost_ = new QWidget(sensorGroup);
    objectSensorVtkHost_->setMinimumHeight(300);
    objectSensorVtkHost_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* objectSensorHostLayout = new QVBoxLayout(objectSensorVtkHost_);
    objectSensorHostLayout->setContentsMargins(0, 0, 0, 0);
    auto* sensorPlaceholder = new QLabel(QStringLiteral("启动运行后显示传感器在对象上的三维位置"), objectSensorVtkHost_);
    sensorPlaceholder->setAlignment(Qt::AlignCenter);
    sensorPlaceholder->setStyleSheet(QStringLiteral("QLabel{color:#64748b;border:1px dashed #cbd5e1;}"));
    objectSensorHostLayout->addWidget(sensorPlaceholder);
    sensorLeftLayout->addWidget(objectSensorVtkHost_, 1);

    tblObjectSensors_ = makeGraphTable(sensorGroup, {
        QStringLiteral("传感器组"), QStringLiteral("部件/位置"), QStringLiteral("测点"),
        QStringLiteral("通道数"), QStringLiteral("单位"), QStringLiteral("数据前缀")
    });
    sensorLayout->addWidget(sensorLeftPanel, 2);
    sensorLayout->addWidget(tblObjectSensors_, 3);
    contentLayout->addWidget(sensorGroup);

    auto* runConfigGroup = new QGroupBox(QStringLiteral("运行配置（点开始前可改）"), content);
    auto* runConfigLayout = new QGridLayout(runConfigGroup);
    runConfigLayout->setContentsMargins(8, 8, 8, 8);
    runConfigLayout->setHorizontalSpacing(10);
    runConfigLayout->setVerticalSpacing(6);

    auto makeSpin = [&](const int minValue, const int maxValue, const QString& tooltip) {
        auto* spin = new QSpinBox(runConfigGroup);
        spin->setRange(minValue, maxValue);
        spin->setToolTip(tooltip);
        return spin;
    };
    objectOnlineFramesSpin_ = makeSpin(1, 100000, QStringLiteral("在线主线回放总帧数"));
    objectPredictionEveryFramesSpin_ = makeSpin(1, 100000, QStringLiteral("每隔多少在线帧开启一次未来预测分支"));
    objectFutureMaxIterationsSpin_ = makeSpin(1, 100000, QStringLiteral("每个未来预测分支最多向前预测多少步"));
    objectBranchChunkIterationsSpin_ = makeSpin(1, 100000, QStringLiteral("每次调度分支推进的步数"));
    objectMaxConcurrentBranchesSpin_ = makeSpin(1, 1024, QStringLiteral("同时运行的未来预测分支上限"));
    objectReplayTimeScaleSpin_ = new QDoubleSpinBox(runConfigGroup);
    objectReplayTimeScaleSpin_->setRange(0.01, 1000.0);
    objectReplayTimeScaleSpin_->setDecimals(2);
    objectReplayTimeScaleSpin_->setSingleStep(0.25);
    objectReplayTimeScaleSpin_->setToolTip(QStringLiteral("平台时钟回放速度倍率，不改变输入数据自身时间步长"));
    objectSensorStreamEdit_ = new QLineEdit(runConfigGroup);
    objectSensorStreamEdit_->setToolTip(QStringLiteral("传感器外部回放流文件，点开始前应用到 runtime host"));

    auto addConfigRow = [&](const int row, const QString& label, QWidget* widget) {
        runConfigLayout->addWidget(new QLabel(label, runConfigGroup), row, 0);
        runConfigLayout->addWidget(widget, row, 1);
    };
    addConfigRow(0, QStringLiteral("在线帧数"), objectOnlineFramesSpin_);
    addConfigRow(1, QStringLiteral("分支触发间隔（帧）"), objectPredictionEveryFramesSpin_);
    addConfigRow(2, QStringLiteral("未来预测步数"), objectFutureMaxIterationsSpin_);
    addConfigRow(3, QStringLiteral("分支块步数"), objectBranchChunkIterationsSpin_);
    addConfigRow(4, QStringLiteral("最大并发分支"), objectMaxConcurrentBranchesSpin_);
    addConfigRow(5, QStringLiteral("回放速度倍率"), objectReplayTimeScaleSpin_);
    runConfigLayout->addWidget(new QLabel(QStringLiteral("传感器输入流"), runConfigGroup), 6, 0);
    auto* streamRow = new QWidget(runConfigGroup);
    auto* streamLayout = new QHBoxLayout(streamRow);
    streamLayout->setContentsMargins(0, 0, 0, 0);
    streamLayout->setSpacing(6);
    streamLayout->addWidget(objectSensorStreamEdit_, 1);
    auto* browseStreamButton = new QPushButton(QStringLiteral("选择"), streamRow);
    streamLayout->addWidget(browseStreamButton);
    runConfigLayout->addWidget(streamRow, 6, 1);
    auto* applyRunConfigButton = new QPushButton(QStringLiteral("应用到下一次开始"), runConfigGroup);
    objectRunConfigHint_ = new QLabel(QStringLiteral("修改后点“应用”或直接点“开始”生效；运行中修改需要重新开始。"), runConfigGroup);
    objectRunConfigHint_->setWordWrap(true);
    objectRunConfigHint_->setStyleSheet(QStringLiteral("QLabel{color:#475569;}"));
    runConfigLayout->addWidget(objectRunConfigHint_, 7, 0, 1, 2);
    runConfigLayout->addWidget(applyRunConfigButton, 8, 1, Qt::AlignRight);
    runConfigLayout->setColumnStretch(1, 1);
    contentLayout->addWidget(runConfigGroup);

    connect(browseStreamButton, &QPushButton::clicked, this, [this]() {
        const QString startPath = objectSensorStreamEdit_ && !objectSensorStreamEdit_->text().isEmpty()
            ? QFileInfo(objectSensorStreamEdit_->text()).absolutePath()
            : objectPackagePath(QStringLiteral("fixtures"));
        const QString selected = QFileDialog::getOpenFileName(
            this,
            QStringLiteral("选择传感器输入流"),
            startPath,
            QStringLiteral("JSON 文件 (*.json);;所有文件 (*.*)"));
        if (!selected.isEmpty() && objectSensorStreamEdit_) {
            objectSensorStreamEdit_->setText(QDir::toNativeSeparators(selected));
        }
    });
    connect(applyRunConfigButton, &QPushButton::clicked, this, &EnvPredictorUI::applyPlatformRunConfigFromUi_);
    connect(objectMeshCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        rebuildObjectSensorView_();
    }, Qt::UniqueConnection);
    connect(objectSensorLayoutCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        rebuildObjectSensorView_();
    }, Qt::UniqueConnection);

    auto* bottomSplitter = new QSplitter(Qt::Horizontal, content);
    tblRuntimeChain_ = makeGraphTable(bottomSplitter, {
        QStringLiteral("阶段"), QStringLiteral("使用"), QStringLiteral("算子"),
        QStringLiteral("输出"), QStringLiteral("说明")
    });
    tblObjectRuntimeConfig_ = makeGraphTable(bottomSplitter, {
        QStringLiteral("配置"), QStringLiteral("当前值"), QStringLiteral("来源/说明")
    });
    bottomSplitter->addWidget(tblRuntimeChain_);
    bottomSplitter->addWidget(tblObjectRuntimeConfig_);
    bottomSplitter->setStretchFactor(0, 3);
    bottomSplitter->setStretchFactor(1, 2);
    contentLayout->addWidget(bottomSplitter);

    lbHealthLedger_ = new QLabel(pageRuntimeChain_);
    lbHealthLedger_->setWordWrap(true);
    lbHealthLedger_->setStyleSheet(QStringLiteral("QLabel{color:#475569;padding:4px;}"));
    root->addWidget(lbHealthLedger_);

    auto* refreshButton = new QPushButton(QString::fromUtf8("刷新"), pageRuntimeChain_);
    connect(refreshButton, &QPushButton::clicked, this, &EnvPredictorUI::refreshRuntimeChainPage_);
    root->addWidget(refreshButton, 0, Qt::AlignRight);

    ui.tabWidget_main->insertTab(1, pageRuntimeChain_, QString::fromUtf8("对象信息"));
    refreshRuntimeChainPage_();
}

void EnvPredictorUI::refreshRuntimeChainPage_()
{
    if (!pageRuntimeChain_) {
        return;
    }

    clearGraphTable(tblObjectBasic_);
    clearGraphTable(tblObjectMeshes_);
    clearGraphTable(tblObjectSensors_);
    clearGraphTable(tblRuntimeChain_);
    clearGraphTable(tblObjectRuntimeConfig_);

    const QJsonObject objectManifest = readJsonObject(objectPackagePath(QStringLiteral("object/twin_object.json")));
    const QJsonObject sensorLayoutsDoc = readJsonObject(objectPackagePath(QStringLiteral("assets/sensor_layouts.json")));
    const QJsonObject runProfile = readJsonObject(objectPackagePath(QStringLiteral("run_profiles/online_filtering_full.json")));
    const QJsonObject onlineWorkflow = readJsonObject(objectPackagePath(QStringLiteral("workflows/online_filtering_external_input.json")));

    const QJsonArray resources = objectManifest.value(QStringLiteral("resources")).toArray();
    auto resourceById = [&](const QString& resourceId) -> QJsonObject {
        for (const QJsonValue& value : resources) {
            const QJsonObject resource = value.toObject();
            if (resource.value(QStringLiteral("resource_id")).toString() == resourceId) {
                return resource;
            }
        }
        return {};
    };

    int meshCount = 0;
    int sensorGroupCount = 0;
    int declaredSensorPoints = 0;
    for (const QJsonValue& value : resources) {
        const QJsonObject resource = value.toObject();
        const QString type = resource.value(QStringLiteral("resource_type")).toString();
        if (type == QStringLiteral("mesh")) {
            ++meshCount;
        }
        if (type == QStringLiteral("sensor_layout")) {
            ++sensorGroupCount;
            declaredSensorPoints += resource.value(QStringLiteral("node_count")).toInt();
        }
    }

    QMap<QString, int> streamChannelCounts;
    const auto workspaceRoot = workspaceRootFromEnvironment();
    const auto configuredObjectRoot = environmentPath("FLIGHTENV_PLATFORM_OBJECT_ROOT").empty()
        ? (workspaceRoot / "flightenv-object-reentry-vehicle")
        : environmentPath("FLIGHTENV_PLATFORM_OBJECT_ROOT");
    const PlatformUiRunConfig uiRunConfig = loadPlatformUiRunConfig(workspaceRoot, configuredObjectRoot);
    const QString sensorStreamPath = environmentPath("FLIGHTENV_PLATFORM_EXTERNAL_OBSERVATION_STREAM").empty()
        ? platformPathText(uiRunConfig.external_observation_stream)
        : platformPathText(environmentPath("FLIGHTENV_PLATFORM_EXTERNAL_OBSERVATION_STREAM"));
    const QJsonObject streamDoc = readJsonObject(sensorStreamPath);
    const QJsonArray frames = streamDoc.value(QStringLiteral("frames")).toArray();
    if (!frames.isEmpty()) {
        const QJsonObject firstFrame = frames.first().toObject();
        QJsonArray sensorIds = firstFrame.value(QStringLiteral("sensor_ids")).toArray();
        if (sensorIds.isEmpty()) {
            sensorIds = firstFrame.value(QStringLiteral("frame")).toObject()
                .value(QStringLiteral("frame")).toObject()
                .value(QStringLiteral("sensor_ids")).toArray();
        }
        for (const QJsonValue& idValue : sensorIds) {
            const QString id = idValue.toString();
            const int suffix = id.lastIndexOf(QLatin1Char('_'));
            const QString prefix = suffix > 0 ? id.left(suffix) : id;
            streamChannelCounts[prefix] += 1;
        }
    }
    int channelCount = 0;
    for (auto it = streamChannelCounts.cbegin(); it != streamChannelCounts.cend(); ++it) {
        channelCount += it.value();
    }

    const QString objectName = objectManifest.value(QStringLiteral("display_name")).toString(QStringLiteral("再入飞行器"));
    const QString objectId = objectManifest.value(QStringLiteral("object_id")).toString(QStringLiteral("reentry_vehicle"));
    const QJsonArray components = objectManifest.value(QStringLiteral("components")).toArray();
    const QJsonArray workflows = objectManifest.value(QStringLiteral("workflows")).toArray();

    if (lbObjectSummary_) {
        lbObjectSummary_->setText(QStringLiteral(
            "对象：%1（%2）；部件=%3，网格=%4，传感器组=%5，传感器物理点=%6，当前数据通道=%7。对象已默认加载，无需选择。")
            .arg(objectName, objectId)
            .arg(components.size())
            .arg(meshCount)
            .arg(sensorGroupCount)
            .arg(declaredSensorPoints)
            .arg(channelCount));
    }
    if (lbObjectFlow_) {
        lbObjectFlow_->setText(QStringLiteral(
            "运行流程：弹道/传感器回放 -> 状态转移 -> 观测方程 -> 粒子滤波融合 -> 多场重构 -> 损伤/烧蚀累积 -> 失效与寿命评估 -> 按帧触发未来预测分支。"));
    }

    appendGraphRow(tblObjectBasic_, {QStringLiteral("对象"), objectName, QStringLiteral("object/twin_object.json")});
    appendGraphRow(tblObjectBasic_, {QStringLiteral("对象ID"), objectId, QStringLiteral("object_id")});
    appendGraphRow(tblObjectBasic_, {QStringLiteral("对象类型"), objectManifest.value(QStringLiteral("object_type")).toString(), QStringLiteral("object_type")});
    appendGraphRow(tblObjectBasic_, {QStringLiteral("部件数量"), QString::number(components.size()), QStringLiteral("components")});
    appendGraphRow(tblObjectBasic_, {QStringLiteral("工作流数量"), QString::number(workflows.size()), QStringLiteral("workflows")});
    appendGraphRow(tblObjectBasic_, {QStringLiteral("默认在线流程"), runProfile.value(QStringLiteral("workflow_id")).toString(), QStringLiteral("online_filtering_full")});

    for (const QJsonValue& value : resources) {
        const QJsonObject resource = value.toObject();
        if (resource.value(QStringLiteral("resource_type")).toString() != QStringLiteral("mesh")) {
            continue;
        }
        appendGraphRow(tblObjectMeshes_, {
            resource.value(QStringLiteral("display_name")).toString(resource.value(QStringLiteral("resource_id")).toString()),
            resource.value(QStringLiteral("component_id")).toString(QStringLiteral("-")),
            objectResourcePath(resource.value(QStringLiteral("path")).toString()),
            resource.value(QStringLiteral("layout_role")).toString(resource.value(QStringLiteral("resource_id")).toString())
        });
    }

    const QJsonArray layoutEntries = sensorLayoutsDoc.value(QStringLiteral("layouts")).toArray();
    auto layoutById = [&](const QString& resourceId) -> QJsonObject {
        for (const QJsonValue& value : layoutEntries) {
            const QJsonObject layout = value.toObject();
            if (layout.value(QStringLiteral("resource_id")).toString() == resourceId) {
                return layout;
            }
        }
        return {};
    };

    for (const QJsonValue& value : resources) {
        const QJsonObject resource = value.toObject();
        if (resource.value(QStringLiteral("resource_type")).toString() != QStringLiteral("sensor_layout")) {
            continue;
        }
        const QString resourceId = resource.value(QStringLiteral("resource_id")).toString();
        const QJsonObject layout = layoutById(resourceId);
        const QString prefix = resource.value(QStringLiteral("channel_prefix")).toString(
            layout.value(QStringLiteral("channel_prefix")).toString());
        int channels = streamChannelCounts.value(prefix, 0);
        if (channels == 0 && resourceId == QStringLiteral("sensor.ballistic.virtual")) {
            channels = streamChannelCounts.value(QStringLiteral("ballistic_actual"), 5);
        }
        if (channels == 0) {
            channels = resource.value(QStringLiteral("node_count")).toInt() *
                std::max(1, resource.value(QStringLiteral("value_dim")).toInt(1));
        }
        appendGraphRow(tblObjectSensors_, {
            resource.value(QStringLiteral("display_name")).toString(resourceId),
            resource.value(QStringLiteral("component_id")).toString(QStringLiteral("虚拟/对象级")),
            jsonNumberText(resource, QStringLiteral("node_count"),
                jsonNumberText(layout, QStringLiteral("node_count"), QStringLiteral("-"))),
            QString::number(channels),
            layout.value(QStringLiteral("unit")).toString(QStringLiteral("-")),
            prefix.isEmpty() ? QStringLiteral("-") : prefix
        });
    }

    const QJsonObject trajectoryDataset = resourceById(QStringLiteral("dataset.trajectory.replay.primary"));
    const QString trajectoryPath = objectResourcePath(trajectoryDataset.value(QStringLiteral("uri")).toString());
    const QString databasePath = objectResourcePath(trajectoryDataset.value(QStringLiteral("source_database_path")).toString());

    QString progressPath;
    if (!platform_snapshot_.run_dir.empty()) {
        progressPath = platformPathText(platform_snapshot_.run_dir / "mainline_progress.json");
    }
    if (progressPath.isEmpty() || !QFileInfo::exists(progressPath)) {
        progressPath = findNewestSummary(QStringLiteral("_local_artifacts/platform-runtime/mainline-runs"),
            QStringLiteral("mainline_progress.json"));
    }
    const QJsonObject progress = progressPath.isEmpty() ? QJsonObject{} : readJsonObject(progressPath);
    const QJsonObject clock = progress.value(QStringLiteral("clock")).toObject();

    if (objectOnlineFramesSpin_) {
        QSignalBlocker b0(objectOnlineFramesSpin_);
        QSignalBlocker b1(objectPredictionEveryFramesSpin_);
        QSignalBlocker b2(objectFutureMaxIterationsSpin_);
        QSignalBlocker b3(objectBranchChunkIterationsSpin_);
        QSignalBlocker b4(objectMaxConcurrentBranchesSpin_);
        QSignalBlocker b5(objectReplayTimeScaleSpin_);
        objectOnlineFramesSpin_->setValue(uiRunConfig.online_frames);
        objectPredictionEveryFramesSpin_->setValue(uiRunConfig.prediction_every_frames);
        objectFutureMaxIterationsSpin_->setValue(uiRunConfig.future_max_iterations);
        objectBranchChunkIterationsSpin_->setValue(uiRunConfig.branch_chunk_iterations);
        objectMaxConcurrentBranchesSpin_->setValue(uiRunConfig.max_concurrent_branches);
        objectReplayTimeScaleSpin_->setValue(uiRunConfig.replay_time_scale);
        if (objectSensorStreamEdit_) {
            objectSensorStreamEdit_->setText(QDir::toNativeSeparators(sensorStreamPath));
        }
    }

    appendGraphRow(tblObjectRuntimeConfig_, {
        QStringLiteral("平台时间步长"),
        clock.contains(QStringLiteral("delta_t_s"))
            ? QStringLiteral("%1 s").arg(jsonNumberText(clock, QStringLiteral("delta_t_s")))
            : QStringLiteral("由输入回放数据决定"),
        progressPath.isEmpty() ? QStringLiteral("对象默认") : progressPath
    });
    appendGraphRow(tblObjectRuntimeConfig_, {
        QStringLiteral("在线回放帧数"),
        QString::number(uiRunConfig.online_frames),
        QStringLiteral("_local_artifacts/platform-ui/env_platform_controller_run_config.json")
    });
    appendGraphRow(tblObjectRuntimeConfig_, {
        QStringLiteral("预测分支触发"),
        QStringLiteral("每 %1 帧开启一个未来预测分支").arg(uiRunConfig.prediction_every_frames),
        QStringLiteral("runtime host 参数：--prediction-every-frames")
    });
    appendGraphRow(tblObjectRuntimeConfig_, {
        QStringLiteral("未来预测长度"),
        QStringLiteral("%1 步").arg(uiRunConfig.future_max_iterations),
        QStringLiteral("runtime host 参数：--future-max-iterations")
    });
    appendGraphRow(tblObjectRuntimeConfig_, {
        QStringLiteral("最大并发分支"),
        QString::number(uiRunConfig.max_concurrent_branches),
        QStringLiteral("runtime host 参数：--max-concurrent-branches")
    });
    appendGraphRow(tblObjectRuntimeConfig_, {
        QStringLiteral("回放速度倍率"),
        QString::number(uiRunConfig.replay_time_scale, 'f', 2),
        QStringLiteral("runtime host 参数：--replay-time-scale")
    });
    appendGraphRow(tblObjectRuntimeConfig_, {
        QStringLiteral("传感器/弹道输入流"),
        sensorStreamPath,
        QStringLiteral("runtime_launch.external_observation_stream")
    });
    appendGraphRow(tblObjectRuntimeConfig_, {
        QStringLiteral("弹道回放文件"),
        trajectoryPath,
        QStringLiteral("dataset.trajectory.replay.primary")
    });
    appendGraphRow(tblObjectRuntimeConfig_, {
        QStringLiteral("原始数据库"),
        databasePath,
        QStringLiteral("source_database_path")
    });

    const auto appendOperatorNode = [&](const QString& stageLabel,
                                        const QJsonObject& stage,
                                        const QJsonObject& node) {
        if (!tblRuntimeChain_) {
            return;
        }
        const int row = tblRuntimeChain_->rowCount();
        tblRuntimeChain_->insertRow(row);
        const QString nodeId = node.value(QStringLiteral("node_id")).toString();
        const QString operatorRef = node.value(QStringLiteral("operator_ref")).toString();
        const QJsonObject activation = node.value(QStringLiteral("activation_policy")).toObject();
        const bool enabled = activation.value(QStringLiteral("enabled")).toBool(true);
        const bool required = activation.value(QStringLiteral("required")).toBool(false);
        const bool disabledByConfig = containsString(uiRunConfig.disabled_operator_refs, operatorRef);
        setTableText(tblRuntimeChain_, row, 0, stageLabel);
        auto* check = new QCheckBox(tblRuntimeChain_);
        check->setProperty("platform_operator_ref", operatorRef);
        check->setProperty("platform_node_id", nodeId);
        check->setProperty("platform_feature", activation.value(QStringLiteral("feature")).toString());
        check->setProperty("platform_required", required);
        check->setChecked(enabled && !disabledByConfig);
        check->setEnabled(!required);
        check->setToolTip(required
            ? QStringLiteral("必选算子，当前对象运行必须启用")
            : QStringLiteral("对象方案选择：取消勾选后，下一次训练/开始会重新编译 workflow 并移除此算子"));
        tblRuntimeChain_->setCellWidget(row, 1, check);
        connect(check, &QCheckBox::toggled, this, [this]() {
            platformRunConfigApplyOk_ = false;
            if (objectRunConfigHint_) {
                objectRunConfigHint_->setText(QStringLiteral("算子选择已修改；点训练/开始前会自动应用，也可以先点应用运行设置。"));
            }
        });
        setTableText(tblRuntimeChain_, row, 2, operatorDisplayName(operatorRef, nodeId));

        QStringList outputs;
        const QJsonArray stageOutputs = stage.value(QStringLiteral("subgraph")).toObject()
            .value(QStringLiteral("stage_outputs")).toArray();
        const QString prefix = nodeId + QStringLiteral(".");
        for (const QJsonValue& outputValue : stageOutputs) {
            const QString output = outputValue.toString();
            if (output.startsWith(prefix)) {
                outputs << output.mid(prefix.size());
            }
        }
        setTableText(tblRuntimeChain_, row, 3, outputs.isEmpty() ? QStringLiteral("-") : outputs.join(QStringLiteral(", ")));
        setTableText(tblRuntimeChain_, row, 4,
            activation.value(QStringLiteral("feature")).toString().isEmpty()
                ? operatorRef
                : QStringLiteral("功能开关：%1；算子ID：%2")
                    .arg(activation.value(QStringLiteral("feature")).toString(), operatorRef));
    };

    const QJsonArray phases = onlineWorkflow.value(QStringLiteral("phases")).toArray();
    for (const QJsonValue& phaseValue : phases) {
        const QJsonObject phase = phaseValue.toObject();
        const QJsonArray stages = phase.value(QStringLiteral("stages")).toArray();
        for (const QJsonValue& stageValue : stages) {
            const QJsonObject stage = stageValue.toObject();
            const QString stageLabel = workflowStageLabel(stage.value(QStringLiteral("stage_id")).toString());
            const QJsonArray nodes = stage.value(QStringLiteral("subgraph")).toObject()
                .value(QStringLiteral("nodes")).toArray();
            for (const QJsonValue& nodeValue : nodes) {
                appendOperatorNode(stageLabel, stage, nodeValue.toObject());
            }
        }
    }

    if (objectMeshCombo_) {
        bool previousMeshOk = false;
        const int previousMesh = objectMeshCombo_->currentData().toInt(&previousMeshOk);
        const QSignalBlocker comboBlocker(objectMeshCombo_);
        objectMeshCombo_->clear();
        if (runtime_view_ && !runtime_view_->meshes.empty()) {
            for (std::size_t i = 0; i < runtime_view_->meshes.size(); ++i) {
                const auto& mesh = runtime_view_->meshes[i];
                QString name = QString::fromStdString(mesh.name);
                if (name.isEmpty()) {
                    name = QFileInfo(QString::fromStdString(mesh.path)).baseName();
                }
                objectMeshCombo_->addItem(
                    QStringLiteral("%1  (%2)").arg(name, QString::fromStdString(mesh.path)),
                    static_cast<int>(i));
            }
            const int oldIndex = previousMeshOk ? objectMeshCombo_->findData(previousMesh) : -1;
            objectMeshCombo_->setCurrentIndex(oldIndex >= 0 ? oldIndex : 0);
            objectMeshCombo_->setEnabled(true);
        } else {
            objectMeshCombo_->addItem(QStringLiteral("运行初始化后可选择网格"), -1);
            objectMeshCombo_->setEnabled(false);
        }
    }

    if (objectSensorLayoutCombo_) {
        const QString previousSensorLayout = objectSensorLayoutCombo_->currentData().toString();
        const QSignalBlocker comboBlocker(objectSensorLayoutCombo_);
        objectSensorLayoutCombo_->clear();
        loadPlatformSensorLayouts();
        if (!platformSensorLayouts_.empty()) {
            for (const auto& layout : platformSensorLayouts_) {
                QString name = layout.display_name.isEmpty() ? layout.resource_id : layout.display_name;
                if (name.isEmpty()) {
                    name = QStringLiteral("未命名传感器组");
                }
                objectSensorLayoutCombo_->addItem(
                    QStringLiteral("%1  (%2点)").arg(name).arg(static_cast<int>(layout.nodes.size())),
                    layout.resource_id);
            }
            const int oldIndex = previousSensorLayout.isEmpty()
                ? -1
                : objectSensorLayoutCombo_->findData(previousSensorLayout);
            objectSensorLayoutCombo_->setCurrentIndex(oldIndex >= 0 ? oldIndex : 0);
            objectSensorLayoutCombo_->setEnabled(true);
        } else {
            objectSensorLayoutCombo_->addItem(QStringLiteral("未读取到传感器组"), QString());
            objectSensorLayoutCombo_->setEnabled(false);
        }
    }
    rebuildObjectSensorView_();

    for (QTableWidget* table : {tblObjectBasic_, tblObjectMeshes_, tblObjectSensors_, tblRuntimeChain_, tblObjectRuntimeConfig_}) {
        if (table) {
            table->resizeColumnsToContents();
        }
    }

    if (lbHealthLedger_) {
        lbHealthLedger_->setText(QStringLiteral(
            "说明：本页面向对象验收展示，隐藏 typed-buffer/runtime URI 等调试细节；取消勾选非必选算子后，下一次训练/开始会重新编译 workflow 并按选择运行；必选算子不可关闭。"));
    }
}

void EnvPredictorUI::applyPlatformRunConfigFromUi_()
{
#ifdef FLIGHTENV_USE_PLATFORM_BACKEND
    platformRunConfigApplyOk_ = false;
    if (!objectOnlineFramesSpin_ || !objectPredictionEveryFramesSpin_ ||
        !objectFutureMaxIterationsSpin_ || !objectBranchChunkIterationsSpin_ ||
        !objectMaxConcurrentBranchesSpin_ || !objectReplayTimeScaleSpin_) {
        platformRunConfigApplyOk_ = true;
        return;
    }

    const auto workspaceRoot = workspaceRootFromEnvironment();
    const auto configPath = platformUiRunConfigPath(workspaceRoot);
    QDir().mkpath(QFileInfo(QString::fromStdString(configPath.string())).absolutePath());

    QJsonObject config;
    config.insert(QStringLiteral("schema_version"), QStringLiteral("flightenv.platform_ui.run_config.v1"));
    config.insert(QStringLiteral("online_frames"), objectOnlineFramesSpin_->value());
    config.insert(QStringLiteral("prediction_every_frames"), objectPredictionEveryFramesSpin_->value());
    config.insert(QStringLiteral("future_max_iterations"), objectFutureMaxIterationsSpin_->value());
    config.insert(QStringLiteral("branch_chunk_iterations"), objectBranchChunkIterationsSpin_->value());
    config.insert(QStringLiteral("max_concurrent_branches"), objectMaxConcurrentBranchesSpin_->value());
    config.insert(QStringLiteral("replay_time_scale"), objectReplayTimeScaleSpin_->value());
    if (objectSensorStreamEdit_) {
        config.insert(
            QStringLiteral("external_observation_stream"),
            QDir::fromNativeSeparators(objectSensorStreamEdit_->text().trimmed()));
    }
    QJsonArray disabledOperators;
    std::set<QString> seenDisabledOperators;
    if (tblRuntimeChain_) {
        for (int row = 0; row < tblRuntimeChain_->rowCount(); ++row) {
            auto* check = qobject_cast<QCheckBox*>(tblRuntimeChain_->cellWidget(row, 1));
            if (!check || check->property("platform_required").toBool()) {
                continue;
            }
            const QString operatorRef = check->property("platform_operator_ref").toString().trimmed();
            if (!operatorRef.isEmpty() && !check->isChecked() && seenDisabledOperators.insert(operatorRef).second) {
                disabledOperators.push_back(operatorRef);
            }
        }
    }
    config.insert(QStringLiteral("disabled_operator_refs"), disabledOperators);

    QFile file(QString::fromStdString(configPath.string()));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (objectRunConfigHint_) {
            objectRunConfigHint_->setText(QStringLiteral("运行配置保存失败：%1").arg(file.errorString()));
        }
        return;
    }
    file.write(QJsonDocument(config).toJson(QJsonDocument::Indented));
    file.close();

    try {
        if (auto platformBackend = std::dynamic_pointer_cast<launchsupport::PlatformControllerBackend>(controller_backend_)) {
            platformBackend->configure(platformBackendOptions(true));
        }
    } catch (const std::exception& e) {
        if (objectRunConfigHint_) {
            objectRunConfigHint_->setText(QStringLiteral("运行配置编译失败：%1").arg(QString::fromUtf8(e.what())));
        }
        RCLCPP_ERROR(envUiLogger(), "Platform UI run config compile failed: %s", e.what());
        return;
    }

    if (objectRunConfigHint_) {
        objectRunConfigHint_->setText(QStringLiteral(
            "已应用到下一次训练/开始：在线%1帧，每%2帧开分支，未来%3步，并发%4，回放倍率%5，禁用算子%6个。")
            .arg(objectOnlineFramesSpin_->value())
            .arg(objectPredictionEveryFramesSpin_->value())
            .arg(objectFutureMaxIterationsSpin_->value())
            .arg(objectMaxConcurrentBranchesSpin_->value())
            .arg(objectReplayTimeScaleSpin_->value(), 0, 'f', 2)
            .arg(disabledOperators.size()));
    }
    platformRunConfigApplyOk_ = true;
#endif
}

void EnvPredictorUI::rebuildObjectSensorView_()
{
#ifdef FLIGHTENV_USE_PLATFORM_BACKEND
    if (!objectSensorVtkHost_) {
        return;
    }

    auto* layout = objectSensorVtkHost_->layout();
    if (!layout) {
        layout = new QVBoxLayout(objectSensorVtkHost_);
        layout->setContentsMargins(0, 0, 0, 0);
        objectSensorVtkHost_->setLayout(layout);
    }

    auto clearHost = [&]() {
        QLayoutItem* item = nullptr;
        while ((item = layout->takeAt(0)) != nullptr) {
            if (item->widget()) {
                item->widget()->deleteLater();
            }
            delete item;
        }
        objectSensorVtk_ = nullptr;
    };

    if (!runtime_view_ || runtime_view_->meshes.empty()) {
        return;
    }

    loadPlatformSensorLayouts();
    int selectedLayoutIndex = 0;
    if (objectSensorLayoutCombo_ && objectSensorLayoutCombo_->currentIndex() >= 0) {
        const QString selectedResourceId = objectSensorLayoutCombo_->currentData().toString();
        const auto it = std::find_if(
            platformSensorLayouts_.begin(),
            platformSensorLayouts_.end(),
            [&](const PlatformSensorLayoutView& layoutView) {
                return layoutView.resource_id == selectedResourceId;
            });
        if (it != platformSensorLayouts_.end()) {
            selectedLayoutIndex = static_cast<int>(std::distance(platformSensorLayouts_.begin(), it));
        }
    }
    if (selectedLayoutIndex < 0 || selectedLayoutIndex >= static_cast<int>(platformSensorLayouts_.size())) {
        selectedLayoutIndex = 0;
    }

    clearHost();
    if (platformSensorLayouts_.empty() || platformSensorLayouts_[selectedLayoutIndex].nodes.empty()) {
        auto* emptyLabel = new QLabel(QStringLiteral("对象包中未读取到传感器三维坐标"), objectSensorVtkHost_);
        emptyLabel->setAlignment(Qt::AlignCenter);
        emptyLabel->setStyleSheet(QStringLiteral("QLabel{color:#64748b;border:1px dashed #cbd5e1;}"));
        layout->addWidget(emptyLabel);
        return;
    }

    int meshIndex = 0;
    if (objectMeshCombo_ && objectMeshCombo_->currentData().isValid()) {
        bool meshOk = false;
        const int selectedMesh = objectMeshCombo_->currentData().toInt(&meshOk);
        if (meshOk && selectedMesh >= 0 && selectedMesh < static_cast<int>(runtime_view_->meshes.size())) {
            meshIndex = selectedMesh;
        }
    }
    const auto& mesh = runtime_view_->meshes[meshIndex];
    const auto& sensorPoints = platformSensorLayouts_[selectedLayoutIndex].nodes;

    int fieldIndex = 2;
    if (fieldIndex < 0 || static_cast<std::size_t>(fieldIndex) >= runtime_view_->fields.size()) {
        fieldIndex = 0;
        if (!mesh.subject_indices.empty()) {
            fieldIndex = mesh.subject_indices.front();
        }
    }
    if (fieldIndex < 0 || static_cast<std::size_t>(fieldIndex) >= runtime_view_->fields.size()) {
        fieldIndex = 0;
    }

    QString meshName = QString::fromStdString(mesh.name);
    if (meshName.isEmpty()) {
        meshName = QFileInfo(QString::fromStdString(mesh.path)).baseName();
    }

    objectSensorVtk_ = new VTKSingleDialog(
        meshName.toStdString(),
        std::string(MODELSTLDIR) + mesh.path,
        objectSensorVtkHost_);
    objectSensorVtk_->setRuntimeView(runtime_view_);
    objectSensorVtk_->flg = fieldIndex;
    objectSensorVtk_->countFlg = 0;
    objectSensorVtk_->isModelLoaded = objectSensorVtk_->loadModelData();
    objectSensorVtk_->RemoveActor();
    objectSensorVtk_->setMarkerPoints(sensorPoints);
    layout->addWidget(objectSensorVtk_);
    vtkSizeTimer->start(500);
#endif
}

void EnvPredictorUI::buildGraphWorkflowTabs_(QVBoxLayout* root)
{
    lbGraphWorkflowSummary_ = new QLabel(QString::fromUtf8("完整链路：等待 GraphRuntime evidence。"), pageGraphRuntime_);
    lbGraphWorkflowSummary_->setWordWrap(true);
    lbGraphWorkflowSummary_->setStyleSheet(QStringLiteral("QLabel{padding:6px;border:1px solid #d1d5db;border-radius:4px;background:#ffffff;}"));
    root->addWidget(lbGraphWorkflowSummary_);

    graphWorkflowTabs_ = new QTabWidget(pageGraphRuntime_);
    graphWorkflowTabs_->setMinimumHeight(520);

    auto addTablePage = [&](const QString& title, QLabel** summary, QTableWidget** table, const QStringList& headers) {
        auto* page = new QWidget(graphWorkflowTabs_);
        auto* layout = new QVBoxLayout(page);
        layout->setContentsMargins(6, 6, 6, 6);
        layout->setSpacing(6);
        if (summary) {
            *summary = new QLabel(QString::fromUtf8("等待数据"), page);
            (*summary)->setWordWrap(true);
            (*summary)->setStyleSheet(QStringLiteral("QLabel{padding:5px;background:#f8fafc;border:1px solid #e5e7eb;border-radius:4px;}"));
            layout->addWidget(*summary);
        }
        *table = makeGraphTable(page, headers);
        layout->addWidget(*table, 1);
        graphWorkflowTabs_->addTab(page, title);
    };

    {
        auto* page = new QWidget(graphWorkflowTabs_);
        auto* layout = new QVBoxLayout(page);
        layout->setContentsMargins(6, 6, 6, 6);
        layout->setSpacing(6);
        lbGraphObjectSummary_ = new QLabel(QString::fromUtf8("对象画像：等待 catalog 和 runtime evidence。"), page);
        lbGraphObjectSummary_->setWordWrap(true);
        lbGraphObjectSummary_->setStyleSheet(QStringLiteral("QLabel{padding:5px;background:#f8fafc;border:1px solid #e5e7eb;border-radius:4px;}"));
        layout->addWidget(lbGraphObjectSummary_);

        auto* vertical = new QSplitter(Qt::Vertical, page);
        auto* top = new QSplitter(Qt::Horizontal, vertical);
        graphObjectTable_ = makeGraphTable(top, {
            QString::fromUtf8("对象/部位"),
            QString::fromUtf8("几何/数据资产"),
            QString::fromUtf8("物理场/传感器"),
            QString::fromUtf8("模型匹配"),
            QString::fromUtf8("说明")
        });
        graphBindingTable_ = makeGraphTable(top, {
            QString::fromUtf8("对象/绑定"),
            QString::fromUtf8("模型类型/领域"),
            QString::fromUtf8("模型 ID"),
            QString::fromUtf8("阶段"),
            QString::fromUtf8("优先级/启用"),
            QString::fromUtf8("配置")
        });
        top->addWidget(graphObjectTable_);
        top->addWidget(graphBindingTable_);
        top->setStretchFactor(0, 2);
        top->setStretchFactor(1, 2);

        auto* bottom = new QSplitter(Qt::Horizontal, vertical);
        graphEquationTable_ = makeGraphTable(bottom, {
            QString::fromUtf8("图/方程组"),
            QString::fromUtf8("算子/角色"),
            QString::fromUtf8("输入"),
            QString::fromUtf8("输出"),
            QString::fromUtf8("关系说明")
        });
        graphCatalogModelTable_ = makeGraphTable(bottom, {
            QString::fromUtf8("模型 ID"),
            QString::fromUtf8("类型/领域"),
            QString::fromUtf8("运行时/版本"),
            QString::fromUtf8("资产"),
            QString::fromUtf8("适用包络/状态")
        });
        bottom->addWidget(graphEquationTable_);
        bottom->addWidget(graphCatalogModelTable_);
        bottom->setStretchFactor(0, 2);
        bottom->setStretchFactor(1, 2);

        vertical->addWidget(top);
        vertical->addWidget(bottom);
        vertical->setStretchFactor(0, 1);
        vertical->setStretchFactor(1, 1);
        layout->addWidget(vertical, 1);
        graphWorkflowTabs_->addTab(page, QString::fromUtf8("对象/模型/方程"));
    }

    {
        auto* page = new QWidget(graphWorkflowTabs_);
        auto* layout = new QVBoxLayout(page);
        layout->setContentsMargins(6, 6, 6, 6);
        layout->setSpacing(6);
        graphWorkflowPathWidget_ = new flightenv::ui::display::WorkflowPathWidget(page);
        layout->addWidget(graphWorkflowPathWidget_);
        lbGraphThroughputSummary_ = new QLabel(QString::fromUtf8("实时吞吐：等待 operator_live_status 和 result ports。"), page);
        lbGraphThroughputSummary_->setWordWrap(true);
        lbGraphThroughputSummary_->setStyleSheet(QStringLiteral("QLabel{padding:5px;background:#f8fafc;border:1px solid #e5e7eb;border-radius:4px;}"));
        layout->addWidget(lbGraphThroughputSummary_);
        auto* splitter = new QSplitter(Qt::Horizontal, page);
        graphPathTable_ = makeGraphTable(splitter, {
            QString::fromUtf8("序号"), QString::fromUtf8("节点"), QString::fromUtf8("算子类型"),
            QString::fromUtf8("输入端口"), QString::fromUtf8("输出端口"), QString::fromUtf8("执行形态")
        });
        graphThroughputTable_ = makeGraphTable(splitter, {
            QString::fromUtf8("节点"), QString::fromUtf8("状态"), QString::fromUtf8("耗时/ms"),
            QString::fromUtf8("输出大小"), QString::fromUtf8("吞吐"), QString::fromUtf8("模型/资源")
        });
        splitter->addWidget(graphPathTable_);
        splitter->addWidget(graphThroughputTable_);
        splitter->setStretchFactor(0, 1);
        splitter->setStretchFactor(1, 1);
        layout->addWidget(splitter, 1);
        graphWorkflowTabs_->addTab(page, QString::fromUtf8("编排/吞吐"));
    }

    addTablePage(QString::fromUtf8("资产/训练"), nullptr, &graphAssetTable_,
        {QString::fromUtf8("类别"), QString::fromUtf8("ID"), QString::fromUtf8("位置/状态"), QString::fromUtf8("备注")});
    addTablePage(QString::fromUtf8("PF融合"), nullptr, &graphFusionTable_,
        {QString::fromUtf8("算子"), QString::fromUtf8("模型状态"), QString::fromUtf8("PF/运行配置"), QString::fromUtf8("场布局")});

    {
        auto* page = new QWidget(graphWorkflowTabs_);
        auto* layout = new QVBoxLayout(page);
        layout->setContentsMargins(6, 6, 6, 6);
        layout->setSpacing(6);
        lbGraphTrajectorySummary_ = new QLabel(QString::fromUtf8("等待弹道输出"), page);
        lbGraphTrajectorySummary_->setWordWrap(true);
        lbGraphTrajectorySummary_->setStyleSheet(QStringLiteral("QLabel{padding:5px;background:#f8fafc;border:1px solid #e5e7eb;border-radius:4px;}"));
        layout->addWidget(lbGraphTrajectorySummary_);
        graphTrajectoryPlot_ = new flightenv::ui::display::TrajectoryPathWidget(page);
        layout->addWidget(graphTrajectoryPlot_, 1);
        graphTrajectoryTable_ = makeGraphTable(page,
            {QString::fromUtf8("t/s"), QString::fromUtf8("高度/m"), QString::fromUtf8("Mach"), QString::fromUtf8("动压/Pa"), QString::fromUtf8("攻角/rad"), QString::fromUtf8("法向过载/g")});
        graphTrajectoryTable_->setMaximumHeight(180);
        layout->addWidget(graphTrajectoryTable_);
        graphWorkflowTabs_->addTab(page, QString::fromUtf8("弹道"));
    }

    {
        auto* page = new QWidget(graphWorkflowTabs_);
        auto* layout = new QVBoxLayout(page);
        layout->setContentsMargins(6, 6, 6, 6);
        layout->setSpacing(6);
        lbGraphFieldSummary_ = new QLabel(QString::fromUtf8("等待多物理场输出"), page);
        lbGraphFieldSummary_->setWordWrap(true);
        lbGraphFieldSummary_->setStyleSheet(QStringLiteral("QLabel{padding:5px;background:#f8fafc;border:1px solid #e5e7eb;border-radius:4px;}"));
        layout->addWidget(lbGraphFieldSummary_);
        auto* controls = new QHBoxLayout();
        controls->addWidget(new QLabel(QString::fromUtf8("云图 subject"), page));
        graphFieldSubjectCombo_ = makeSubjectCombo(page, QString::fromUtf8("选择预测场 subject，云图显示该 subject 最新 forecast step。"));
        graphFieldSubjectCombo_->setCurrentIndex(3);
        controls->addWidget(graphFieldSubjectCombo_);
        controls->addStretch();
        layout->addLayout(controls);
        graphMultiFieldScroll_ = new QScrollArea(page);
        graphMultiFieldScroll_->setWidgetResizable(true);
        graphMultiFieldContainer_ = new QWidget(graphMultiFieldScroll_);
        graphMultiFieldGrid_ = new QGridLayout(graphMultiFieldContainer_);
        graphMultiFieldGrid_->setContentsMargins(0, 0, 0, 0);
        graphMultiFieldGrid_->setSpacing(10);
        graphMultiFieldContainer_->setLayout(graphMultiFieldGrid_);
        graphMultiFieldScroll_->setWidget(graphMultiFieldContainer_);
        layout->addWidget(graphMultiFieldScroll_, 1);
        graphFieldVtk_ = new flightenv::ui::demo::VtkModelFieldWidget(page);
        graphFieldVtk_->hide();
        graphFieldTable_ = makeGraphTable(page,
            {QString::fromUtf8("subject"), QString::fromUtf8("field"), QString::fromUtf8("步数"), QString::fromUtf8("节点/值数"), QString::fromUtf8("首步统计"), QString::fromUtf8("末步统计")});
        graphFieldTable_->setMaximumHeight(170);
        layout->addWidget(graphFieldTable_);
        graphWorkflowTabs_->addTab(page, QString::fromUtf8("多物理场"));
    }

    {
        auto* page = new QWidget(graphWorkflowTabs_);
        auto* layout = new QVBoxLayout(page);
        layout->setContentsMargins(6, 6, 6, 6);
        layout->setSpacing(6);
        lbGraphDamageSummary_ = new QLabel(QString::fromUtf8("等待损伤输出"), page);
        lbGraphDamageSummary_->setWordWrap(true);
        lbGraphDamageSummary_->setStyleSheet(QStringLiteral("QLabel{padding:5px;background:#f8fafc;border:1px solid #e5e7eb;border-radius:4px;}"));
        layout->addWidget(lbGraphDamageSummary_);
        auto* controls = new QHBoxLayout();
        controls->addWidget(new QLabel(QString::fromUtf8("云图 subject"), page));
        graphDamageSubjectCombo_ = makeSubjectCombo(page, QString::fromUtf8("选择累计损伤场 subject，云图显示最新 forecast step。"));
        graphDamageSubjectCombo_->setCurrentIndex(3);
        controls->addWidget(graphDamageSubjectCombo_);
        controls->addStretch();
        layout->addLayout(controls);
        auto* splitter = new QSplitter(Qt::Horizontal, page);
        graphDamageVtk_ = new flightenv::ui::demo::VtkModelFieldWidget(splitter);
        graphDamageTrend_ = new flightenv::ui::display::ScalarTrendWidget(splitter);
        graphDamageTrend_->setTitle(QString::fromUtf8("最大累计损伤"), QStringLiteral("0-1"));
        graphDamageTrend_->setFixedRange(0.0, 1.0);
        splitter->addWidget(graphDamageVtk_);
        splitter->addWidget(graphDamageTrend_);
        splitter->setStretchFactor(0, 2);
        splitter->setStretchFactor(1, 1);
        layout->addWidget(splitter, 1);
        graphDamageTable_ = makeGraphTable(page,
            {QString::fromUtf8("时间/s"), QString::fromUtf8("增量数"), QString::fromUtf8("最大累计损伤"), QString::fromUtf8("最大增量"), QString::fromUtf8("首超")});
        graphDamageTable_->setMaximumHeight(170);
        layout->addWidget(graphDamageTable_);
        graphWorkflowTabs_->addTab(page, QString::fromUtf8("损伤"));
    }

    {
        auto* page = new QWidget(graphWorkflowTabs_);
        auto* layout = new QVBoxLayout(page);
        layout->setContentsMargins(6, 6, 6, 6);
        layout->setSpacing(6);
        lbGraphLifeSummary_ = new QLabel(QString::fromUtf8("等待寿命输出"), page);
        lbGraphLifeSummary_->setWordWrap(true);
        lbGraphLifeSummary_->setStyleSheet(QStringLiteral("QLabel{padding:5px;background:#f8fafc;border:1px solid #e5e7eb;border-radius:4px;}"));
        layout->addWidget(lbGraphLifeSummary_);
        auto* controls = new QHBoxLayout();
        controls->addWidget(new QLabel(QString::fromUtf8("寿命场 subject"), page));
        graphLifeSubjectCombo_ = makeSubjectCombo(page, QString::fromUtf8("优先显示 life.field 节点级剩余寿命场；缺失时才兼容旧 evidence 派生显示。"));
        graphLifeSubjectCombo_->setCurrentIndex(3);
        controls->addWidget(graphLifeSubjectCombo_);
        controls->addStretch();
        layout->addLayout(controls);
        auto* splitter = new QSplitter(Qt::Horizontal, page);
        graphLifeVtk_ = new flightenv::ui::demo::VtkModelFieldWidget(splitter);
        graphLifeTrend_ = new flightenv::ui::display::ScalarTrendWidget(splitter);
        graphLifeTrend_->setTitle(QString::fromUtf8("RUL/未来步"), QStringLiteral("s"));
        splitter->addWidget(graphLifeVtk_);
        splitter->addWidget(graphLifeTrend_);
        splitter->setStretchFactor(0, 2);
        splitter->setStretchFactor(1, 1);
        layout->addWidget(splitter, 1);
        graphLifeTable_ = makeGraphTable(page,
            {QString::fromUtf8("指标"), QString::fromUtf8("值"), QString::fromUtf8("说明")});
        graphLifeTable_->setMaximumHeight(170);
        layout->addWidget(graphLifeTable_);
        graphWorkflowTabs_->addTab(page, QString::fromUtf8("寿命"));
    }

    graphEvidenceText_ = new QPlainTextEdit(graphWorkflowTabs_);
    graphEvidenceText_->setReadOnly(true);
    graphEvidenceText_->setMaximumBlockCount(200);
    auto* evidencePage = new QWidget(graphWorkflowTabs_);
    auto* evidenceLayout = new QVBoxLayout(evidencePage);
    evidenceLayout->setContentsMargins(6, 6, 6, 6);
    evidenceLayout->addWidget(graphEvidenceText_);
    graphWorkflowTabs_->addTab(evidencePage, QString::fromUtf8("Evidence"));

    root->addWidget(graphWorkflowTabs_, 1);

    connect(graphFieldSubjectCombo_, qOverload<int>(&QComboBox::currentIndexChanged),
        this, &EnvPredictorUI::refreshGraphWorkflowEvidence_);
    connect(graphDamageSubjectCombo_, qOverload<int>(&QComboBox::currentIndexChanged),
        this, &EnvPredictorUI::refreshGraphWorkflowEvidence_);
    connect(graphLifeSubjectCombo_, qOverload<int>(&QComboBox::currentIndexChanged),
        this, &EnvPredictorUI::refreshGraphWorkflowEvidence_);
}

void EnvPredictorUI::clearGraphMultiFieldWidgets_()
{
    for (auto& item : graphMultiFieldWidgets_) {
        if (item.second) {
            item.second->deleteLater();
        }
    }
    graphMultiFieldWidgets_.clear();
    graphMultiFieldOrder_.clear();
}

void EnvPredictorUI::layoutGraphMultiFieldWidgets_()
{
    if (!graphMultiFieldGrid_ || !graphMultiFieldContainer_) {
        return;
    }

    QLayoutItem* item = nullptr;
    while ((item = graphMultiFieldGrid_->takeAt(0)) != nullptr) {
        if (item->widget()) {
            item->widget()->setParent(nullptr);
        }
        delete item;
    }

    const int columns = ui.windowNumspinBox
        ? std::max(1, ui.windowNumspinBox->value())
        : 2;
    for (int col = 0; col < columns; ++col) {
        graphMultiFieldGrid_->setColumnStretch(col, 1);
    }

    int visibleCount = 0;
    for (const QString& key : graphMultiFieldOrder_) {
        const auto it = graphMultiFieldWidgets_.find(key);
        if (it == graphMultiFieldWidgets_.end() || it->second == nullptr) {
            continue;
        }
        auto* widget = it->second;
        widget->show();
        widget->setParent(graphMultiFieldContainer_);
        graphMultiFieldGrid_->addWidget(widget, visibleCount / columns, visibleCount % columns);
        ++visibleCount;
    }

    if (visibleCount == 0) {
        auto*& widget = graphMultiFieldWidgets_[QStringLiteral("__empty__")];
        if (!widget) {
            widget = new flightenv::ui::demo::VtkModelFieldWidget(graphMultiFieldContainer_);
            widget->setMinimumHeight(280);
        }
        widget->clearField(QStringLiteral("No renderable field output in current graph evidence."));
        widget->show();
        widget->setParent(graphMultiFieldContainer_);
        graphMultiFieldGrid_->addWidget(widget, 0, 0);
    }

    graphMultiFieldContainer_->updateGeometry();
    if (graphMultiFieldScroll_) {
        graphMultiFieldScroll_->updateGeometry();
    }
}

void EnvPredictorUI::renderGraphPlatformFieldArtifacts_()
{
#ifdef FLIGHTENV_USE_PLATFORM_BACKEND
    if (!graphMultiFieldGrid_ || !runtime_view_) {
        return;
    }

    graphMultiFieldOrder_.clear();
    const auto fields = platformFieldsForCurrentFrame();
    const QString assetRoot = workspacePath(QStringLiteral("_deps/example"));

    for (const auto& field : fields) {
        const QString key = platformFieldIdentityKey(field);
        graphMultiFieldOrder_.push_back(key);

        auto*& widget = graphMultiFieldWidgets_[key];
        if (widget == nullptr) {
            widget = new flightenv::ui::demo::VtkModelFieldWidget(graphMultiFieldContainer_);
            widget->setMinimumHeight(280);
            widget->setAssetRoot(assetRoot);
            widget->setRuntimeSnapshot(runtime_view_->snapshot);
        }

        QString readError;
        bool loading = false;
        const std::vector<double> values = cachedPlatformFieldArtifactValues(field, &readError, &loading);
        if (loading && values.empty()) {
            widget->setStatusMessage(widget->hasRenderedValues()
                                         ? QStringLiteral("云图数据后台加载中，保留上一帧显示...")
                                         : QStringLiteral("云图数据后台加载中..."));
            continue;
        }
        if (!readError.isEmpty()) {
            widget->clearField(readError);
            continue;
        }
        if (field.node_count > 0 &&
            (values.size() < static_cast<std::size_t>(field.node_count) ||
             values.size() % static_cast<std::size_t>(field.node_count) != 0)) {
            widget->clearField(QStringLiteral("field artifact values cannot map to node_count: values=%1 node_count=%2")
                                   .arg(values.size())
                                   .arg(static_cast<qlonglong>(field.node_count)));
            continue;
        }

        widget->renderPlatformFieldArtifact(
            field,
            values,
            0,
            QStringLiteral("%1 · loop %2").arg(platformFieldTitle(field)).arg(field.loop_iteration_index));
    }

    layoutGraphMultiFieldWidgets_();
#endif
}

void EnvPredictorUI::buildGraphRuntimePage_()
{
    pageGraphRuntime_ = new QWidget(this);
    auto* root = new QVBoxLayout(pageGraphRuntime_);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);

    auto* controlGroup = new QGroupBox(QString::fromUtf8("GraphRuntimeControllerRunner"), pageGraphRuntime_);
    auto* controlLayout = new QGridLayout(controlGroup);
    controlLayout->setContentsMargins(10, 12, 10, 10);
    controlLayout->setHorizontalSpacing(8);
    controlLayout->setVerticalSpacing(6);

    graphCatalogPathEdit_ = new QLineEdit(
        workspacePath(QStringLiteral("_local_artifacts/flightenv-runtime-private/platform/platform-catalog.json")),
        controlGroup);
    graphOutputDirEdit_ = new QLineEdit(
        workspacePath(QStringLiteral("_local_artifacts/flightenv-runtime-private/graph-runtime-controller/ui_live_run")),
        controlGroup);
    // 这四个控件会原样传给 runner：预测步数控制单次未来链路长度，
    // 在线帧数和预测间隔帧控制数据库 replay 以及何时触发完整预测。
    graphMaxSamplesSpin_ = new QSpinBox(controlGroup);
    graphMaxSamplesSpin_->setRange(1, 2000);
    graphMaxSamplesSpin_->setValue(8);
    graphMaxSamplesSpin_->setToolTip(QString::fromUtf8("每次预测 run 的未来弹道/场/损伤推进步数。"));
    graphReplayFramesSpin_ = new QSpinBox(controlGroup);
    graphReplayFramesSpin_->setRange(1, 1000);
    graphReplayFramesSpin_->setValue(50);
    graphReplayFramesSpin_->setToolTip(QString::fromUtf8("从数据库 taskpoint replay 的在线输入帧数；每一帧对应一次在线融合/滤波状态更新。"));
    graphPredictionIntervalSpin_ = new QSpinBox(controlGroup);
    graphPredictionIntervalSpin_->setRange(1, 500);
    graphPredictionIntervalSpin_->setValue(10);
    graphPredictionIntervalSpin_->setToolTip(QString::fromUtf8("每隔多少个在线帧触发一次完整未来预测；runner 也会在第一帧和最后一帧触发。"));
    graphFrameDelayMsSpin_ = new QSpinBox(controlGroup);
    graphFrameDelayMsSpin_->setRange(0, 5000);
    graphFrameDelayMsSpin_->setValue(200);
    graphFrameDelayMsSpin_->setToolTip(QString::fromUtf8("在线帧 replay 延迟，用于 UI 中观察实时刷新。"));
    graphPredictToLandingCheck_ = new QCheckBox(QString::fromUtf8("预测到落点/完整 horizon"), controlGroup);
    graphPredictToLandingCheck_->setToolTip(QString::fromUtf8(
        "勾选后 UI 将单次预测步数提升到 2000，用于检查弹道从当前在线状态延伸到落点；不勾选时使用左侧步数做快速调试。"));
    graphPredictionOffCheck_ = new QCheckBox(QString::fromUtf8("只跑在线滤波"), controlGroup);
    graphPredictionOffCheck_->setToolTip(QString::fromUtf8(
        "勾选后只回放数据库观测并执行在线状态转移/观测/滤波，不触发未来预测/QoI 分支。"));

    graphStartButton_ = new QPushButton(QString::fromUtf8("启动算子图"), controlGroup);
    graphStopButton_ = new QPushButton(QString::fromUtf8("停止"), controlGroup);
    graphStopButton_->setEnabled(false);
    auto* refreshButton = new QPushButton(QString::fromUtf8("刷新状态"), controlGroup);

    controlLayout->addWidget(new QLabel(QString::fromUtf8("Catalog"), controlGroup), 0, 0);
    controlLayout->addWidget(graphCatalogPathEdit_, 0, 1, 1, 4);
    controlLayout->addWidget(new QLabel(QString::fromUtf8("Evidence"), controlGroup), 1, 0);
    controlLayout->addWidget(graphOutputDirEdit_, 1, 1, 1, 4);
    controlLayout->addWidget(new QLabel(QString::fromUtf8("每次预测步数"), controlGroup), 2, 0);
    controlLayout->addWidget(graphMaxSamplesSpin_, 2, 1);
    controlLayout->addWidget(new QLabel(QString::fromUtf8("在线帧数"), controlGroup), 2, 2);
    controlLayout->addWidget(graphReplayFramesSpin_, 2, 3);
    controlLayout->addWidget(new QLabel(QString::fromUtf8("预测间隔帧"), controlGroup), 2, 4);
    controlLayout->addWidget(graphPredictionIntervalSpin_, 2, 5);
    controlLayout->addWidget(new QLabel(QString::fromUtf8("帧延迟/ms"), controlGroup), 3, 0);
    controlLayout->addWidget(graphFrameDelayMsSpin_, 3, 1);
    controlLayout->addWidget(graphPredictToLandingCheck_, 3, 2, 1, 2);
    controlLayout->addWidget(graphPredictionOffCheck_, 3, 4, 1, 2);
    controlLayout->addWidget(graphStartButton_, 4, 2);
    controlLayout->addWidget(graphStopButton_, 4, 3);
    controlLayout->addWidget(refreshButton, 4, 4, 1, 2);
    controlLayout->setColumnStretch(1, 1);
    root->addWidget(controlGroup);

    lbGraphRuntimeStatus_ = new QLabel(QString::fromUtf8("未启动"), pageGraphRuntime_);
    lbGraphRuntimeStatus_->setWordWrap(true);
    lbGraphRuntimeStatus_->setStyleSheet(QStringLiteral("QLabel{padding:6px;border:1px solid #cbd5e1;border-radius:4px;background:#f8fafc;}"));
    root->addWidget(lbGraphRuntimeStatus_);

    graphOperatorScroll_ = new QScrollArea(pageGraphRuntime_);
    graphOperatorScroll_->setWidgetResizable(true);
    graphOperatorScroll_->setMinimumHeight(260);
    graphOperatorContainer_ = new QWidget(graphOperatorScroll_);
    graphOperatorGrid_ = new QGridLayout(graphOperatorContainer_);
    graphOperatorGrid_->setContentsMargins(4, 4, 4, 4);
    graphOperatorGrid_->setSpacing(8);
    graphOperatorScroll_->setWidget(graphOperatorContainer_);
    root->addWidget(graphOperatorScroll_, 1);

    buildGraphWorkflowTabs_(root);

    graphRuntimeLog_ = new QPlainTextEdit(pageGraphRuntime_);
    graphRuntimeLog_->setReadOnly(true);
    graphRuntimeLog_->setMaximumBlockCount(500);
    graphRuntimeLog_->setMinimumHeight(120);
    root->addWidget(graphRuntimeLog_);

    graphRunnerProcess_ = new QProcess(this);
    graphRunnerProcess_->setProcessChannelMode(QProcess::MergedChannels);
    graphRuntimePollTimer_ = new QTimer(this);
    graphRuntimePollTimer_->setInterval(500);

    connect(graphStartButton_, &QPushButton::clicked, this, &EnvPredictorUI::startGraphRuntimeRunner_);
    connect(graphStopButton_, &QPushButton::clicked, this, &EnvPredictorUI::stopGraphRuntimeRunner_);
    connect(refreshButton, &QPushButton::clicked, this, &EnvPredictorUI::refreshGraphRuntimePage_);
    connect(refreshButton, &QPushButton::clicked, this, &EnvPredictorUI::refreshGraphWorkflowEvidence_);
    connect(graphPredictToLandingCheck_, &QCheckBox::toggled, this, [this](const bool checked) {
        if (graphMaxSamplesSpin_) {
            graphMaxSamplesSpin_->setEnabled(!checked);
        }
    });
    connect(graphRuntimePollTimer_, &QTimer::timeout, this, &EnvPredictorUI::refreshGraphRuntimePage_);
    connect(graphRunnerProcess_, &QProcess::readyReadStandardOutput, this, [this]() {
        appendGraphRuntimeLog_(QString::fromUtf8(graphRunnerProcess_->readAllStandardOutput()));
    });
    connect(graphRunnerProcess_, &QProcess::readyReadStandardError, this, [this]() {
        appendGraphRuntimeLog_(QString::fromUtf8(graphRunnerProcess_->readAllStandardError()));
    });
    connect(graphRunnerProcess_,
        qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
        this,
        [this](int exitCode, QProcess::ExitStatus exitStatus) {
            appendGraphRuntimeLog_(QString::fromUtf8("GraphRuntimeControllerRunner 退出：code=%1 status=%2")
                .arg(exitCode)
                .arg(exitStatus == QProcess::NormalExit ? QStringLiteral("normal") : QStringLiteral("crash")));
            refreshGraphRuntimePage_();
            if (graphRuntimePollTimer_) {
                graphRuntimePollTimer_->stop();
            }
            if (graphStartButton_) {
                graphStartButton_->setEnabled(true);
            }
            if (graphStopButton_) {
                graphStopButton_->setEnabled(false);
            }
        });

    ui.tabWidget_main->insertTab(2, pageGraphRuntime_, QString::fromUtf8("算子运行"));
    refreshGraphRuntimePage_();
}

void EnvPredictorUI::setGraphRuntimeStatus_(const QString& text)
{
    if (lbGraphRuntimeStatus_) {
        lbGraphRuntimeStatus_->setText(text);
    }
}

void EnvPredictorUI::appendGraphRuntimeLog_(const QString& text)
{
    if (!graphRuntimeLog_ || text.isEmpty()) {
        return;
    }
    graphRuntimeLog_->appendPlainText(text.trimmed());
}

void EnvPredictorUI::startGraphRuntimeRunner_()
{
    if (!graphRunnerProcess_) {
        return;
    }
    if (graphRunnerProcess_->state() != QProcess::NotRunning) {
        setGraphRuntimeStatus_(QString::fromUtf8("算子图已经在运行中。"));
        return;
    }

    const QString runnerPath = workspacePath(QStringLiteral("_deps/workspace/x64/Release/GraphRuntimeControllerRunner.exe"));
    const QString catalogPath = graphCatalogPathEdit_ ? graphCatalogPathEdit_->text().trimmed() : QString();
    const QString outputDir = graphOutputDirEdit_ ? graphOutputDirEdit_->text().trimmed() : QString();
    if (!QFileInfo::exists(runnerPath)) {
        setGraphRuntimeStatus_(QString::fromUtf8("未找到 Runner：%1").arg(runnerPath));
        return;
    }
    if (!QFileInfo::exists(catalogPath)) {
        setGraphRuntimeStatus_(QString::fromUtf8("未找到 catalog：%1").arg(catalogPath));
        return;
    }
    QDir().mkpath(outputDir);
    // 启动前清掉上一轮 evidence，避免 UI 先显示旧的单帧/旧 run 结果。
    for (const QString& name : {
        QStringLiteral("operator_live_status.json"),
        QStringLiteral("graph_run_evidence.json"),
        QStringLiteral("graph_snapshot.json"),
        QStringLiteral("operator_snapshot.json"),
        QStringLiteral("resource_lock.json"),
        QStringLiteral("graph_outputs.json"),
        QStringLiteral("runtime_snapshot.json"),
        QStringLiteral("workflow_timeline.json")}) {
        QFile::remove(QDir(outputDir).filePath(name));
    }
    QDir staleOnlineDir(QDir(outputDir).filePath(QStringLiteral("online_filter")));
    if (staleOnlineDir.exists()) {
        staleOnlineDir.removeRecursively();
    }

    while (graphOperatorGrid_ && graphOperatorGrid_->count() > 0) {
        QLayoutItem* item = graphOperatorGrid_->takeAt(0);
        if (item && item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }
    graphOperatorCards_.clear();
    clearGraphTable(graphPathTable_);
    clearGraphTable(graphThroughputTable_);
    clearGraphTable(graphAssetTable_);
    clearGraphTable(graphFusionTable_);
    clearGraphTable(graphPathTable_);
    clearGraphTable(graphThroughputTable_);
    clearGraphTable(graphObjectTable_);
    clearGraphTable(graphBindingTable_);
    clearGraphTable(graphEquationTable_);
    clearGraphTable(graphCatalogModelTable_);
    clearGraphTable(graphTrajectoryTable_);
    clearGraphTable(graphFieldTable_);
    clearGraphTable(graphDamageTable_);
    clearGraphTable(graphLifeTable_);
    if (graphWorkflowPathWidget_) {
        graphWorkflowPathWidget_->clear();
    }
    if (graphTrajectoryPlot_) {
        graphTrajectoryPlot_->clear();
    }
    if (graphDamageTrend_) {
        graphDamageTrend_->clear();
    }
    if (graphLifeTrend_) {
        graphLifeTrend_->clear();
    }
    clearGraphMultiFieldWidgets_();
    if (graphFieldVtk_) {
        graphFieldVtk_->clearField(QString::fromUtf8("等待本轮 runtime_snapshot 和 field.forecast"));
    }
    if (graphDamageVtk_) {
        graphDamageVtk_->clearField(QString::fromUtf8("等待本轮 runtime_snapshot 和 damage.forecast"));
    }
    if (graphLifeVtk_) {
        graphLifeVtk_->clearField(QString::fromUtf8("等待本轮 runtime_snapshot 和 life.field"));
    }
    setLabelText(lbGraphWorkflowSummary_, QString::fromUtf8("完整链路：等待本轮运行输出。"));
    setLabelText(lbGraphObjectSummary_, QString::fromUtf8("对象画像：等待本轮 catalog/evidence 解析。"));
    setLabelText(lbGraphThroughputSummary_, QString::fromUtf8("实时吞吐：等待本轮运行输出。"));
    setLabelText(lbGraphTrajectorySummary_, QString::fromUtf8("等待弹道输出"));
    setLabelText(lbGraphFieldSummary_, QString::fromUtf8("等待多物理场输出"));
    setLabelText(lbGraphDamageSummary_, QString::fromUtf8("等待损伤输出"));
    setLabelText(lbGraphLifeSummary_, QString::fromUtf8("等待寿命输出"));
    if (graphEvidenceText_) {
        graphEvidenceText_->clear();
    }
    graphMultiFieldOrder_.clear();
    if (graphRuntimeLog_) {
        graphRuntimeLog_->clear();
    }

    QStringList args;
    args << catalogPath << outputDir;
    // 参数顺序必须和 GraphRuntimeControllerRunner 保持一致：
    // catalog evidence max_prediction_samples replay_frame_count prediction_interval_frames frame_delay_ms。
    const int requestedMaxSamples =
        (graphPredictToLandingCheck_ && graphPredictToLandingCheck_->isChecked())
            ? 2000
            : (graphMaxSamplesSpin_ ? graphMaxSamplesSpin_->value() : 8);
    args << QString::number(requestedMaxSamples);
    args << QString::number(graphReplayFramesSpin_ ? graphReplayFramesSpin_->value() : 50);
    args << QString::number(graphPredictionIntervalSpin_ ? graphPredictionIntervalSpin_->value() : 10);
    args << QString::number(graphFrameDelayMsSpin_ ? graphFrameDelayMsSpin_->value() : 200);
    if (graphPredictionOffCheck_ && graphPredictionOffCheck_->isChecked()) {
        args << QStringLiteral("keep") << QStringLiteral("prediction_off");
    }
    graphRunnerProcess_->setWorkingDirectory(QFileInfo(runnerPath).absolutePath());
    appendGraphRuntimeLog_(QString::fromUtf8("启动：%1 %2").arg(runnerPath, args.join(QStringLiteral(" "))));
    graphRunnerProcess_->start(runnerPath, args);
    if (!graphRunnerProcess_->waitForStarted(3000)) {
        setGraphRuntimeStatus_(QString::fromUtf8("Runner 启动失败：%1").arg(graphRunnerProcess_->errorString()));
        return;
    }
    if (graphStartButton_) {
        graphStartButton_->setEnabled(false);
    }
    if (graphStopButton_) {
        graphStopButton_->setEnabled(true);
    }
    if (graphRuntimePollTimer_) {
        graphRuntimePollTimer_->start();
    }
    setGraphRuntimeStatus_(QString::fromUtf8("运行中：正在等待 operator_live_status.json。"));
}

void EnvPredictorUI::stopGraphRuntimeRunner_()
{
    if (graphRuntimePollTimer_) {
        graphRuntimePollTimer_->stop();
    }
    if (graphRunnerProcess_ && graphRunnerProcess_->state() != QProcess::NotRunning) {
        appendGraphRuntimeLog_(QString::fromUtf8("请求停止 GraphRuntimeControllerRunner。"));
        graphRunnerProcess_->terminate();
        if (!graphRunnerProcess_->waitForFinished(3000)) {
            graphRunnerProcess_->kill();
            graphRunnerProcess_->waitForFinished(3000);
        }
    }
    if (graphStartButton_) {
        graphStartButton_->setEnabled(true);
    }
    if (graphStopButton_) {
        graphStopButton_->setEnabled(false);
    }
}

void EnvPredictorUI::refreshGraphRuntimePage_()
{
    if (!graphOutputDirEdit_ || !graphOperatorGrid_) {
        return;
    }
    const QString outputDir = graphOutputDirEdit_->text().trimmed();
    try {
        const auto live = launchsupport::read_graph_operator_live_status(
            std::filesystem::path(outputDir.toStdWString()));
        if (live.status == "missing") {
            setGraphRuntimeStatus_(QString::fromUtf8("等待实时状态文件：%1")
                .arg(QString::fromStdWString(live.live_status_path.wstring())));
            return;
        }

        setGraphRuntimeStatus_(QString::fromUtf8("状态=%1，算子=%2，run=%3")
            .arg(liveStatusText(live.status))
            .arg(static_cast<int>(live.operators.size()))
            .arg(QString::fromUtf8(live.run_id.c_str())));

        for (const auto& op : live.operators) {
            const QString key = QString::fromUtf8(
                !op.node_id.empty() ? op.node_id.c_str() : op.operator_ref.c_str());
            QGroupBox* box = nullptr;
            auto found = graphOperatorCards_.find(key);
            if (found == graphOperatorCards_.end()) {
                box = new QGroupBox(key, graphOperatorContainer_);
                auto* form = new QFormLayout(box);
                form->setContentsMargins(8, 12, 8, 8);
                form->setSpacing(4);
                const QList<QPair<QString, QString>> rows = {
                    {QStringLiteral("status"), QString::fromUtf8("状态")},
                    {QStringLiteral("type"), QString::fromUtf8("类型")},
                    {QStringLiteral("kind"), QString::fromUtf8("执行")},
                    {QStringLiteral("ports"), QString::fromUtf8("端口")},
                    {QStringLiteral("resources"), QString::fromUtf8("资源")},
                    {QStringLiteral("duration"), QString::fromUtf8("耗时")},
                    {QStringLiteral("reason"), QString::fromUtf8("诊断")}
                };
                for (const auto& row : rows) {
                    auto* label = new QLabel(box);
                    label->setObjectName(row.first);
                    label->setWordWrap(true);
                    form->addRow(row.second, label);
                }
                const int cardIndex = static_cast<int>(graphOperatorCards_.size());
                graphOperatorGrid_->addWidget(box, cardIndex / 2, cardIndex % 2);
                graphOperatorCards_[key] = box;
            }
            else {
                box = found->second;
            }
            box->setTitle(QString::fromUtf8("%1  %2").arg(key, QString::fromUtf8(op.operator_id.c_str())));
            box->setStyleSheet(liveStatusStyle(QString::fromUtf8(op.status.c_str())));
            if (auto* label = namedLabel(box, "status")) {
                label->setText(liveStatusText(op.status));
            }
            if (auto* label = namedLabel(box, "type")) {
                label->setText(QString::fromUtf8("%1 / %2")
                    .arg(QString::fromUtf8(op.operator_type.c_str()), QString::fromUtf8(op.role.c_str())));
            }
            if (auto* label = namedLabel(box, "kind")) {
                label->setText(QString::fromUtf8("%1  %2")
                    .arg(QString::fromUtf8(op.execution_kind.c_str()), QString::fromUtf8(op.operator_ref.c_str())));
            }
            if (auto* label = namedLabel(box, "ports")) {
                label->setText(QString::fromUtf8("%1 -> %2")
                    .arg(joinStdStrings(op.input_ports), joinStdStrings(op.output_ports)));
            }
            if (auto* label = namedLabel(box, "resources")) {
                label->setText(joinStdStrings(op.input_resource_ids, 3));
            }
            if (auto* label = namedLabel(box, "duration")) {
                label->setText(op.duration_ms > 0
                    ? QString::fromUtf8("%1 ms").arg(static_cast<qlonglong>(op.duration_ms))
                    : QString::fromUtf8("-"));
            }
            if (auto* label = namedLabel(box, "reason")) {
                label->setText(QString::fromUtf8(op.reason.c_str()));
            }
        }
        refreshGraphWorkflowEvidence_();
    }
    catch (const std::exception& e) {
        setGraphRuntimeStatus_(QString::fromUtf8("读取 GraphRuntime 状态失败：%1").arg(QString::fromUtf8(e.what())));
    }
}

void EnvPredictorUI::refreshGraphWorkflowEvidence_()
{
    if (!graphOutputDirEdit_) {
        return;
    }

    clearGraphTable(graphAssetTable_);
    clearGraphTable(graphFusionTable_);
    clearGraphTable(graphPathTable_);
    clearGraphTable(graphThroughputTable_);
    clearGraphTable(graphObjectTable_);
    clearGraphTable(graphBindingTable_);
    clearGraphTable(graphEquationTable_);
    clearGraphTable(graphCatalogModelTable_);
    clearGraphTable(graphTrajectoryTable_);
    clearGraphTable(graphFieldTable_);
    clearGraphTable(graphDamageTable_);
    clearGraphTable(graphLifeTable_);
    if (graphEvidenceText_) {
        graphEvidenceText_->clear();
    }
    graphMultiFieldOrder_.clear();
#ifdef FLIGHTENV_USE_PLATFORM_BACKEND
    const bool graphDtoVtkEnabled = false;
#else
    const bool graphDtoVtkEnabled = true;
#endif

    const QString outputDirText = graphOutputDirEdit_->text().trimmed();
    const QString catalogPathText = graphCatalogPathEdit_ ? graphCatalogPathEdit_->text().trimmed() : QString();
    populateCatalogWorkbench(
        catalogPathText,
        lbGraphObjectSummary_,
        graphObjectTable_,
        graphBindingTable_,
        graphCatalogModelTable_);
    appendConceptualEquationRows(graphEquationTable_);

    const std::filesystem::path runDir(outputDirText.toStdWString());
    const std::filesystem::path evidencePath = runDir / "graph_run_evidence.json";
    if (!std::filesystem::exists(evidencePath)) {
        // 首个 graph_run_evidence.json 通常要等真实模型初始化和第一次预测结束。
        // 这段先读 workflow_timeline.json，让 UI 能看到在线帧/预测 run 的过程状态。
        QString timelineError;
        const auto timeline = readJsonFileNlohmann(runDir / "workflow_timeline.json", &timelineError);
        if (timeline && timeline->is_object()) {
            const QString summary = QString::fromUtf8("在线帧=%1/%2，滤波帧=%3，预测run=%4，状态=%5；等待首个 graph evidence")
                .arg(static_cast<qlonglong>(jsonDouble(*timeline, "observed_frame_count", 0.0)))
                .arg(static_cast<qlonglong>(jsonDouble(*timeline, "requested_frame_count", 0.0)))
                .arg(static_cast<qlonglong>(jsonDouble(*timeline, "filter_update_count", 0.0)))
                .arg(static_cast<qlonglong>(jsonDouble(*timeline, "prediction_run_count", 0.0)))
                .arg(jsonString(*timeline, "status"));
            setLabelText(lbGraphWorkflowSummary_, QString::fromUtf8("完整链路：%1").arg(summary));
            setLabelText(lbGraphThroughputSummary_, QString::fromUtf8("实时吞吐：%1").arg(summary));
            appendGraphRow(graphThroughputTable_, {
                QString::fromUtf8("workflow_timeline"),
                jsonString(*timeline, "status"),
                QStringLiteral("-"),
                QStringLiteral("-"),
                summary,
                QString::fromStdWString((runDir / "workflow_timeline.json").wstring())
            });
        }
        else {
            setLabelText(lbGraphWorkflowSummary_, QString::fromUtf8("完整链路：等待 graph_run_evidence.json。"));
        }
        return;
    }

    try {
        // graph_run_evidence 出现后，所有正式展示都走 node-sdk reader；
        // UI 只消费公开 JSON/DTO，不直接 include runtime-private 内核算法。
        const auto view = launchsupport::read_graph_run_evidence(runDir);
        setLabelText(lbGraphWorkflowSummary_,
            QString::fromUtf8("完整链路：run=%1，graph=%2，状态=%3，节点=%4，算子=%5，输出端口=%6")
                .arg(QString::fromUtf8(view.run_id.c_str()),
                    QString::fromUtf8(view.graph_template_id.c_str()),
                    liveStatusText(view.status))
                .arg(static_cast<int>(view.graph_nodes.size()))
                .arg(static_cast<int>(view.operator_snapshots.size()))
                .arg(static_cast<int>(view.result_ports.size())));

        clearGraphTable(graphEquationTable_);
        const std::filesystem::path onlineRunDir = runDir / "online_filter";
        if (std::filesystem::exists(onlineRunDir / "graph_run_evidence.json")) {
            try {
                const auto onlineView = launchsupport::read_graph_run_evidence(onlineRunDir);
                appendEquationRowsFromView(
                    graphEquationTable_,
                    QString::fromUtf8("在线滤波子图：状态转移 + 观测 + PF"),
                    onlineView.graph_nodes);
            }
            catch (const std::exception& e) {
                appendGraphRow(graphEquationTable_, {
                    QString::fromUtf8("在线滤波子图"),
                    QString::fromUtf8("读取失败"),
                    QStringLiteral("-"),
                    QStringLiteral("-"),
                    QString::fromUtf8(e.what())
                });
                appendConceptualOnlineEquationRows(graphEquationTable_);
            }
        }
        else {
            appendConceptualOnlineEquationRows(graphEquationTable_);
        }
        appendEquationRowsFromView(
            graphEquationTable_,
            QString::fromUtf8("未来预测/QoI 子图：当前状态到落点预测"),
            view.graph_nodes);

        const auto portSizes = resultPortSizes(view.result_ports);
        std::map<std::string, launchsupport::GraphOperatorLiveStatusView> liveByNode;
        try {
            const auto live = launchsupport::read_graph_operator_live_status(runDir);
            for (const auto& op : live.operators) {
                liveByNode[op.node_id] = op;
            }
        }
        catch (...) {
        }

        std::map<std::string, launchsupport::GraphOperatorSnapshotView> snapshotByNode;
        for (const auto& snapshot : view.operator_snapshots) {
            snapshotByNode[snapshot.node_id] = snapshot;
        }

        std::vector<flightenv::ui::display::WorkflowStepView> workflowSteps;
        std::size_t totalBytes = 0;
        std::int64_t totalDurationMs = 0;
        int graphRow = 0;
        for (const auto& node : view.graph_nodes) {
            const auto liveIt = liveByNode.find(node.node_id);
            const auto snapshotIt = snapshotByNode.find(node.node_id);
            const std::int64_t durationMs = liveIt == liveByNode.end() ? 0 : liveIt->second.duration_ms;
            const std::size_t outputBytes = outputBytesForPorts(node.outputs, portSizes);
            totalBytes += outputBytes;
            totalDurationMs += std::max<std::int64_t>(0, durationMs);

            appendGraphRow(graphPathTable_, {
                QString::number(++graphRow),
                QString::fromUtf8(node.node_id.c_str()),
                QString::fromUtf8(node.operator_type.c_str()),
                joinStdStrings(node.inputs, 6),
                joinStdStrings(node.outputs, 6),
                snapshotIt == snapshotByNode.end()
                    ? QStringLiteral("-")
                    : QString::fromUtf8(snapshotIt->second.execution_kind.c_str())
            });
            appendGraphRow(graphThroughputTable_, {
                QString::fromUtf8(node.node_id.c_str()),
                liveIt == liveByNode.end()
                    ? QString::fromUtf8("未上报")
                    : liveStatusText(liveIt->second.status),
                durationMs > 0 ? QString::number(static_cast<qlonglong>(durationMs)) : QStringLiteral("-"),
                bytesText(outputBytes),
                throughputText(outputBytes, durationMs),
                snapshotIt == snapshotByNode.end()
                    ? QStringLiteral("-")
                    : joinStdStrings(snapshotIt->second.input_resource_ids, 3)
            });

            flightenv::ui::display::WorkflowStepView step;
            step.node_id = QString::fromUtf8(node.node_id.c_str());
            step.operator_type = QString::fromUtf8(node.operator_type.c_str());
            step.status = liveIt == liveByNode.end()
                ? QStringLiteral("unknown")
                : QString::fromUtf8(liveIt->second.status.c_str());
            step.inputs = joinStdStrings(node.inputs, 3);
            step.outputs = joinStdStrings(node.outputs, 3);
            step.duration_ms = durationMs;
            step.output_bytes = outputBytes;
            workflowSteps.push_back(std::move(step));
        }
        if (graphWorkflowPathWidget_) {
            graphWorkflowPathWidget_->setSteps(std::move(workflowSteps));
        }
        QString timelineSummary;
        QString timelineError;
        // workflow_timeline 是多帧调度账本，补充单次 graph evidence 看不到的总帧数和总步数。
        const auto workflowTimeline = view.has_workflow_timeline
            ? readJsonFileNlohmann(view.workflow_timeline_path, &timelineError)
            : std::optional<nlohmann::json>{};
        if (workflowTimeline && workflowTimeline->is_object()) {
            timelineSummary = QString::fromUtf8("在线帧=%1，滤波帧=%2，预测run=%3，弹道步=%4，场步=%5，损伤步=%6，寿命场节点值=%7")
                .arg(static_cast<qlonglong>(jsonDouble(*workflowTimeline, "observed_frame_count", 0.0)))
                .arg(static_cast<qlonglong>(jsonDouble(*workflowTimeline, "filter_update_count", 0.0)))
                .arg(static_cast<qlonglong>(jsonDouble(*workflowTimeline, "prediction_run_count", 0.0)))
                .arg(static_cast<qlonglong>(jsonDouble(*workflowTimeline, "trajectory_step_count_total", 0.0)))
                .arg(static_cast<qlonglong>(jsonDouble(*workflowTimeline, "field_step_count_total", 0.0)))
                .arg(static_cast<qlonglong>(jsonDouble(*workflowTimeline, "damage_step_count_total", 0.0)))
                .arg(static_cast<qlonglong>(jsonDouble(*workflowTimeline, "life_field_node_value_count", 0.0)));
            appendGraphRow(graphThroughputTable_, {
                QString::fromUtf8("workflow_timeline"),
                jsonString(*workflowTimeline, "status"),
                QStringLiteral("-"),
                bytesText(static_cast<std::size_t>(std::max<qint64>(
                    0,
                    QFileInfo(QString::fromStdWString(view.workflow_timeline_path.wstring())).size()))),
                timelineSummary,
                QString::fromStdWString(view.workflow_timeline_path.wstring())
            });
            const auto runs = workflowTimeline->value("prediction_runs", nlohmann::json::array());
            for (const auto& run : runs) {
                appendGraphRow(graphThroughputTable_, {
                    QString::fromUtf8("prediction_run[%1]").arg(static_cast<int>(jsonDouble(run, "run_index", 0.0))),
                    jsonString(run, "trajectory_status"),
                    QStringLiteral("-"),
                    QString::fromUtf8("traj=%1 field=%2 damage=%3 life_nodes=%4")
                        .arg(static_cast<qlonglong>(jsonDouble(run, "trajectory_sample_count", 0.0)))
                        .arg(static_cast<qlonglong>(jsonDouble(run, "field_step_count", 0.0)))
                        .arg(static_cast<qlonglong>(jsonDouble(run, "damage_step_count", 0.0)))
                        .arg(static_cast<qlonglong>(jsonDouble(run, "life_field_node_value_count", 0.0))),
                    QString::fromUtf8("RUL=%1s").arg(formatNumber(jsonDouble(run, "rul_s", -1.0), 7)),
                    jsonString(run, "source_state_frame_id")
                });
            }
        }
        else {
            timelineSummary = view.has_workflow_timeline
                ? QString::fromUtf8("timeline读取失败：%1").arg(timelineError)
                : QString::fromUtf8("timeline缺失：当前只能看单次 graph evidence");
        }

        setLabelText(lbGraphThroughputSummary_,
            QString::fromUtf8("实时吞吐：端口总输出=%1，累计执行耗时=%2 ms，平均吞吐=%3，runtime_snapshot=%4；%5")
                .arg(bytesText(totalBytes))
                .arg(static_cast<qlonglong>(totalDurationMs))
                .arg(throughputText(totalBytes, totalDurationMs))
                .arg(view.has_runtime_snapshot ? QString::fromUtf8("可用") : QString::fromUtf8("缺失"),
                    timelineSummary));

        const QString runnerPath = workspacePath(QStringLiteral("_deps/workspace/x64/Release/GraphRuntimeControllerRunner.exe"));
        const QString trainerPath = workspacePath(QStringLiteral("_deps/workspace/x64/Release/EnvTrainer.exe"));
        appendGraphRow(graphAssetTable_, {
            QString::fromUtf8("运行入口"),
            QString::fromUtf8("GraphRuntimeControllerRunner"),
            QFileInfo::exists(runnerPath) ? QString::fromUtf8("可用") : QString::fromUtf8("缺失"),
            runnerPath
        });
        appendGraphRow(graphAssetTable_, {
            QString::fromUtf8("训练入口"),
            QString::fromUtf8("EnvTrainer"),
            QFileInfo::exists(trainerPath) ? QString::fromUtf8("可用") : QString::fromUtf8("缺失"),
            trainerPath
        });
        for (const auto& lock : view.resource_locks) {
            QString kind = QString::fromUtf8(lock.resource_type.c_str());
            const QString id = QString::fromUtf8(lock.resource_id.c_str());
            if (id.contains(QStringLiteral("database"), Qt::CaseInsensitive)) kind = QString::fromUtf8("数据库");
            else if (id.contains(QStringLiteral("pod"), Qt::CaseInsensitive)) kind = QString::fromUtf8("POD基/训练资产");
            else if (id.contains(QStringLiteral("bpnn"), Qt::CaseInsensitive)) kind = QString::fromUtf8("BPNN网络/训练资产");
            else if (id.contains(QStringLiteral("geometry"), Qt::CaseInsensitive)) kind = QString::fromUtf8("三维模型");
            else if (id.contains(QStringLiteral("trajectory"), Qt::CaseInsensitive)) kind = QString::fromUtf8("弹道模型/包");
            appendGraphRow(graphAssetTable_, {
                kind,
                id,
                compactPathText(lock.uri),
                lock.checksum.empty()
                    ? QString::fromUtf8(lock.description.c_str())
                    : QString::fromUtf8("checksum=%1").arg(QString::fromUtf8(lock.checksum.substr(0, 12).c_str()))
            });
        }

        QString jsonError;
        const auto operatorJson = readJsonFileNlohmann(view.operator_snapshot_path, &jsonError);
        if (operatorJson && operatorJson->is_array()) {
            for (const auto& op : *operatorJson) {
                const QString type = jsonString(op, "operator_type");
                if (type != QStringLiteral("field_reconstruction") &&
                    type != QStringLiteral("field_merge") &&
                    type != QStringLiteral("state_transition") &&
                    type != QStringLiteral("qoi_equation")) {
                    continue;
                }

                const auto metadata = op.value("metadata", nlohmann::json::object());
                const auto modelSnapshot = metadata.value("model_snapshot", nlohmann::json::object());
                const auto modelMeta = modelSnapshot.value("metadata", nlohmann::json::object());
                const auto cfg = modelMeta.value("cfg_override", nlohmann::json::object());
                const auto runtimeMeta = modelMeta.value("runtime_meta_summary", nlohmann::json::object());
                const auto layouts = runtimeMeta.value("field_layouts", nlohmann::json::array());
                QString layoutText;
                for (const auto& layout : layouts) {
                    if (!layoutText.isEmpty()) {
                        layoutText += QStringLiteral("; ");
                    }
                    layoutText += QString::fromUtf8("%1:%2 nodes=%3")
                        .arg(jsonString(layout, "subject"),
                            jsonString(layout, "name"),
                            jsonString(layout, "node_count"));
                }
                if (layoutText.isEmpty()) {
                    layoutText = QStringLiteral("-");
                }

                QString cfgText;
                if (cfg.is_object()) {
                    cfgText = QString::fromUtf8("pfkit.init_state.mode=%1, runtime.mode=%2")
                        .arg(jsonString(cfg, "pfkit.init_state.mode"), jsonString(cfg, "runtime.mode"));
                }
                else if (type == QStringLiteral("state_transition")) {
                    cfgText = QString::fromUtf8("弹道状态转移 / 未来预测");
                }
                else if (type == QStringLiteral("qoi_equation")) {
                    cfgText = QString::fromUtf8("状态到 QoI 映射");
                }
                else {
                    cfgText = QStringLiteral("-");
                }

                appendGraphRow(graphFusionTable_, {
                    QString::fromUtf8("%1 / %2").arg(jsonString(op, "node_id"), type),
                    jsonString(modelSnapshot, "model_status", jsonString(modelSnapshot, "model_id")),
                    cfgText,
                    layoutText
                });
            }
        }
        else if (!jsonError.isEmpty()) {
            appendGraphRow(graphFusionTable_, {
                QString::fromUtf8("operator_snapshot"),
                QString::fromUtf8("读取失败"),
                jsonError,
                QStringLiteral("-")
            });
        }

        QString runtimeSnapshotError;
        const auto runtimeSnapshot = view.has_runtime_snapshot
            ? readRuntimeSnapshotDto(view.runtime_snapshot_path, &runtimeSnapshotError)
            : std::optional<contracts::RuntimeSnapshotDTO>{};
        const QString assetRoot = workspacePath(QStringLiteral("_deps/example"));
        if (graphDtoVtkEnabled) {
        for (auto* vtk : {graphFieldVtk_, graphDamageVtk_, graphLifeVtk_}) {
            if (!vtk) {
                continue;
            }
            vtk->setAssetRoot(assetRoot);
            if (runtimeSnapshot.has_value()) {
                vtk->setRuntimeSnapshot(runtimeSnapshot.value());
            }
        }
        if (!runtimeSnapshot.has_value()) {
            const QString message = runtimeSnapshotError.isEmpty()
                ? QString::fromUtf8("缺少 runtime_snapshot.json：无法把场值映射到真实飞船模型")
                : runtimeSnapshotError;
            if (graphFieldVtk_) graphFieldVtk_->clearField(message);
            if (graphDamageVtk_) graphDamageVtk_->clearField(message);
            if (graphLifeVtk_) graphLifeVtk_->clearField(message);
        }

        } else {
            const QString message = QStringLiteral("平台版 Controller 的三维云图只使用 PlatformFieldArtifact，旧 DTO result port 不再驱动渲染。");
            if (graphFieldVtk_) graphFieldVtk_->clearField(message);
            if (graphDamageVtk_) graphDamageVtk_->clearField(message);
            if (graphLifeVtk_) graphLifeVtk_->clearField(message);
        }

        std::optional<contracts::DamageForecastFrame> damageForecastForLife;

        QString parseError;
        if (const auto trajectoryJson = parsePortPayload(view.result_ports, "state.future", &parseError)) {
            const auto trajectory = trajectoryFrameFromGraphJson(*trajectoryJson);
            setLabelText(lbGraphTrajectorySummary_,
                QString::fromUtf8("弹道：status=%1，model=%2，horizon=%3s，step=%4s，samples=%5")
                    .arg(QString::fromUtf8(trajectory.status.c_str()),
                        QString::fromUtf8(trajectory.model_snapshot.model_id.c_str()),
                        formatNumber(trajectory.horizon_s),
                        formatNumber(trajectory.step_s))
                    .arg(static_cast<int>(trajectory.samples.size())));
            if (graphTrajectoryPlot_) {
                graphTrajectoryPlot_->setTrajectory(trajectory);
            }
            for (const auto& sample : trajectory.samples) {
                const double altitude = sample.lla_rad_m.size() >= 3 ? sample.lla_rad_m[2] : std::numeric_limits<double>::quiet_NaN();
                appendGraphRow(graphTrajectoryTable_, {
                    formatNumber(sample.time_s, 6),
                    formatNumber(altitude, 7),
                    formatNumber(sample.mach, 6),
                    formatNumber(sample.dynamic_pressure_pa, 7),
                    formatNumber(sample.angle_of_attack_rad, 6),
                    formatNumber(sample.normal_load_g, 6)
                });
            }
        }
        else {
            setLabelText(lbGraphTrajectorySummary_, QString::fromUtf8("弹道：%1").arg(parseError));
            if (graphTrajectoryPlot_) {
                graphTrajectoryPlot_->clear();
            }
        }

        parseError.clear();
        if (const auto fieldJson = parsePortPayload(view.result_ports, "field.forecast", &parseError)) {
            const auto fieldFrame = fieldJson->get<contracts::FieldForecastFrame>();
            const auto steps = fieldJson->value("steps", nlohmann::json::array());
            setLabelText(lbGraphFieldSummary_,
                QString::fromUtf8("多物理场：status=%1，frame=%2，steps=%3，source=%4")
                    .arg(jsonString(*fieldJson, "status"),
                        jsonString(*fieldJson, "frame_id"))
                    .arg(static_cast<int>(steps.size()))
                    .arg(jsonString(*fieldJson, "source_trajectory_frame_id")));

            struct FieldAgg {
                QString subject;
                QString field;
                QString unit;
                int stepCount = 0;
                std::size_t valueCount = 0;
                ValueStats first;
                ValueStats last;
            };
            std::map<QString, FieldAgg> fields;
            for (int stepIndex = 0; stepIndex < static_cast<int>(steps.size()); ++stepIndex) {
                const auto tensors = steps[static_cast<std::size_t>(stepIndex)].value("fields", nlohmann::json::array());
                for (const auto& tensor : tensors) {
                    const QString subject = jsonString(tensor, "subject");
                    const QString field = jsonString(tensor, "field_kind");
                    const QString key = subject + QStringLiteral("/") + field;
                    auto& agg = fields[key];
                    agg.subject = subject;
                    agg.field = field;
                    agg.unit = jsonString(tensor, "unit");
                    agg.stepCount += 1;
                    const auto values = tensor.value("values", nlohmann::json::array());
                    agg.valueCount = std::max<std::size_t>(agg.valueCount, values.size());
                    for (const auto& value : values) {
                        if (!value.is_number()) {
                            continue;
                        }
                        if (stepIndex == 0) {
                            addStats(agg.first, value.get<double>());
                        }
                        if (stepIndex == static_cast<int>(steps.size()) - 1) {
                            addStats(agg.last, value.get<double>());
                        }
                    }
                }
            }
            for (const auto& item : fields) {
                const auto& agg = item.second;
                appendGraphRow(graphFieldTable_, {
                    agg.subject,
                    agg.field + (agg.unit.isEmpty() || agg.unit == QStringLiteral("-") ? QString() : QStringLiteral(" / ") + agg.unit),
                    QString::number(agg.stepCount),
                    QString::number(static_cast<qulonglong>(agg.valueCount)),
                    statsText(agg.first),
                    statsText(agg.last)
                });
            }
            const auto vtkSubject = selectedSubject(graphFieldSubjectCombo_, contracts::SubjectType::T);
            if (graphDtoVtkEnabled && graphFieldVtk_ && runtimeSnapshot.has_value()) {
                if (const auto bundle = latestFieldBundleForSubject(fieldFrame, vtkSubject)) {
                    const auto stats = graphFieldVtk_->renderFieldBundle(
                        bundle.value(),
                        vtkSubject,
                        0,
                        QString::fromUtf8("预测场 %1").arg(subjectText(vtkSubject)));
                    if (!stats.ok) {
                        appendGraphRow(graphFieldTable_, {
                            subjectText(vtkSubject),
                            QString::fromUtf8("VTK渲染"),
                            QStringLiteral("-"),
                            QStringLiteral("-"),
                            stats.message,
                            QStringLiteral("-")
                        });
                    }
                }
                else {
                    graphFieldVtk_->clearField(QString::fromUtf8("field.forecast 中没有 subject=%1 的场值")
                        .arg(subjectText(vtkSubject)));
                }
            }
        }
        else {
            setLabelText(lbGraphFieldSummary_, QString::fromUtf8("多物理场：%1").arg(parseError));
        }

        parseError.clear();
        if (const auto damageJson = parsePortPayload(view.result_ports, "damage.forecast", &parseError)) {
            const auto damageFrame = damageJson->get<contracts::DamageForecastFrame>();
            damageForecastForLife = damageFrame;
            const auto evidence = damageJson->value("diagnostic", nlohmann::json::object())
                .value("evidence", nlohmann::json::object());
            const auto initialVars = damageJson->value("initial_state", nlohmann::json::object())
                .value("variables", nlohmann::json::array());
            const double initialDamage = initialVars.empty()
                ? 0.0
                : jsonDouble(initialVars.front(), "cumulative_damage", 0.0);
            const double maxDamage = jsonDouble(evidence, "max_damage", 0.0);
            const double threshold = jsonDouble(evidence, "failure_threshold", 1.0);
            setLabelText(lbGraphDamageSummary_,
                QString::fromUtf8("损伤：status=%1，current=%2，max_future=%3，threshold=%4，mode=%5")
                    .arg(jsonString(*damageJson, "status"),
                        formatNumber(initialDamage),
                        formatNumber(maxDamage),
                        formatNumber(threshold),
                        jsonString(initialVars.empty() ? nlohmann::json::object() : initialVars.front(), "damage_mode")));

            const auto steps = damageJson->value("steps", nlohmann::json::array());
            const auto firstExceedance = damageJson->value("first_exceedance", nlohmann::json::object());
            std::vector<double> damageTrend;
            for (const auto& step : steps) {
                const auto increments = step.value("increments", nlohmann::json::array());
                ValueStats cumulativeStats;
                ValueStats incrementStats;
                for (const auto& increment : increments) {
                    addStats(cumulativeStats, jsonDouble(increment, "cumulative_value", std::numeric_limits<double>::quiet_NaN()));
                    addStats(incrementStats, jsonDouble(increment, "increment_value", std::numeric_limits<double>::quiet_NaN()));
                }
                appendGraphRow(graphDamageTable_, {
                    formatNumber(jsonDouble(step, "time_s", 0.0), 6),
                    QString::number(static_cast<int>(increments.size())),
                    cumulativeStats.count == 0 ? QStringLiteral("-") : formatNumber(cumulativeStats.max, 6),
                    incrementStats.count == 0 ? QStringLiteral("-") : formatNumber(incrementStats.max, 6),
                    jsonString(firstExceedance, "exceeded")
                });
                if (cumulativeStats.count > 0) {
                    damageTrend.push_back(std::clamp(cumulativeStats.max, 0.0, 1.0));
                }
            }
            if (graphDamageTrend_) {
                graphDamageTrend_->setSamples(damageTrend);
            }
            const auto vtkSubject = selectedSubject(graphDamageSubjectCombo_, contracts::SubjectType::T);
            if (graphDtoVtkEnabled && graphDamageVtk_ && runtimeSnapshot.has_value()) {
                if (const auto bundle = latestDamageFieldForSubject(damageFrame, vtkSubject)) {
                    const auto stats = graphDamageVtk_->renderFieldBundle(
                        bundle.value(),
                        vtkSubject,
                        0,
                        QString::fromUtf8("累计损伤 %1").arg(subjectText(vtkSubject)),
                        QStringLiteral("damage"));
                    if (!stats.ok) {
                        appendGraphRow(graphDamageTable_, {
                            QStringLiteral("-"),
                            QStringLiteral("-"),
                            QStringLiteral("-"),
                            QStringLiteral("-"),
                            stats.message
                        });
                    }
                }
                else {
                    graphDamageVtk_->clearField(QString::fromUtf8("damage.forecast 中没有 subject=%1 的累计损伤场")
                        .arg(subjectText(vtkSubject)));
                }
            }
        }
        else {
            setLabelText(lbGraphDamageSummary_, QString::fromUtf8("损伤：%1").arg(parseError));
            if (graphDamageTrend_) {
                graphDamageTrend_->clear();
            }
        }

        parseError.clear();
        if (const auto lifeJson = parsePortPayload(view.result_ports, "life.assessment", &parseError)) {
            const auto lifeFrame = lifeJson->get<contracts::LifeAssessmentFrame>();
            const auto health = lifeJson->value("health_state", nlohmann::json::object());
            const auto diagnostic = lifeJson->value("diagnostic", nlohmann::json::object());
            setLabelText(lbGraphLifeSummary_,
                QString::fromUtf8("寿命：status=%1，RUL=%2s，first_exceed=%3s，damage=%4，reason=%5")
                    .arg(jsonString(*lifeJson, "status"),
                        formatNumber(jsonDouble(*lifeJson, "rul_s", -1.0)),
                        formatNumber(jsonDouble(*lifeJson, "first_limit_exceedance_s", -1.0)),
                        formatNumber(jsonDouble(health, "damage_index", 0.0)),
                        jsonString(*lifeJson, "reason")));
            appendGraphRow(graphLifeTable_, {QString::fromUtf8("RUL/s"), formatNumber(jsonDouble(*lifeJson, "rul_s", -1.0), 7), jsonString(diagnostic, "message")});
            appendGraphRow(graphLifeTable_, {QString::fromUtf8("首超时间/s"), formatNumber(jsonDouble(*lifeJson, "first_limit_exceedance_s", -1.0), 7), jsonString(*lifeJson, "controlling_location_id")});
            appendGraphRow(graphLifeTable_, {QString::fromUtf8("当前损伤"), formatNumber(jsonDouble(health, "damage_index", 0.0), 7), jsonString(*lifeJson, "controlling_damage_mode")});
            appendGraphRow(graphLifeTable_, {QString::fromUtf8("评估方法"), jsonString(*lifeJson, "assessment_method"), jsonString(*lifeJson, "reason")});

            if (graphLifeTrend_) {
                std::vector<double> samples;
                if (damageForecastForLife.has_value()) {
                    samples.reserve(damageForecastForLife->steps.size());
                    for (const auto& step : damageForecastForLife->steps) {
                        samples.push_back(std::max(0.0, lifeFrame.rul_s - step.time_s));
                    }
                }
                if (samples.empty()) {
                    samples.push_back(std::max(0.0, lifeFrame.rul_s));
                }
                graphLifeTrend_->setSamples(samples);
            }

            const auto vtkSubject = selectedSubject(graphLifeSubjectCombo_, contracts::SubjectType::T);
            QString lifeFieldParseError;
            std::optional<contracts::FieldBundleDTO> lifeFieldFrame;
            if (const auto lifeFieldJson = parsePortPayload(view.result_ports, "life.field", &lifeFieldParseError)) {
                lifeFieldFrame = lifeFieldJson->get<contracts::FieldBundleDTO>();
            }
            if (graphDtoVtkEnabled && graphLifeVtk_ && runtimeSnapshot.has_value()) {
                if (lifeFieldFrame.has_value()) {
                    if (const auto bundle = fieldBundleForSubject(lifeFieldFrame.value(), vtkSubject)) {
                        const auto stats = graphLifeVtk_->renderFieldBundle(
                            bundle.value(),
                            vtkSubject,
                            0,
                            QString::fromUtf8("剩余寿命场 %1").arg(subjectText(vtkSubject)),
                            QStringLiteral("s"));
                        appendGraphRow(graphLifeTable_, {
                            QString::fromUtf8("寿命场来源"),
                            QString::fromUtf8("life.field"),
                            stats.ok ? QString::fromUtf8("节点级 RUL 场，单位 s") : stats.message
                        });
                    }
                    else {
                        graphLifeVtk_->clearField(QString::fromUtf8("life.field 中没有 subject=%1 的寿命场")
                            .arg(subjectText(vtkSubject)));
                    }
                }
                else if (damageForecastForLife.has_value()) {
                    appendGraphRow(graphLifeTable_, {
                        QString::fromUtf8("life.field"),
                        QString::fromUtf8("缺失/解析失败"),
                        lifeFieldParseError.isEmpty() ? QString::fromUtf8("兼容旧 evidence 派生显示") : lifeFieldParseError
                    });
                    if (const auto bundle = deriveLifeFieldForSubject(damageForecastForLife.value(), lifeFrame, vtkSubject)) {
                        const auto stats = graphLifeVtk_->renderFieldBundle(
                            bundle.value(),
                            vtkSubject,
                            0,
                            QString::fromUtf8("剩余寿命场 %1").arg(subjectText(vtkSubject)),
                            lifeFrame.rul_s > 0.0 ? QStringLiteral("s") : QStringLiteral("fraction"));
                        appendGraphRow(graphLifeTable_, {
                            QString::fromUtf8("寿命场来源"),
                            QString::fromUtf8("damage.forecast -> life.assessment 派生"),
                            stats.ok ? QString::fromUtf8("兼容旧 evidence；正式链路应输出 life.field") : stats.message
                        });
                    }
                    else {
                        graphLifeVtk_->clearField(QString::fromUtf8("无法从 damage.forecast 派生 subject=%1 的寿命场")
                            .arg(subjectText(vtkSubject)));
                    }
                }
                else {
                    graphLifeVtk_->clearField(lifeFieldParseError.isEmpty()
                        ? QString::fromUtf8("缺少 life.field：无法显示寿命场")
                        : lifeFieldParseError);
                }
            }
        }
        else {
            setLabelText(lbGraphLifeSummary_, QString::fromUtf8("寿命：%1").arg(parseError));
            if (graphLifeTrend_) {
                graphLifeTrend_->clear();
            }
        }

        if (graphEvidenceText_) {
            QStringList lines;
            lines << QString::fromUtf8("graph_run_evidence = %1")
                .arg(QString::fromStdWString(view.graph_run_evidence_path.wstring()));
            lines << QString::fromUtf8("graph_snapshot = %1")
                .arg(QString::fromStdWString(view.graph_snapshot_path.wstring()));
            lines << QString::fromUtf8("operator_snapshot = %1")
                .arg(QString::fromStdWString(view.operator_snapshot_path.wstring()));
            lines << QString::fromUtf8("resource_lock = %1")
                .arg(QString::fromStdWString(view.resource_lock_path.wstring()));
            lines << QString::fromUtf8("graph_outputs = %1")
                .arg(QString::fromStdWString(view.graph_outputs_path.wstring()));
            lines << QString::fromUtf8("runtime_snapshot = %1 (%2)")
                .arg(QString::fromStdWString(view.runtime_snapshot_path.wstring()),
                    view.has_runtime_snapshot ? QString::fromUtf8("exists") : QString::fromUtf8("missing"));
            lines << QString::fromUtf8("workflow_timeline = %1 (%2)")
                .arg(QString::fromStdWString(view.workflow_timeline_path.wstring()),
                    view.has_workflow_timeline ? QString::fromUtf8("exists") : QString::fromUtf8("missing"));
            if (workflowTimeline && workflowTimeline->is_object()) {
                lines << QString::fromUtf8("timeline_counts = observed:%1 filter:%2 predict:%3 trajectory_steps:%4 field_steps:%5 damage_steps:%6 life_nodes:%7")
                    .arg(static_cast<qlonglong>(jsonDouble(*workflowTimeline, "observed_frame_count", 0.0)))
                    .arg(static_cast<qlonglong>(jsonDouble(*workflowTimeline, "filter_update_count", 0.0)))
                    .arg(static_cast<qlonglong>(jsonDouble(*workflowTimeline, "prediction_run_count", 0.0)))
                    .arg(static_cast<qlonglong>(jsonDouble(*workflowTimeline, "trajectory_step_count_total", 0.0)))
                    .arg(static_cast<qlonglong>(jsonDouble(*workflowTimeline, "field_step_count_total", 0.0)))
                    .arg(static_cast<qlonglong>(jsonDouble(*workflowTimeline, "damage_step_count_total", 0.0)))
                    .arg(static_cast<qlonglong>(jsonDouble(*workflowTimeline, "life_field_node_value_count", 0.0)));
            }
            lines << QStringLiteral("");
            lines << QString::fromUtf8("workflow path:");
            for (const auto& node : view.graph_nodes) {
                lines << QString::fromUtf8("  - %1 [%2] %3 -> %4")
                    .arg(QString::fromUtf8(node.node_id.c_str()),
                        QString::fromUtf8(node.operator_type.c_str()),
                        joinStdStrings(node.inputs, 8),
                        joinStdStrings(node.outputs, 8));
            }
            lines << QStringLiteral("");
            lines << QString::fromUtf8("result ports:");
            for (const auto& port : view.result_ports) {
                lines << QString::fromUtf8("  - %1 (%2 bytes)")
                    .arg(QString::fromUtf8(port.port_name.c_str()))
                    .arg(static_cast<qulonglong>(port.payload_json.size()));
            }
            graphEvidenceText_->setPlainText(lines.join(QStringLiteral("\n")));
        }
    }
    catch (const std::exception& e) {
        setLabelText(lbGraphWorkflowSummary_,
            QString::fromUtf8("完整链路读取失败：%1").arg(QString::fromUtf8(e.what())));
    }
}

// 弹窗实现（占位）
SensorExtraConfigDialog::SensorExtraConfigDialog(QWidget* parent) :QDialog(parent) {
    setWindowTitle(QString::fromUtf8("传感器额外配置"));
    auto* v = new QVBoxLayout(this);
    v->addWidget(new QLabel(QString::fromUtf8("这里放该传感器的高级参数（示例占位）"), this));
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    v->addWidget(box);
    connect(box, &QDialogButtonBox::accepted, this, [this]() { accept(); });
    connect(box, &QDialogButtonBox::rejected, this, [this]() { reject(); });
}



void EnvPredictorUI::buildAcqAndConfigFlat_()
{
    pageAcqConfig_ = new QWidget(this);
    auto* root = new QVBoxLayout(pageAcqConfig_);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);

    // ── 顶部：传感器配置与通道映射 ─────────────────────────────
    {
        auto* grp = new QGroupBox(toQStr("传感器配置与通道映射"), pageAcqConfig_);
        auto* v = new QVBoxLayout(grp);
        auto* tip = new QLabel(toQStr("配置每个通道的字段索引、单位、采样频率、缩放与偏置。"), grp);
        tip->setWordWrap(true); v->addWidget(tip);

        tblSensorMap_ = new QTableWidget(4, 8, grp);

        tblSensorMap_->setHorizontalHeaderLabels({ toQStr("通道"), toQStr("传感器"), toQStr("通道ID"), toQStr("单位"),
            toQStr("采样频率(Hz)"), toQStr("测量噪声"), toQStr("分辨率"), toQStr("状态") });
        tblSensorMap_->horizontalHeader()->setStretchLastSection(true);
        auto mkRow = [&](int r, const QString& name, int idx, const QString& unit, double hz, const QString& sc, double off) {
            tblSensorMap_->setItem(r, 0, new QTableWidgetItem(name));
            auto* cb = new QComboBox(tblSensorMap_);
            cb->addItems({ toQStr("硬件传感器1"), toQStr("虚拟传感器A"), toQStr("数据库源") });
            tblSensorMap_->setCellWidget(r, 1, cb);

            tblSensorMap_->setItem(r, 2, [&]() { QTableWidgetItem* item = new QTableWidgetItem(QString::number(idx)); item->setTextAlignment(Qt::AlignHCenter | Qt::AlignVCenter); item->setFlags(item->flags() & ~Qt::ItemIsEditable); return item; }());
            tblSensorMap_->setItem(r, 3, [&]() { QTableWidgetItem* item = new QTableWidgetItem(unit); item->setTextAlignment(Qt::AlignHCenter | Qt::AlignVCenter); item->setFlags(item->flags() & ~Qt::ItemIsEditable); return item; }());
            tblSensorMap_->setItem(r, 4, [&]() { QTableWidgetItem* item = new QTableWidgetItem(QString::number(hz)); item->setTextAlignment(Qt::AlignHCenter | Qt::AlignVCenter); item->setFlags(item->flags() & ~Qt::ItemIsEditable); return item; }());
            tblSensorMap_->setItem(r, 5, [&]() { QTableWidgetItem* item = new QTableWidgetItem(sc); item->setTextAlignment(Qt::AlignHCenter | Qt::AlignVCenter); item->setFlags(item->flags() & ~Qt::ItemIsEditable); return item; }());
            tblSensorMap_->setItem(r, 6, [&]() { QTableWidgetItem* item = new QTableWidgetItem(QString::number(off)); item->setTextAlignment(Qt::AlignHCenter | Qt::AlignVCenter); item->setFlags(item->flags() & ~Qt::ItemIsEditable); return item; }());

            QWidget* checkBoxContainer = new QWidget();
            QHBoxLayout* layout = new QHBoxLayout(checkBoxContainer);
            layout->setSpacing(5);  // 复选框之间的间距
            layout->setContentsMargins(2, 2, 2, 2);  // 容器内边距

            // 创建三个复选框
            QCheckBox* check1 = new QCheckBox();
            QCheckBox* check2 = new QCheckBox();
            QCheckBox* check3 = new QCheckBox();

            QString checkBoxStyle = "QCheckBox {"
                "color: #000000; /* 黑色字体 */"
                "font-size: 12pt; /* 字体大小 */"
                "}"
                "QCheckBox::indicator {"
                "width: 16px;"
                "height: 16px;"
                "}";

            check1->setText(toQStr("连接"));
            check2->setText(toQStr("未连接"));
            check3->setText(toQStr("故障"));

            check1->setStyleSheet(checkBoxStyle);
            check2->setStyleSheet(checkBoxStyle);
            check3->setStyleSheet(checkBoxStyle);

            // 添加到布局
            layout->addWidget(check1, 0, Qt::AlignCenter);
            layout->addWidget(check2, 0, Qt::AlignCenter);
            layout->addWidget(check3, 0, Qt::AlignCenter);
            tblSensorMap_->setCellWidget(r, 7, checkBoxContainer);
            tblSensorMap_->setColumnWidth(7, 120);
            for (size_t i = 0; i < tblSensorMap_->rowCount(); i++)
                tblSensorMap_->setRowHeight(i, 40);
            
            };
        mkRow(0, toQStr("温度 T"), 1, toQStr("°C"), 20.0, "0.0-0.2", 0.5);
        mkRow(1, toQStr("应变 E"), 2, toQStr("με"), 50.0, "0.0-0.2", 0.5);
        mkRow(2, toQStr("热流 Q"), 3, toQStr("W/m^2"), 50.0, "0.0-0.2", 0.5);
        mkRow(3, toQStr("压力 P"), 4, toQStr("Pa"), 50.0, "0.0-0.2", 0.5);
        mkRow(3, toQStr("弹道"), 5, toQStr("-"), 50.0, "0.0-0.2", 0.5);
        v->addWidget(tblSensorMap_);
        root->addWidget(grp);
    }

    // ── 中部：左右并排：系统对齐配置 | 传感器面板 ────────────────
    {
        auto* mid = new QSplitter(Qt::Horizontal, pageAcqConfig_);

        // 左：系统对齐配置
        {
            auto* grp = new QGroupBox(toQStr("系统对齐配置"), mid);
            auto* form = new QFormLayout(grp);
            spBucketMs_ = new QSpinBox(grp); spBucketMs_->setRange(0, 5000); spBucketMs_->setValue(100);
            spLingerMs_ = new QSpinBox(grp); spLingerMs_->setRange(0, 5000); spLingerMs_->setValue(600);
            cbPolicy_ = new QComboBox(grp); cbPolicy_->addItems({ toQStr("等全(AllRequired)"), toQStr("等够(AtLeastM)"), toQStr("到时发(AnyAfterLinger)") });
            spAtLeastM_ = new QSpinBox(grp); spAtLeastM_->setRange(1, 16); spAtLeastM_->setValue(2);
            form->addRow(toQStr("时间桶宽度 bucket_ms"), spBucketMs_);
            form->addRow(toQStr("迟到等待上限 linger_ms"), spLingerMs_);
            form->addRow(toQStr("输出策略"), cbPolicy_);
            form->addRow(toQStr("AtLeastM 的 M"), spAtLeastM_);
            auto* tip = new QLabel(toQStr("说明：以事件时间为主，时间桶削弱抖动；等待上限控制迟到容忍度。"), grp);
            tip->setWordWrap(true); form->addRow(tip);
            mid->addWidget(grp);
        }

        // 右：传感器面板
        {
            auto* grp = new QGroupBox(toQStr("传感器面板（含虚拟/数据库）"), mid);
            auto* lay = new QVBoxLayout(grp);

            auto* topBar = new QHBoxLayout();
            auto* btnMore = new QPushButton(toQStr("更多配置…"), grp);
            connect(btnMore, &QPushButton::clicked, this, [this] { SensorExtraConfigDialog dlg(this); dlg.exec(); });
            topBar->addWidget(btnMore); topBar->addStretch();
            lay->addLayout(topBar);

            auto* hs = new QSplitter(Qt::Horizontal, grp);
            // 左树
            auto* left = new QWidget(hs); auto* vleft = new QVBoxLayout(left);
            treeSensors_ = new QTreeView(left);
            modelSensors_ = new QStandardItemModel(treeSensors_);
            modelSensors_->setHorizontalHeaderLabels({ toQStr("类别"), toQStr("名称/地址") });
            treeSensors_->setModel(modelSensors_);
            treeSensors_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
            treeSensors_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
            vleft->addWidget(treeSensors_);
            hs->addWidget(left);
            // 右表
            auto* right = new QWidget(hs); auto* vright = new QVBoxLayout(right);
            lbSensorInfo_ = new QLabel(toQStr("请选择左侧设备查看状态与可配参数"), right);
            tblSensorParams_ = new QTableWidget(0, 2, right);
            tblSensorParams_->setHorizontalHeaderLabels({ toQStr("参数"), toQStr("值") });
            tblSensorParams_->horizontalHeader()->setStretchLastSection(true);
            vright->addWidget(lbSensorInfo_);
            vright->addWidget(tblSensorParams_);
            hs->addWidget(right);

            lay->addWidget(hs);
            mid->addWidget(grp);
        }

        mid->setStretchFactor(1, 1);
        mid->setStretchFactor(2, 2);
        root->addWidget(mid, /*stretch*/1);
    }
    ui.tabWidget_main->insertTab(0, pageAcqConfig_, QString::fromUtf8("采集系统与配置"));
    // 示例填充
    fillMockSensors_();
}

void EnvPredictorUI::fillMockSensors_()
{
    modelSensors_->removeRows(0, modelSensors_->rowCount());
    auto mk = [&](const QString& cat, const QString& name) {
        QList<QStandardItem*> row; row << new QStandardItem(cat) << new QStandardItem(name);
        modelSensors_->appendRow(row);
        };
    mk(toQStr("硬件"), "USB-SERIAL CH340 (COM7)");
    mk(toQStr("硬件"), "Env Gateway v2 (192.168.1.50:5001)");
    mk(toQStr("虚拟"), "VirtualSensor: Sine-Temp(20Hz)");
    mk(toQStr("数据库"), "DBSource: SensorsPacked");
}

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
#ifdef FLIGHTENV_USE_PLATFORM_BACKEND
            if (!runtime_initialized_) {
                applyPlatformRunConfigFromUi_();
                if (!platformRunConfigApplyOk_) {
                    RCLCPP_ERROR(envUiLogger(), "UI start aborted: platform run config was not applied.");
                    return;
                }
            }
#endif
            if (!runtime_initialized_) {
                const int init_rc = interactionData();
                if (init_rc != 0) {
                    RCLCPP_ERROR(envUiLogger(), "UI start aborted: runtime initialization failed.");
                    return;
                }
            }

            controller_backend_->resume_streaming();
#ifdef FLIGHTENV_USE_PLATFORM_BACKEND
            platformStreamingPaused_ = false;
            ui.startBtn->setText(QStringLiteral("开始/继续"));
            if (platformFrameStatusLabel_) {
                platformFrameStatusLabel_->setText(QStringLiteral("平台推流中"));
            }
#endif
        }
    } catch (const std::exception& e) {
        RCLCPP_ERROR(envUiLogger(), "UI start failed: %s", e.what());
    } catch (...) {
        RCLCPP_ERROR(envUiLogger(), "UI start failed: unknown exception");
    }
#endif

}

void EnvPredictorUI::on_pauseBtn_clicked() {//暂停

#ifdef ONLINE
    if (controller_backend_) {
        controller_backend_->pause_streaming();
#ifdef FLIGHTENV_USE_PLATFORM_BACKEND
        platformStreamingPaused_ = true;
        ui.startBtn->setText(QStringLiteral("继续"));
        if (platformFrameStatusLabel_) {
            platformFrameStatusLabel_->setText(QStringLiteral("已暂停推流，点击继续恢复"));
        }
#endif
    }
#endif
    return;
}

void EnvPredictorUI::on_resetBtn_clicked() {//复位
#ifdef FLIGHTENV_USE_PLATFORM_BACKEND
    try {
        if (controller_backend_) {
            controller_backend_->stop_online_run();
        }
    } catch (const std::exception& e) {
        RCLCPP_ERROR(envUiLogger(), "UI stop/reset failed: %s", e.what());
    } catch (...) {
        RCLCPP_ERROR(envUiLogger(), "UI stop/reset failed: unknown exception");
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
    RCLCPP_INFO(envUiLogger(), "Platform UI stop/reset requested from timeline panel.");
    return;
#endif
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
#ifdef FLIGHTENV_USE_PLATFORM_BACKEND
        if (!runtime_initialized_) {
            applyPlatformRunConfigFromUi_();
            if (!platformRunConfigApplyOk_) {
                RCLCPP_ERROR(envUiLogger(), "UI train/init aborted: platform run config was not applied.");
                return;
            }
        }
#endif
        const int init_rc = interactionData();
        if (init_rc != 0) {
            RCLCPP_ERROR(envUiLogger(), "UI train/init aborted: runtime initialization failed.");
        }
    } catch (const std::exception& e) {
        RCLCPP_ERROR(envUiLogger(), "UI train/init failed: %s", e.what());
    } catch (...) {
        RCLCPP_ERROR(envUiLogger(), "UI train/init failed: unknown exception");
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
#ifdef FLIGHTENV_USE_PLATFORM_BACKEND
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
#endif
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
                RCLCPP_WARN(envUiLogger(), "sensor replay chart update skipped: %s", e.what());
            }
        }
    }

    connect(vtkMarker, &VTKSingleDialog::allMarkersVisibilityUpdated, this, &EnvPredictorUI::redrawAllMarkers);
}
