#pragma once

#include "EnvPredictorUI.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QProcess>
#include <QSplitter>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QWidget>

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "EnvContracts/dto/DamageForecastFrame.hpp"
#include "EnvContracts/dto/FieldForecastFrame.hpp"
#include "EnvContracts/dto/LifeAssessmentFrame.hpp"
#include "EnvContracts/dto/RuntimeSnapshotDTO.hpp"
#include "EnvContracts/dto/TrajectoryPredictionFrame.hpp"
#include "EnvNodeSupport/GraphRunEvidenceReader.h"
#include "EnvNodeSupport/PlatformCatalogReader.h"
#include "EnvPredictorUiHelpers.h"
#include "module-demos/common/GraphWorkflowDisplayWidgets.h"
#include "module-demos/common/VtkModelFieldWidget.h"

namespace flightenv::platform_ui::internal {

void platformUiLogInfo(const char* format, ...);
void platformUiLogWarning(const char* format, ...);
void platformUiLogError(const char* format, ...);
bool controllerBackendCanRun();

QString platformPathText(const std::filesystem::path& path);
std::filesystem::path environmentPath(const char* name);
bool environmentFlagEnabled(const char* name, bool defaultValue);
std::filesystem::path workspaceRootFromEnvironment();
QString workspacePath(const QString& relativePath);
std::filesystem::path platformUiRunConfigPath(const std::filesystem::path& workspaceRoot);
QString findNewestSummary(const QString& relativeDir, const QString& fileName);
QJsonObject readJsonObject(const QString& path);
QString jsonNumberText(const QJsonObject& object, const QString& key, const QString& fallback = QStringLiteral("-"));
QString workspaceResolvedPath(const QString& pathOrUri);
QString workspaceDisplayPath(const QString& pathOrUri);
QString objectPackagePath(const QString& relativePath);
QString objectResourcePath(const QString& pathOrUri);
QString objectResourceDisplayPath(const QString& pathOrUri);

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

PlatformUiRunConfig loadPlatformUiRunConfig(
    const std::filesystem::path& workspaceRoot,
    const std::filesystem::path& objectRoot);
launchsupport::PlatformControllerBackendOptions platformBackendOptions(bool compileWorkflowsForRun = false);

QJsonObject platformBallisticPreview(const QJsonObject& frame);
QString platformFieldIdentityKey(const launchsupport::PlatformFieldArtifactView& field);
QString platformFieldDisplayName(const QString& rawName);
QString platformComponentDisplayName(const QString& rawName);
QString platformFieldDisplayLabelFromKey(const QString& identityKey);
QString platformFieldTitle(const launchsupport::PlatformFieldArtifactView& field);
bool isRenderablePlatformField(const launchsupport::PlatformFieldArtifactView& field);
contracts::SubjectType inferPlatformFieldSubject(
    const launchsupport::PlatformFieldArtifactView& field,
    const std::shared_ptr<const launchsupport::RuntimeViewModel>& runtimeView);
std::vector<double> readPlatformFieldArtifactValues(const std::filesystem::path& artifactPath, QString* error);

QString canonicalMetricLabel(const QString& label);
QString displayMetricLabel(const QString& label);
QString displayMetricUnit(const QString& label, const QString& unit);
bool shouldDisplaySeriesMetric(const QString& label);
bool isDisplayableScalarValue(const QJsonValue& value);
QString scalarValueText(const QJsonValue& value);

QString formatNumber(double value, int precision = 4);
QString joinStdStrings(const std::vector<std::string>& values, int maxItems = 4);
QString liveStatusText(const std::string& status);
QString liveStatusStyle(const QString& status);
QLabel* namedLabel(QWidget* parent, const char* name);
QString compactPathText(const std::string& value, int tailChars = 68);
QString jsonString(const nlohmann::json& j, const char* key, const QString& fallback = QStringLiteral("-"));
double jsonDouble(const nlohmann::json& j, const char* key, double fallback = 0.0);
std::optional<nlohmann::json> readJsonFileNlohmann(const std::filesystem::path& path, QString* error);
std::optional<nlohmann::json> parsePortPayload(
    const std::vector<launchsupport::GraphResultPortView>& ports,
    const std::string& portName,
    QString* error);

QTableWidget* makeGraphTable(QWidget* parent, const QStringList& headers);
void clearGraphTable(QTableWidget* table);
void appendGraphRow(QTableWidget* table, const QStringList& values);
void setTableText(QTableWidget* table, int row, int col, const QString& text);
void setLabelText(QLabel* label, const QString& text);
QString workflowStageLabel(const QString& stageId);
QString operatorDisplayName(const QString& operatorRef, const QString& nodeId);
bool containsString(const std::vector<std::string>& values, const QString& value);
void populateCatalogWorkbench(
    const QString& catalogPathText,
    QLabel* summaryLabel,
    QTableWidget* objectTable,
    QTableWidget* bindingTable,
    QTableWidget* modelTable);
void appendConceptualEquationRows(QTableWidget* table);
void appendConceptualOnlineEquationRows(QTableWidget* table);
void appendEquationRowsFromView(
    QTableWidget* table,
    const QString& graphName,
    const std::vector<launchsupport::GraphOperatorNodeView>& nodes);

struct ValueStats {
    std::size_t count = 0;
    double min = std::numeric_limits<double>::infinity();
    double max = -std::numeric_limits<double>::infinity();
    double sum = 0.0;
};

void addStats(ValueStats& stats, double value);
QString statsText(const ValueStats& stats);
QString subjectText(contracts::SubjectType subject);

contracts::SubjectType selectedSubject(QComboBox* combo, contracts::SubjectType fallback);
QComboBox* makeSubjectCombo(QWidget* parent, const QString& tooltip);
contracts::TrajectoryPredictionFrame trajectoryFrameFromGraphJson(const nlohmann::json& value);
std::optional<contracts::FieldBundleDTO> latestFieldBundleForSubject(
    const contracts::FieldForecastFrame& frame,
    contracts::SubjectType subject);
std::optional<contracts::FieldBundleDTO> latestDamageFieldForSubject(
    const contracts::DamageForecastFrame& frame,
    contracts::SubjectType subject);
std::optional<contracts::FieldBundleDTO> fieldBundleForSubject(
    const contracts::FieldBundleDTO& frame,
    contracts::SubjectType subject);
std::optional<contracts::FieldBundleDTO> deriveLifeFieldForSubject(
    const contracts::DamageForecastFrame& damage,
    const contracts::LifeAssessmentFrame& life,
    contracts::SubjectType subject);
std::optional<contracts::RuntimeSnapshotDTO> readRuntimeSnapshotDto(
    const std::filesystem::path& path,
    QString* error);
std::map<std::string, std::size_t> resultPortSizes(
    const std::vector<launchsupport::GraphResultPortView>& ports);
std::size_t outputBytesForPorts(
    const std::vector<std::string>& ports,
    const std::map<std::string, std::size_t>& sizes);
QString bytesText(std::size_t bytes);
QString throughputText(std::size_t bytes, std::int64_t durationMs);

} // namespace flightenv::platform_ui::internal
