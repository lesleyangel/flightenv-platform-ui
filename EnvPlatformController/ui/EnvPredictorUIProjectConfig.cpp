#include "EnvPredictorUI.h"
#include "EnvPredictorUiHelpers.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QSizePolicy>
#include <QVBoxLayout>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

using json = nlohmann::json;

json read_json_file(const QString& path)
{
    std::ifstream in(QDir::toNativeSeparators(path).toStdString());
    if (!in) {
        throw std::runtime_error("failed to open JSON: " + path.toStdString());
    }
    json j;
    in >> j;
    return j;
}

QString decode_process_output(const QByteArray& bytes)
{
    QString text = QString::fromUtf8(bytes);
    if (text.contains(QChar(0xfffd))) {
        text = QString::fromLocal8Bit(bytes);
    }
    return text;
}

void write_json_file(const QString& path, const json& value)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    std::ofstream out(QDir::toNativeSeparators(path).toStdString());
    if (!out) {
        throw std::runtime_error("failed to write JSON: " + path.toStdString());
    }
    out << value.dump(2) << '\n';
}

QString native_path(const QString& path)
{
    return QDir::toNativeSeparators(path);
}

QString model_ref_path_text(const json& project)
{
    if (!project.contains("model_ref") || !project.at("model_ref").is_object()) {
        return {};
    }
    return QString::fromStdString(project.at("model_ref").value("path", std::string{}));
}

QString resolve_model_path(const QString& projectPath, const json& project)
{
    const QString ref = model_ref_path_text(project);
    if (ref.trimmed().isEmpty()) {
        return native_path(QFileInfo(projectPath).absoluteDir().filePath("launcher_local_test_model.ui.json"));
    }
    QFileInfo refInfo(ref);
    if (refInfo.isAbsolute()) {
        return native_path(refInfo.absoluteFilePath());
    }
    return native_path(QFileInfo(projectPath).absoluteDir().filePath(ref));
}

QString relative_path_from_project(const QString& projectPath, const QString& targetPath)
{
    const QDir base(QFileInfo(projectPath).absolutePath());
    return QDir::toNativeSeparators(base.relativeFilePath(targetPath));
}

json load_model_json(const QString& projectPath, const json& project)
{
    const QString modelPath = resolve_model_path(projectPath, project);
    if (QFileInfo::exists(modelPath)) {
        return read_json_file(modelPath);
    }
    if (project.contains("model") && project.at("model").is_object()) {
        return project.at("model");
    }
    if (project.contains("model_ref") && project.at("model_ref").is_object()) {
        const auto& ref = project.at("model_ref");
        if (ref.contains("json_text") && ref.at("json_text").is_string()) {
            return json::parse(ref.at("json_text").get<std::string>());
        }
    }
    return json::object();
}

QString vector_text(const std::vector<int>& values)
{
    QStringList parts;
    for (const int value : values) {
        parts << QString::number(value);
    }
    return QStringLiteral("[%1]").arg(parts.join(QStringLiteral(", ")));
}

std::vector<int> parse_int_list(QString text)
{
    text = text.trimmed();
    text.replace('[', ' ');
    text.replace(']', ' ');
    text.replace(';', ',');
    text.replace(QStringLiteral("，"), QStringLiteral(","));
    std::vector<int> values;
    for (const QString& token : text.split(',', Qt::SkipEmptyParts)) {
        bool ok = false;
        const int value = token.trimmed().toInt(&ok);
        if (ok) {
            values.push_back(value);
        }
    }
    if (values.empty()) {
        bool ok = false;
        const int value = text.toInt(&ok);
        if (ok) {
            values.push_back(value);
        }
    }
    return values;
}

std::vector<int> object_ints(const json& object, const std::vector<std::string>& keys)
{
    std::vector<int> values;
    values.reserve(keys.size());
    for (const std::string& key : keys) {
        values.push_back(object.value(key, 0));
    }
    return values;
}

void set_line(QLineEdit* edit, const QString& text)
{
    if (edit) {
        edit->setText(text);
    }
}

void set_line(QLineEdit* edit, const double value)
{
    if (edit) {
        edit->setText(QString::number(value, 'g', 12));
    }
}

void set_line(QLineEdit* edit, const int value)
{
    if (edit) {
        edit->setText(QString::number(value));
    }
}

void set_check(QCheckBox* check, const bool value)
{
    if (check) {
        check->setChecked(value);
    }
}

QString hidden_layers_text(const json& bpnn)
{
    if (!bpnn.contains("hidden_layers") || !bpnn.at("hidden_layers").is_array()) {
        return QStringLiteral("[20, 20]");
    }
    std::vector<int> values;
    for (const auto& item : bpnn.at("hidden_layers")) {
        values.push_back(item.get<int>());
    }
    return vector_text(values);
}

double nullable_double_or(const json& object, const char* key, const double fallback)
{
    if (!object.contains(key) || object.at(key).is_null()) {
        return fallback;
    }
    return object.at(key).get<double>();
}

QString nullable_double_text(const json& object, const char* key)
{
    if (!object.contains(key) || object.at(key).is_null()) {
        return QStringLiteral("\\");
    }
    return QString::number(object.at(key).get<double>(), 'g', 12);
}

json* find_mapping(json& model, const std::string& name)
{
    auto& mappings = model["pred"]["mappings"];
    if (!mappings.is_array()) {
        mappings = json::array();
    }
    for (auto& item : mappings) {
        if (item.is_object() && item.value("name", std::string{}) == name) {
            return &item;
        }
    }
    mappings.push_back({{"name", name}, {"model", json::object()}});
    return &mappings.back();
}

const json* find_mapping_const(const json& model, const std::string& name)
{
    if (!model.contains("pred") ||
        !model.at("pred").contains("mappings") ||
        !model.at("pred").at("mappings").is_array()) {
        return nullptr;
    }
    for (const auto& item : model.at("pred").at("mappings")) {
        if (item.is_object() && item.value("name", std::string{}) == name) {
            return &item;
        }
    }
    return nullptr;
}

void set_subject_checks(const std::vector<std::string>& values,
                        QCheckBox* p,
                        QCheckBox* k,
                        QCheckBox* s,
                        QCheckBox* t)
{
    auto has = [&](const char* key) {
        return std::find(values.begin(), values.end(), key) != values.end();
    };
    set_check(p, has("P"));
    set_check(k, has("K"));
    set_check(s, has("S"));
    set_check(t, has("T"));
}

std::vector<std::string> subject_checks(QCheckBox* p, QCheckBox* k, QCheckBox* s, QCheckBox* t)
{
    std::vector<std::string> values;
    if (p && p->isChecked()) values.push_back("P");
    if (k && k->isChecked()) values.push_back("K");
    if (s && s->isChecked()) values.push_back("S");
    if (t && t->isChecked()) values.push_back("T");
    return values;
}

bool is_legacy_training_control_name(const QString& name)
{
    static const QStringList prefixes = {
        QStringLiteral("lineEdit_BPNNInvert_"),
        QStringLiteral("lineEdit_PODInvert_"),
        QStringLiteral("lineEdit_BPNNForecast_"),
        QStringLiteral("lineEdit_Invert_"),
        QStringLiteral("lineEdit_Forecast_"),
        QStringLiteral("comboBox_InvertType"),
        QStringLiteral("comboBox_ForecastType"),
        QStringLiteral("checkBox_BPNNInvert"),
        QStringLiteral("checkBox_BPNNForecast"),
        QStringLiteral("checkBox_PODInvert"),
        QStringLiteral("checkBox_Invert"),
        QStringLiteral("checkBox_Forecast"),
        QStringLiteral("checkBox_IOInvert"),
        QStringLiteral("checkBox_IOForecast")
    };

    for (const QString& prefix : prefixes) {
        if (name.startsWith(prefix)) {
            return true;
        }
    }
    return false;
}

void polish_legacy_training_layout(QLayout* layout)
{
    if (!layout) {
        return;
    }
    layout->setSpacing(std::max(layout->spacing(), 6));
    layout->setSizeConstraint(QLayout::SetMinimumSize);

    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    layout->getContentsMargins(&left, &top, &right, &bottom);
    layout->setContentsMargins(
        std::max(left, 6),
        std::max(top, 6),
        std::max(right, 6),
        std::max(bottom, 6));

    if (auto* grid = qobject_cast<QGridLayout*>(layout)) {
        grid->setHorizontalSpacing(std::max(grid->horizontalSpacing(), 8));
        grid->setVerticalSpacing(std::max(grid->verticalSpacing(), 6));
    }
}

void polish_legacy_training_control(QWidget* control)
{
    if (!control) {
        return;
    }
    control->setMinimumHeight(22);
    control->setMaximumHeight(28);
    QSizePolicy policy = control->sizePolicy();
    policy.setHorizontalPolicy(QSizePolicy::Expanding);
    policy.setVerticalPolicy(QSizePolicy::Fixed);
    control->setSizePolicy(policy);
}

int legacy_training_group_target_height(QGroupBox* box)
{
    if (!box) {
        return 0;
    }

    const int lineEdits = box->findChildren<QLineEdit*>().size();
    const int combos = box->findChildren<QComboBox*>().size();
    const int checks = box->findChildren<QCheckBox*>().size();
    const int controls = lineEdits + combos + checks;

    // Do not force the large model blocks to grow.  They are page sections and
    // making them tall pushes the training log panel off 1080p screens.
    if (controls >= 16) {
        return 0;
    }
    if (controls >= 10) {
        return 185;
    }
    if (controls >= 6) {
        return 135;
    }
    if (controls >= 3) {
        return 90;
    }
    return 0;
}

struct PodUi {
    QLineEdit* base_order = nullptr;
    QLineEdit* energy_ratio = nullptr;
    QLineEdit* max_iterations = nullptr;
    QLineEdit* regularization = nullptr;
    QLineEdit* save_dir = nullptr;
    QLineEdit* load_dir = nullptr;
    QCheckBox* overwrite = nullptr;
    QCheckBox* strict = nullptr;
};

struct BpnnUi {
    QLineEdit* learning_rate = nullptr;
    QLineEdit* momentum = nullptr;
    QLineEdit* hidden_layers = nullptr;
    QLineEdit* max_epochs = nullptr;
    QLineEdit* error_threshold = nullptr;
    QLineEdit* batch_size = nullptr;
    QLineEdit* split = nullptr;
    QLineEdit* seed = nullptr;
    QCheckBox* use_gpu = nullptr;
    QCheckBox* input_normalize = nullptr;
    QCheckBox* output_normalize = nullptr;
    QLineEdit* save_dir = nullptr;
    QLineEdit* load_dir = nullptr;
    QCheckBox* overwrite = nullptr;
    QCheckBox* strict = nullptr;
};

void load_pod(const json& model, const char* subject, const PodUi& ui)
{
    const json train = model.value("pod", json::object())
        .value("train", json::object())
        .value(subject, json::object());
    const json use = model.value("pod", json::object())
        .value("use", json::object())
        .value(subject, json::object());
    set_line(ui.base_order, train.value("base_order", 3));
    set_line(ui.energy_ratio, nullable_double_text(train, "energy_ratio"));
    set_line(ui.max_iterations, train.value("max_iterations", 500));
    set_line(ui.regularization, train.value("regularization", 0.01));
    set_line(ui.save_dir, QString::fromStdString(train.value("save_dir", std::string("pod_text_dump"))));
    set_line(ui.load_dir, QString::fromStdString(use.value("load_dir", std::string("pod_text_dump"))));
    set_check(ui.overwrite, train.value("overwrite", false));
    set_check(ui.strict, use.value("strict", false));
}

void save_pod(json& model, const char* subject, const PodUi& ui)
{
    json train;
    train["base_order"] = ui.base_order ? ui.base_order->text().toInt() : 3;
    const QString energy = ui.energy_ratio ? ui.energy_ratio->text().trimmed() : QString();
    if (energy.isEmpty() || energy == QStringLiteral("\\") || energy.compare(QStringLiteral("null"), Qt::CaseInsensitive) == 0) {
        train["energy_ratio"] = nullptr;
    } else {
        train["energy_ratio"] = energy.toDouble();
    }
    train["max_iterations"] = ui.max_iterations ? ui.max_iterations->text().toInt() : 500;
    train["regularization"] = ui.regularization ? ui.regularization->text().toDouble() : 0.01;
    train["save_dir"] = ui.save_dir ? ui.save_dir->text().toStdString() : std::string("pod_text_dump");
    train["overwrite"] = ui.overwrite ? ui.overwrite->isChecked() : false;

    json use;
    use["load_dir"] = ui.load_dir ? ui.load_dir->text().toStdString() : std::string("pod_text_dump");
    use["strict"] = ui.strict ? ui.strict->isChecked() : false;

    model["pod"]["train"][subject] = train;
    model["pod"]["use"][subject] = use;
}

void load_bpnn_mapping(const json& model, const char* mappingName, const BpnnUi& ui)
{
    const json* mapping = find_mapping_const(model, mappingName);
    if (!mapping) {
        return;
    }
    const json cfg = mapping->value("model", json::object());
    const json bpnn = cfg.value("bpnn", json::object());
    set_line(ui.learning_rate, bpnn.value("learning_rate", 0.01));
    set_line(ui.momentum, bpnn.value("momentum", 0.95));
    set_line(ui.hidden_layers, hidden_layers_text(bpnn));
    set_line(ui.max_epochs, bpnn.value("max_epochs", 10000));
    set_line(ui.error_threshold, bpnn.value("error_threshold", 0.0));
    set_line(ui.batch_size, bpnn.value("batch_size", 32));
    set_line(ui.split, bpnn.contains("test_split")
        ? bpnn.value("test_split", 0.1)
        : bpnn.value("val_split", 0.1));
    set_line(ui.seed, bpnn.value("seed", 42));
    set_check(ui.use_gpu, bpnn.value("use_gpu", false));
    set_check(ui.input_normalize, cfg.value("input_normalize", true));
    set_check(ui.output_normalize, cfg.value("output_normalize", true));
    set_line(ui.save_dir, QString::fromStdString(cfg.value("train_io", json::object()).value("save_dir", std::string("pred_train_model"))));
    set_line(ui.load_dir, QString::fromStdString(cfg.value("use_io", json::object()).value("load_dir", std::string("pred_train_model"))));
    set_check(ui.overwrite, cfg.value("train_io", json::object()).value("overwrite", false));
    set_check(ui.strict, cfg.value("use_io", json::object()).value("strict", false));
}

void save_bpnn_mapping(json& model, const char* mappingName, const BpnnUi& ui)
{
    json* mapping = find_mapping(model, mappingName);
    json cfg = mapping->value("model", json::object());
    json bpnn = cfg.value("bpnn", json::object());
    bpnn["learning_rate"] = ui.learning_rate ? ui.learning_rate->text().toDouble() : 0.01;
    bpnn["momentum"] = ui.momentum ? ui.momentum->text().toDouble() : 0.95;
    bpnn["hidden_layers"] = stringToVector(ui.hidden_layers ? ui.hidden_layers->text().toStdString() : std::string("[20,20]"));
    bpnn["max_epochs"] = ui.max_epochs ? ui.max_epochs->text().toInt() : 10000;
    bpnn["error_threshold"] = ui.error_threshold ? ui.error_threshold->text().toDouble() : 0.0;
    bpnn["batch_size"] = ui.batch_size ? ui.batch_size->text().toInt() : 32;
    if (bpnn.contains("val_split") && !bpnn.contains("test_split")) {
        bpnn["val_split"] = ui.split ? ui.split->text().toDouble() : 0.1;
    } else {
        bpnn["test_split"] = ui.split ? ui.split->text().toDouble() : 0.1;
    }
    bpnn["use_gpu"] = ui.use_gpu ? ui.use_gpu->isChecked() : false;
    bpnn["seed"] = ui.seed ? ui.seed->text().toInt() : 42;

    cfg["type"] = "BPNN";
    cfg["bpnn"] = bpnn;
    cfg["input_normalize"] = ui.input_normalize ? ui.input_normalize->isChecked() : true;
    cfg["output_normalize"] = ui.output_normalize ? ui.output_normalize->isChecked() : true;
    cfg["train_io"]["save_dir"] = ui.save_dir ? ui.save_dir->text().toStdString() : std::string("pred_train_model");
    cfg["train_io"]["overwrite"] = ui.overwrite ? ui.overwrite->isChecked() : false;
    cfg["use_io"]["load_dir"] = ui.load_dir ? ui.load_dir->text().toStdString() : std::string("pred_train_model");
    cfg["use_io"]["strict"] = ui.strict ? ui.strict->isChecked() : false;
    (*mapping)["model"] = cfg;
}

std::vector<std::string> string_array(const json& object, const char* key)
{
    std::vector<std::string> values;
    if (!object.contains(key) || !object.at(key).is_array()) {
        return values;
    }
    for (const auto& item : object.at(key)) {
        values.push_back(item.get<std::string>());
    }
    return values;
}

} // namespace

QString EnvPredictorUI::workspaceRootPath_() const
{
    const QString envRoot = qEnvironmentVariable("FLIGHTENV_WORKSPACE_ROOT").trimmed();
    if (!envRoot.isEmpty()) {
        return native_path(QDir(envRoot).absolutePath());
    }

    const QStringList starts = {
        QDir::currentPath(),
        QCoreApplication::applicationDirPath()
    };
    for (const QString& start : starts) {
        QDir dir(start);
        for (int depth = 0; depth < 8; ++depth) {
            if (dir.exists(QStringLiteral("flightenv-platform-ui")) || dir.exists(QStringLiteral("_deps"))) {
                return native_path(dir.absolutePath());
            }
            if (!dir.cdUp()) {
                break;
            }
        }
    }
    return native_path(QDir::currentPath());
}

QString EnvPredictorUI::resolveWorkspacePath_(const QString& path) const
{
    QString text = QDir::fromNativeSeparators(path.trimmed());
    if (text.isEmpty()) {
        return {};
    }

    const QFileInfo directInfo(text);
    if (directInfo.isAbsolute() && directInfo.exists()) {
        return native_path(directInfo.absoluteFilePath());
    }

    const QStringList portableRoots = {
        QStringLiteral("_deps"),
        QStringLiteral("_local_artifacts"),
        QStringLiteral("flightenv-object-reentry-vehicle"),
        QStringLiteral("flightenv-platform-pdk")
    };
    for (const QString& rootName : portableRoots) {
        const QString marker = QStringLiteral("/") + rootName + QStringLiteral("/");
        const int markerIndex = text.indexOf(marker, 0, Qt::CaseInsensitive);
        if (markerIndex >= 0) {
            const QString relative = text.mid(markerIndex + 1);
            return native_path(QDir(workspaceRootPath_()).filePath(relative));
        }
        if (text.startsWith(rootName + QStringLiteral("/"), Qt::CaseInsensitive)) {
            return native_path(QDir(workspaceRootPath_()).filePath(text));
        }
    }

    if (directInfo.isAbsolute()) {
        return native_path(text);
    }
    return native_path(QDir(workspaceRootPath_()).filePath(text));
}

QString EnvPredictorUI::displayWorkspacePath_(const QString& path) const
{
    QString text = QDir::fromNativeSeparators(path.trimmed());
    if (text.isEmpty()) {
        return {};
    }

    const QStringList portableRoots = {
        QStringLiteral("_deps"),
        QStringLiteral("_local_artifacts"),
        QStringLiteral("flightenv-object-reentry-vehicle"),
        QStringLiteral("flightenv-platform-pdk")
    };
    for (const QString& rootName : portableRoots) {
        const QString marker = QStringLiteral("/") + rootName + QStringLiteral("/");
        const int markerIndex = text.indexOf(marker, 0, Qt::CaseInsensitive);
        if (markerIndex >= 0) {
            return native_path(text.mid(markerIndex + 1));
        }
        if (text.startsWith(rootName + QStringLiteral("/"), Qt::CaseInsensitive)) {
            return native_path(text);
        }
    }

    const QFileInfo info(resolveWorkspacePath_(text));
    const QDir root(workspaceRootPath_());
    const QString relative = root.relativeFilePath(info.absoluteFilePath());
    if (!relative.startsWith(QStringLiteral(".."))) {
        return native_path(relative);
    }
    return native_path(text);
}

QString EnvPredictorUI::defaultTrainingProjectConfigPath_() const
{
    return native_path(QStringLiteral("_deps/example/launcher_local_test_cfg.json"));
}

QString EnvPredictorUI::defaultTrainingOutputRootPath_() const
{
    return native_path(QStringLiteral("_local_artifacts/platform-ui-training"));
}

QString EnvPredictorUI::currentTrainingProjectConfigPath_() const
{
    const QString text = trainingProjectConfigEdit_ ? trainingProjectConfigEdit_->text().trimmed() : QString();
    return resolveWorkspacePath_(text.isEmpty() ? defaultTrainingProjectConfigPath_() : text);
}

QString EnvPredictorUI::currentTrainingOutputRootPath_() const
{
    const QString text = trainingOutputRootEdit_ ? trainingOutputRootEdit_->text().trimmed() : QString();
    return resolveWorkspacePath_(text.isEmpty() ? defaultTrainingOutputRootPath_() : text);
}

void EnvPredictorUI::setTrainingStatus_(const QString& text)
{
    if (trainingStatusLabel_) {
        trainingStatusLabel_->setText(text);
    }
}

void EnvPredictorUI::appendTrainingLog_(const QString& text)
{
    if (!trainingLogEdit_ || text.isEmpty()) {
        return;
    }
    QString normalized = text;
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    normalized.replace(QChar('\r'), QChar('\n'));
    trainingLogEdit_->appendPlainText(normalized.trimmed());
}

void EnvPredictorUI::buildTrainingCliControls_(QVBoxLayout* layout, QWidget* parent)
{
    if (!layout) {
        return;
    }

    // 这里仅搭建训练 CLI 的轻量配置面板。真正读写 cfg、启动进程和解析日志
    // 放在本文件下方的 load/save/start/finish 函数里，避免再塞回主 UI 文件。
    auto* form = new QWidget(parent);
    auto* grid = new QGridLayout(form);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(6);

    int row = 0;
    auto addRow = [&](const QString& label, QLineEdit*& edit) {
        auto* name = new QLabel(label, form);
        edit = new QLineEdit(form);
        grid->addWidget(name, row, 0);
        grid->addWidget(edit, row, 1, 1, 3);
        ++row;
    };

    addRow(QStringLiteral("训练 cfg"), trainingProjectConfigEdit_);
    addRow(QStringLiteral("输出目录"), trainingOutputRootEdit_);
    addRow(QStringLiteral("数据库"), trainingDbPathEdit_);
    addRow(QStringLiteral("训练任务 ID"), trainingTaskIdsEdit_);
    addRow(QStringLiteral("场 ID"), trainingFieldIdsEdit_);
    addRow(QStringLiteral("传感器 ID"), trainingSensorIdsEdit_);
    addRow(QStringLiteral("Base IDs"), trainingBaseIdsEdit_);

    trainingUsePodDatabaseCheck_ = new QCheckBox(QStringLiteral("使用数据库 POD"), form);
    grid->addWidget(trainingUsePodDatabaseCheck_, row, 1, 1, 3);
    ++row;

    auto* buttons = new QWidget(form);
    auto* buttonLayout = new QHBoxLayout(buttons);
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    buttonLayout->setSpacing(6);
    trainingLoadConfigButton_ = new QPushButton(QStringLiteral("读取 cfg"), buttons);
    trainingSaveConfigButton_ = new QPushButton(QStringLiteral("保存 cfg"), buttons);
    trainingSaveAsConfigButton_ = new QPushButton(QStringLiteral("另存 cfg"), buttons);
    trainingDbBrowseButton_ = new QPushButton(QStringLiteral("选择数据库"), buttons);
    trainingOutputBrowseButton_ = new QPushButton(QStringLiteral("选择输出目录"), buttons);
    trainingStartButton_ = new QPushButton(QStringLiteral("开始训练"), buttons);
    trainingClearLogButton_ = new QPushButton(QStringLiteral("清空日志"), buttons);
    for (QPushButton* button : {
        trainingLoadConfigButton_,
        trainingSaveConfigButton_,
        trainingSaveAsConfigButton_,
        trainingDbBrowseButton_,
        trainingOutputBrowseButton_,
        trainingStartButton_,
        trainingClearLogButton_
    }) {
        buttonLayout->addWidget(button);
    }
    grid->addWidget(buttons, row, 0, 1, 4);
    ++row;

    trainingStatusLabel_ = new QLabel(QStringLiteral("训练配置待加载"), form);
    trainingStatusLabel_->setWordWrap(true);
    grid->addWidget(trainingStatusLabel_, row, 0, 1, 4);
    ++row;

    trainingLogEdit_ = new QPlainTextEdit(form);
    trainingLogEdit_->setReadOnly(true);
    trainingLogEdit_->setMinimumHeight(140);
    trainingLogEdit_->setPlaceholderText(QStringLiteral("训练进程输出会实时显示在这里"));
    grid->addWidget(trainingLogEdit_, row, 0, 1, 4);

    trainingProjectConfigEdit_->setText(defaultTrainingProjectConfigPath_());
    trainingOutputRootEdit_->setText(defaultTrainingOutputRootPath_());
    trainingTaskIdsEdit_->setText(QStringLiteral("[1,2]"));
    trainingFieldIdsEdit_->setText(QStringLiteral("[4,5,3,1]"));
    trainingSensorIdsEdit_->setText(QStringLiteral("[8,9,7,6]"));
    trainingBaseIdsEdit_->setText(QStringLiteral("{}"));
    if (ui.lineEdit_DDFN_1 && !ui.lineEdit_DDFN_1->text().trimmed().isEmpty()) {
        trainingDbPathEdit_->setText(displayWorkspacePath_(ui.lineEdit_DDFN_1->text().trimmed()));
    }

    connect(trainingLoadConfigButton_, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(
            this,
            QStringLiteral("读取训练 cfg"),
            currentTrainingProjectConfigPath_(),
            QStringLiteral("JSON (*.json);;All files (*.*)"));
        if (!path.isEmpty()) {
            trainingProjectConfigEdit_->setText(displayWorkspacePath_(path));
            loadTrainingProjectConfig_(path);
        }
    });
    connect(trainingSaveConfigButton_, &QPushButton::clicked, this, [this]() {
        saveTrainingProjectConfig_(currentTrainingProjectConfigPath_());
    });
    connect(trainingSaveAsConfigButton_, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getSaveFileName(
            this,
            QStringLiteral("另存训练 cfg"),
            currentTrainingProjectConfigPath_(),
            QStringLiteral("JSON (*.json);;All files (*.*)"));
        if (!path.isEmpty()) {
            trainingProjectConfigEdit_->setText(displayWorkspacePath_(path));
            saveTrainingProjectConfig_(path);
        }
    });
    connect(trainingDbBrowseButton_, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(
            this,
            QStringLiteral("选择训练数据库"),
            trainingDbPathEdit_ ? resolveWorkspacePath_(trainingDbPathEdit_->text()) : workspaceRootPath_(),
            QStringLiteral("SQLite DB (*.db *.sqlite *.sqlite3);;All files (*.*)"));
        if (!path.isEmpty() && trainingDbPathEdit_) {
            trainingDbPathEdit_->setText(displayWorkspacePath_(path));
        }
    });
    connect(trainingOutputBrowseButton_, &QPushButton::clicked, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(
            this,
            QStringLiteral("选择训练输出目录"),
            currentTrainingOutputRootPath_());
        if (!dir.isEmpty() && trainingOutputRootEdit_) {
            trainingOutputRootEdit_->setText(displayWorkspacePath_(dir));
        }
    });
    connect(trainingStartButton_, &QPushButton::clicked, this, &EnvPredictorUI::startTrainingCli_);
    connect(trainingClearLogButton_, &QPushButton::clicked, this, [this]() {
        if (trainingLogEdit_) {
            trainingLogEdit_->clear();
        }
    });

    layout->addWidget(form);
    if (QFileInfo::exists(currentTrainingProjectConfigPath_())) {
        loadTrainingProjectConfig_(currentTrainingProjectConfigPath_());
    }
}

void EnvPredictorUI::hideLegacyInlineTrainingButtons_()
{
    const QVector<QPushButton*> legacyButtons = {
        ui.pushButton, ui.pushButton_2, ui.pushButton_4, ui.pushButton_5, ui.pushButton_8,
        ui.pushButton_9, ui.pushButton_10, ui.pushButton_12, ui.pushButton_13, ui.pushButton_14,
        ui.pushButton_15, ui.pushButton_17, ui.pushButton_19, ui.pushButton_20,
        ui.pushButton_23, ui.pushButton_24, ui.pushButton_25, ui.pushButton_26,
        ui.pushButton_28, ui.pushButton_30, ui.pushButton_31,
        ui.pushButton_37, ui.pushButton_51, ui.pushButton_52, ui.pushButton_55,
        ui.pushButton_64, ui.pushButton_65, ui.pushButton_66
    };

    for (QPushButton* button : legacyButtons) {
        if (button) {
            button->hide();
            button->setEnabled(false);
        }
    }
}

void EnvPredictorUI::polishLegacyTrainingForms_()
{
    // The legacy model-training pages are still generated by the old .ui file.
    // Give their parameter controls stable row heights so Qt cannot squeeze rows
    // into each other on 1080p screens or packaged runtimes with different fonts.
    QSet<QGroupBox*> touchedGroups;

    auto visitControl = [&](QWidget* control) {
        if (!control || !is_legacy_training_control_name(control->objectName())) {
            return;
        }

        polish_legacy_training_control(control);

        for (QWidget* parent = control->parentWidget(); parent; parent = parent->parentWidget()) {
            polish_legacy_training_layout(parent->layout());

            if (auto* group = qobject_cast<QGroupBox*>(parent)) {
                touchedGroups.insert(group);
            }

            if (auto* scroll = qobject_cast<QScrollArea*>(parent)) {
                scroll->setWidgetResizable(true);
            }

            if (parent == ui.tab) {
                break;
            }
        }
    };

    for (QLineEdit* edit : findChildren<QLineEdit*>()) {
        visitControl(edit);
    }
    for (QComboBox* combo : findChildren<QComboBox*>()) {
        visitControl(combo);
    }
    for (QCheckBox* check : findChildren<QCheckBox*>()) {
        visitControl(check);
    }

    for (QGroupBox* group : touchedGroups) {
        if (!group) {
            continue;
        }

        polish_legacy_training_layout(group->layout());
        for (QLayout* layout : group->findChildren<QLayout*>()) {
            polish_legacy_training_layout(layout);
        }
        for (QLineEdit* edit : group->findChildren<QLineEdit*>()) {
            polish_legacy_training_control(edit);
        }
        for (QComboBox* combo : group->findChildren<QComboBox*>()) {
            polish_legacy_training_control(combo);
        }
        for (QCheckBox* check : group->findChildren<QCheckBox*>()) {
            polish_legacy_training_control(check);
        }

        if (group->layout()) {
            group->layout()->activate();
        }

        int minimumHeight = std::max(group->minimumHeight(), legacy_training_group_target_height(group));
        if (minimumHeight > 0) {
            minimumHeight = std::max(minimumHeight, group->sizeHint().height());
        }
        QSizePolicy policy = group->sizePolicy();
        policy.setHorizontalPolicy(QSizePolicy::Expanding);
        policy.setVerticalPolicy(QSizePolicy::Preferred);
        group->setSizePolicy(policy);

        if (minimumHeight > 0 && group->minimumHeight() < minimumHeight) {
            group->setMinimumHeight(minimumHeight);
        }
    }
}

bool EnvPredictorUI::loadTrainingProjectConfig_(const QString& path)
{
    try {
        const QString projectPath = resolveWorkspacePath_(path);
        json project = read_json_file(projectPath);
        json model = load_model_json(projectPath, project);

        if (trainingProjectConfigEdit_) {
            trainingProjectConfigEdit_->setText(displayWorkspacePath_(projectPath));
        }

        const json data = model.value("data", json::object());
        const QString dbPath = QString::fromStdString(data.value("db_path", std::string{}));
        const QString dbDisplayPath = displayWorkspacePath_(dbPath);
        set_line(ui.lineEdit_DDFN_1, dbDisplayPath);
        set_line(trainingDbPathEdit_, dbDisplayPath);
        const std::vector<int> trainIds = data.contains("task_train_ids") && data.at("task_train_ids").is_array()
            ? data.at("task_train_ids").get<std::vector<int>>()
            : std::vector<int>{data.value("task_train_id", 1)};
        const QString trainIdsText = vector_text(trainIds);
        const QString fieldIdsText = vector_text(object_ints(data.value("field_ids", json::object()), {"K", "P", "S", "T"}));
        const QString sensorIdsText = vector_text(object_ints(data.value("sensor_ids", json::object()), {"K", "P", "S", "T"}));
        set_line(ui.lineEdit_DDFN_2, trainIdsText);
        set_line(ui.lineEdit_DDFN_3, fieldIdsText);
        set_line(ui.lineEdit_DDFN_4, sensorIdsText);
        set_line(trainingTaskIdsEdit_, trainIdsText);
        set_line(trainingFieldIdsEdit_, fieldIdsText);
        set_line(trainingSensorIdsEdit_, sensorIdsText);
        if (data.contains("base_ids")) {
            const QString baseIdsText = QString::fromStdString(data.at("base_ids").dump());
            set_line(ui.lineEdit_DDFN_5, baseIdsText);
            set_line(trainingBaseIdsEdit_, baseIdsText);
        } else {
            set_line(ui.lineEdit_DDFN_5, QString());
            set_line(trainingBaseIdsEdit_, QString());
        }
        const bool usePodDatabase = data.value("use_pod_database", false);
        set_check(ui.checkBox_DDFN_1, usePodDatabase);
        set_check(trainingUsePodDatabaseCheck_, usePodDatabase);

        load_pod(model, "P", {ui.lineEdit_PODInvert_1, ui.lineEdit_PODInvert_2, ui.lineEdit_PODInvert_3, ui.lineEdit_PODInvert_4, ui.lineEdit_PODInvert_5, ui.lineEdit_PODInvert_6, ui.checkBox_PODInvertCover, ui.checkBox_PODInvertVerify});
        load_pod(model, "K", {ui.lineEdit_PODInvert_7, ui.lineEdit_PODInvert_8, ui.lineEdit_PODInvert_9, ui.lineEdit_PODInvert_10, ui.lineEdit_PODInvert_11, ui.lineEdit_PODInvert_12, ui.checkBox_PODInvertCover_2, ui.checkBox_PODInvertVerify_2});
        load_pod(model, "S", {ui.lineEdit_PODInvert_13, ui.lineEdit_PODInvert_14, ui.lineEdit_PODInvert_15, ui.lineEdit_PODInvert_16, ui.lineEdit_PODInvert_17, ui.lineEdit_PODInvert_18, ui.checkBox_PODInvertCover_3, ui.checkBox_PODInvertVerify_3});
        load_pod(model, "T", {ui.lineEdit_PODInvert_19, ui.lineEdit_PODInvert_20, ui.lineEdit_PODInvert_21, ui.lineEdit_PODInvert_22, ui.lineEdit_PODInvert_23, ui.lineEdit_PODInvert_24, ui.checkBox_PODInvertCover_4, ui.checkBox_PODInvertVerify_4});

        load_bpnn_mapping(model, "predP", {ui.lineEdit_BPNNForecast_1, ui.lineEdit_BPNNForecast_2, ui.lineEdit_BPNNForecast_3, ui.lineEdit_BPNNForecast_4, ui.lineEdit_BPNNForecast_5, ui.lineEdit_BPNNForecast_6, ui.lineEdit_BPNNForecast_7, ui.lineEdit_BPNNForecast_8, ui.checkBox_BPNNForecastGpu, ui.checkBox_ForecastIn, ui.checkBox_ForecastOut, ui.lineEdit_Forecast_Savedir, ui.lineEdit_Forecast_Loaddir, ui.checkBox_IOForecastCover, ui.checkBox_IOForecastVerify});
        load_bpnn_mapping(model, "predST", {ui.lineEdit_BPNNForecast_9, ui.lineEdit_BPNNForecast_10, ui.lineEdit_BPNNForecast_11, ui.lineEdit_BPNNForecast_12, ui.lineEdit_BPNNForecast_13, ui.lineEdit_BPNNForecast_14, ui.lineEdit_BPNNForecast_15, ui.lineEdit_BPNNForecast_16, ui.checkBox_BPNNForecastGpu_2, ui.checkBox_ForecastIn_2, ui.checkBox_ForecastOut_2, ui.lineEdit_Forecast_Savedir_2, ui.lineEdit_Forecast_Loaddir_2, ui.checkBox_IOForecastCover_2, ui.checkBox_IOForecastVerify_2});
        load_bpnn_mapping(model, "predK", {ui.lineEdit_BPNNForecast_17, ui.lineEdit_BPNNForecast_18, ui.lineEdit_BPNNForecast_19, ui.lineEdit_BPNNForecast_20, ui.lineEdit_BPNNForecast_21, ui.lineEdit_BPNNForecast_22, ui.lineEdit_BPNNForecast_23, ui.lineEdit_BPNNForecast_24, ui.checkBox_BPNNForecastGpu_3, ui.checkBox_ForecastIn_3, ui.checkBox_ForecastOut_3, ui.lineEdit_Forecast_Savedir_3, ui.lineEdit_Forecast_Loaddir_3, ui.checkBox_IOForecastCover_3, ui.checkBox_IOForecastVerify_3});

        load_bpnn_mapping(model, "invP", {ui.lineEdit_BPNNInvert_1, ui.lineEdit_BPNNInvert_2, ui.lineEdit_BPNNInvert_3, ui.lineEdit_BPNNInvert_4, ui.lineEdit_BPNNInvert_5, ui.lineEdit_BPNNInvert_6, ui.lineEdit_BPNNInvert_7, ui.lineEdit_BPNNInvert_8, ui.checkBox_BPNNInvertGpu, ui.checkBox_InvertIn, ui.checkBox_InvertOut, ui.lineEdit_Invert_Savedir, ui.lineEdit_Invert_Loaddir, ui.checkBox_IOInvertCover, ui.checkBox_IOInvertVerify});
        load_bpnn_mapping(model, "invK", {ui.lineEdit_BPNNInvert_9, ui.lineEdit_BPNNInvert_10, ui.lineEdit_BPNNInvert_11, ui.lineEdit_BPNNInvert_12, ui.lineEdit_BPNNInvert_13, ui.lineEdit_BPNNInvert_14, ui.lineEdit_BPNNInvert_15, ui.lineEdit_BPNNInvert_16, ui.checkBox_BPNNInvertGpu_2, ui.checkBox_InvertIn_2, ui.checkBox_InvertOut_2, ui.lineEdit_Invert_Savedir_2, ui.lineEdit_Invert_Loaddir_2, ui.checkBox_IOInvertCover_2, ui.checkBox_IOInvertVerify_2});
        load_bpnn_mapping(model, "invS", {ui.lineEdit_BPNNInvert_17, ui.lineEdit_BPNNInvert_18, ui.lineEdit_BPNNInvert_19, ui.lineEdit_BPNNInvert_20, ui.lineEdit_BPNNInvert_21, ui.lineEdit_BPNNInvert_22, ui.lineEdit_BPNNInvert_23, ui.lineEdit_BPNNInvert_24, ui.checkBox_BPNNInvertGpu_3, ui.checkBox_InvertIn_3, ui.checkBox_InvertOut_3, ui.lineEdit_Invert_Savedir_3, ui.lineEdit_Invert_Loaddir_3, ui.checkBox_IOInvertCover_3, ui.checkBox_IOInvertVerify_3});
        load_bpnn_mapping(model, "invT", {ui.lineEdit_BPNNInvert_25, ui.lineEdit_BPNNInvert_26, ui.lineEdit_BPNNInvert_27, ui.lineEdit_BPNNInvert_28, ui.lineEdit_BPNNInvert_29, ui.lineEdit_BPNNInvert_30, ui.lineEdit_BPNNInvert_31, ui.lineEdit_BPNNInvert_32, ui.checkBox_BPNNInvertGpu_4, ui.checkBox_InvertIn_4, ui.checkBox_InvertOut_4, ui.lineEdit_Invert_Savedir_4, ui.lineEdit_Invert_Loaddir_4, ui.checkBox_IOInvertCover_4, ui.checkBox_IOInvertVerify_4});

        if (const json* predP = find_mapping_const(model, "predP")) {
            set_subject_checks(string_array(*predP, "inputs"), ui.PIn, ui.KIn, ui.SIn, ui.TIn);
            set_subject_checks(string_array(*predP, "outputs"), ui.POut, ui.KOut, ui.SOut, ui.TOut);
        }
        if (const json* predST = find_mapping_const(model, "predST")) {
            set_subject_checks(string_array(*predST, "inputs"), ui.PIn_2, ui.KIn_2, ui.SIn_2, ui.TIn_2);
            set_subject_checks(string_array(*predST, "outputs"), ui.POut_2, ui.KOut_2, ui.SOut_2, ui.TOut_2);
        }
        if (const json* predK = find_mapping_const(model, "predK")) {
            set_subject_checks(string_array(*predK, "inputs"), ui.PIn_3, ui.KIn_3, ui.SIn_3, ui.TIn_3);
            set_subject_checks(string_array(*predK, "outputs"), ui.POut_3, ui.KOut_3, ui.SOut_3, ui.TOut_3);
        }

        const json pfkit = model.value("pfkit", json::object());
        const json pf = pfkit.value("pf", json::object());
        set_line(ui.lineEdit_FDFN_1, pf.value("N", 2000));
        set_line(ui.lineEdit_FDFN_2, pf.value("ess_ratio", 0.5));
        set_line(ui.lineEdit_FDFN_3, pf.value("seed", 42));
        const QString resampler = QString::fromStdString(pf.value("resampler", std::string("systematic")));
        const int resamplerIndex = ui.comboBox_FDFN_1->findText(resampler);
        if (resamplerIndex >= 0) {
            ui.comboBox_FDFN_1->setCurrentIndex(resamplerIndex);
        }

        setTrainingStatus_(QStringLiteral("已读取训练 cfg：%1").arg(displayWorkspacePath_(projectPath)));
        return true;
    } catch (const std::exception& e) {
        const QString message = QStringLiteral("读取训练 cfg 失败：%1").arg(QString::fromStdString(e.what()));
        setTrainingStatus_(message);
        QMessageBox::warning(this, QStringLiteral("训练配置"), message);
        return false;
    }
}

bool EnvPredictorUI::saveTrainingProjectConfig_(const QString& path)
{
    try {
        const QString projectPath = resolveWorkspacePath_(path.trimmed().isEmpty() ? currentTrainingProjectConfigPath_() : path);
        json project = QFileInfo::exists(projectPath) ? read_json_file(projectPath) : json::object();
        json model = QFileInfo::exists(projectPath) ? load_model_json(projectPath, project) : json::object();

        const QString dbPathText = displayWorkspacePath_(trainingDbPathEdit_ ? trainingDbPathEdit_->text().trimmed() : ui.lineEdit_DDFN_1->text().trimmed());
        const QString trainIdsInput = trainingTaskIdsEdit_ ? trainingTaskIdsEdit_->text() : ui.lineEdit_DDFN_2->text();
        const QString fieldIdsInput = trainingFieldIdsEdit_ ? trainingFieldIdsEdit_->text() : ui.lineEdit_DDFN_3->text();
        const QString sensorIdsInput = trainingSensorIdsEdit_ ? trainingSensorIdsEdit_->text() : ui.lineEdit_DDFN_4->text();
        const QString baseIdsInput = trainingBaseIdsEdit_ ? trainingBaseIdsEdit_->text().trimmed() : ui.lineEdit_DDFN_5->text().trimmed();
        const bool usePodDatabase = trainingUsePodDatabaseCheck_ ? trainingUsePodDatabaseCheck_->isChecked() : ui.checkBox_DDFN_1->isChecked();

        set_line(ui.lineEdit_DDFN_1, dbPathText);
        set_line(ui.lineEdit_DDFN_2, trainIdsInput);
        set_line(ui.lineEdit_DDFN_3, fieldIdsInput);
        set_line(ui.lineEdit_DDFN_4, sensorIdsInput);
        set_line(ui.lineEdit_DDFN_5, baseIdsInput);
        set_check(ui.checkBox_DDFN_1, usePodDatabase);

        const std::vector<int> trainIds = parse_int_list(trainIdsInput);
        const int firstTrainId = trainIds.empty() ? 1 : trainIds.front();
        const std::vector<int> fieldIds = parse_int_list(fieldIdsInput);
        const std::vector<int> sensorIds = parse_int_list(sensorIdsInput);

        model["enabled_subjects"] = {"P", "K", "S", "T"};
        model["data"]["db_path"] = dbPathText.toStdString();
        model["data"]["task_train_id"] = firstTrainId;
        model["data"]["task_train_ids"] = trainIds.empty() ? std::vector<int>{firstTrainId} : trainIds;
        model["data"]["field_ids"] = {
            {"K", fieldIds.size() > 0 ? fieldIds[0] : 4},
            {"P", fieldIds.size() > 1 ? fieldIds[1] : 5},
            {"S", fieldIds.size() > 2 ? fieldIds[2] : 3},
            {"T", fieldIds.size() > 3 ? fieldIds[3] : 1}
        };
        model["data"]["sensor_ids"] = {
            {"K", sensorIds.size() > 0 ? sensorIds[0] : 8},
            {"P", sensorIds.size() > 1 ? sensorIds[1] : 9},
            {"S", sensorIds.size() > 2 ? sensorIds[2] : 7},
            {"T", sensorIds.size() > 3 ? sensorIds[3] : 6}
        };
        model["data"]["use_pod_database"] = usePodDatabase;
        model["data"]["base_ids"] = baseIdsInput.isEmpty() ? json::object() : json::parse(baseIdsInput.toStdString());

        save_pod(model, "P", {ui.lineEdit_PODInvert_1, ui.lineEdit_PODInvert_2, ui.lineEdit_PODInvert_3, ui.lineEdit_PODInvert_4, ui.lineEdit_PODInvert_5, ui.lineEdit_PODInvert_6, ui.checkBox_PODInvertCover, ui.checkBox_PODInvertVerify});
        save_pod(model, "K", {ui.lineEdit_PODInvert_7, ui.lineEdit_PODInvert_8, ui.lineEdit_PODInvert_9, ui.lineEdit_PODInvert_10, ui.lineEdit_PODInvert_11, ui.lineEdit_PODInvert_12, ui.checkBox_PODInvertCover_2, ui.checkBox_PODInvertVerify_2});
        save_pod(model, "S", {ui.lineEdit_PODInvert_13, ui.lineEdit_PODInvert_14, ui.lineEdit_PODInvert_15, ui.lineEdit_PODInvert_16, ui.lineEdit_PODInvert_17, ui.lineEdit_PODInvert_18, ui.checkBox_PODInvertCover_3, ui.checkBox_PODInvertVerify_3});
        save_pod(model, "T", {ui.lineEdit_PODInvert_19, ui.lineEdit_PODInvert_20, ui.lineEdit_PODInvert_21, ui.lineEdit_PODInvert_22, ui.lineEdit_PODInvert_23, ui.lineEdit_PODInvert_24, ui.checkBox_PODInvertCover_4, ui.checkBox_PODInvertVerify_4});

        save_bpnn_mapping(model, "predP", {ui.lineEdit_BPNNForecast_1, ui.lineEdit_BPNNForecast_2, ui.lineEdit_BPNNForecast_3, ui.lineEdit_BPNNForecast_4, ui.lineEdit_BPNNForecast_5, ui.lineEdit_BPNNForecast_6, ui.lineEdit_BPNNForecast_7, ui.lineEdit_BPNNForecast_8, ui.checkBox_BPNNForecastGpu, ui.checkBox_ForecastIn, ui.checkBox_ForecastOut, ui.lineEdit_Forecast_Savedir, ui.lineEdit_Forecast_Loaddir, ui.checkBox_IOForecastCover, ui.checkBox_IOForecastVerify});
        save_bpnn_mapping(model, "predST", {ui.lineEdit_BPNNForecast_9, ui.lineEdit_BPNNForecast_10, ui.lineEdit_BPNNForecast_11, ui.lineEdit_BPNNForecast_12, ui.lineEdit_BPNNForecast_13, ui.lineEdit_BPNNForecast_14, ui.lineEdit_BPNNForecast_15, ui.lineEdit_BPNNForecast_16, ui.checkBox_BPNNForecastGpu_2, ui.checkBox_ForecastIn_2, ui.checkBox_ForecastOut_2, ui.lineEdit_Forecast_Savedir_2, ui.lineEdit_Forecast_Loaddir_2, ui.checkBox_IOForecastCover_2, ui.checkBox_IOForecastVerify_2});
        save_bpnn_mapping(model, "predK", {ui.lineEdit_BPNNForecast_17, ui.lineEdit_BPNNForecast_18, ui.lineEdit_BPNNForecast_19, ui.lineEdit_BPNNForecast_20, ui.lineEdit_BPNNForecast_21, ui.lineEdit_BPNNForecast_22, ui.lineEdit_BPNNForecast_23, ui.lineEdit_BPNNForecast_24, ui.checkBox_BPNNForecastGpu_3, ui.checkBox_ForecastIn_3, ui.checkBox_ForecastOut_3, ui.lineEdit_Forecast_Savedir_3, ui.lineEdit_Forecast_Loaddir_3, ui.checkBox_IOForecastCover_3, ui.checkBox_IOForecastVerify_3});
        save_bpnn_mapping(model, "invP", {ui.lineEdit_BPNNInvert_1, ui.lineEdit_BPNNInvert_2, ui.lineEdit_BPNNInvert_3, ui.lineEdit_BPNNInvert_4, ui.lineEdit_BPNNInvert_5, ui.lineEdit_BPNNInvert_6, ui.lineEdit_BPNNInvert_7, ui.lineEdit_BPNNInvert_8, ui.checkBox_BPNNInvertGpu, ui.checkBox_InvertIn, ui.checkBox_InvertOut, ui.lineEdit_Invert_Savedir, ui.lineEdit_Invert_Loaddir, ui.checkBox_IOInvertCover, ui.checkBox_IOInvertVerify});
        save_bpnn_mapping(model, "invK", {ui.lineEdit_BPNNInvert_9, ui.lineEdit_BPNNInvert_10, ui.lineEdit_BPNNInvert_11, ui.lineEdit_BPNNInvert_12, ui.lineEdit_BPNNInvert_13, ui.lineEdit_BPNNInvert_14, ui.lineEdit_BPNNInvert_15, ui.lineEdit_BPNNInvert_16, ui.checkBox_BPNNInvertGpu_2, ui.checkBox_InvertIn_2, ui.checkBox_InvertOut_2, ui.lineEdit_Invert_Savedir_2, ui.lineEdit_Invert_Loaddir_2, ui.checkBox_IOInvertCover_2, ui.checkBox_IOInvertVerify_2});
        save_bpnn_mapping(model, "invS", {ui.lineEdit_BPNNInvert_17, ui.lineEdit_BPNNInvert_18, ui.lineEdit_BPNNInvert_19, ui.lineEdit_BPNNInvert_20, ui.lineEdit_BPNNInvert_21, ui.lineEdit_BPNNInvert_22, ui.lineEdit_BPNNInvert_23, ui.lineEdit_BPNNInvert_24, ui.checkBox_BPNNInvertGpu_3, ui.checkBox_InvertIn_3, ui.checkBox_InvertOut_3, ui.lineEdit_Invert_Savedir_3, ui.lineEdit_Invert_Loaddir_3, ui.checkBox_IOInvertCover_3, ui.checkBox_IOInvertVerify_3});
        save_bpnn_mapping(model, "invT", {ui.lineEdit_BPNNInvert_25, ui.lineEdit_BPNNInvert_26, ui.lineEdit_BPNNInvert_27, ui.lineEdit_BPNNInvert_28, ui.lineEdit_BPNNInvert_29, ui.lineEdit_BPNNInvert_30, ui.lineEdit_BPNNInvert_31, ui.lineEdit_BPNNInvert_32, ui.checkBox_BPNNInvertGpu_4, ui.checkBox_InvertIn_4, ui.checkBox_InvertOut_4, ui.lineEdit_Invert_Savedir_4, ui.lineEdit_Invert_Loaddir_4, ui.checkBox_IOInvertCover_4, ui.checkBox_IOInvertVerify_4});

        (*find_mapping(model, "predP"))["inputs"] = subject_checks(ui.PIn, ui.KIn, ui.SIn, ui.TIn);
        (*find_mapping(model, "predP"))["outputs"] = subject_checks(ui.POut, ui.KOut, ui.SOut, ui.TOut);
        (*find_mapping(model, "predST"))["inputs"] = subject_checks(ui.PIn_2, ui.KIn_2, ui.SIn_2, ui.TIn_2);
        (*find_mapping(model, "predST"))["outputs"] = subject_checks(ui.POut_2, ui.KOut_2, ui.SOut_2, ui.TOut_2);
        (*find_mapping(model, "predK"))["inputs"] = subject_checks(ui.PIn_3, ui.KIn_3, ui.SIn_3, ui.TIn_3);
        (*find_mapping(model, "predK"))["outputs"] = subject_checks(ui.POut_3, ui.KOut_3, ui.SOut_3, ui.TOut_3);

        model["pfkit"]["pf"]["N"] = ui.lineEdit_FDFN_1->text().toInt();
        model["pfkit"]["pf"]["ess_ratio"] = ui.lineEdit_FDFN_2->text().toDouble();
        model["pfkit"]["pf"]["seed"] = ui.lineEdit_FDFN_3->text().toInt();
        model["pfkit"]["pf"]["resampler"] = ui.comboBox_FDFN_1->currentText().toStdString();
        model["allow_train_in_init"] = false;

        const QString modelPath = resolve_model_path(projectPath, project);
        if (!project.contains("model_ref") || !project["model_ref"].is_object()) {
            project["model_ref"] = json::object();
        }
        project["model_ref"]["path"] = relative_path_from_project(projectPath, modelPath).toStdString();
        project["model_ref"]["json_text"] = "";
        project["pipeline_mode"] = project.value("pipeline_mode", std::string("pf_coupled"));
        if (project.contains("model")) {
            project["model"] = model;
        }

        write_json_file(modelPath, model);
        write_json_file(projectPath, project);
        setTrainingStatus_(QStringLiteral("已保存 cfg：%1；model：%2")
            .arg(displayWorkspacePath_(projectPath), displayWorkspacePath_(modelPath)));
        return true;
    } catch (const std::exception& e) {
        const QString message = QStringLiteral("保存训练 cfg 失败：%1").arg(QString::fromStdString(e.what()));
        setTrainingStatus_(message);
        QMessageBox::warning(this, QStringLiteral("训练配置"), message);
        return false;
    }
}

void EnvPredictorUI::on_pushButton_37_clicked()
{
    saveTrainingProjectConfig_(currentTrainingProjectConfigPath_());
}

void EnvPredictorUI::startTrainingCli_()
{
    if (trainingProcess_ && trainingProcess_->state() != QProcess::NotRunning) {
        setTrainingStatus_(QStringLiteral("训练正在运行，请等待当前任务结束。"));
        return;
    }

    if (!saveTrainingProjectConfig_(currentTrainingProjectConfigPath_())) {
        return;
    }

    const QString trainer = native_path(QDir(workspaceRootPath_()).filePath("_deps/workspace/x64/Release/EnvTrainer.exe"));
    if (!QFileInfo::exists(trainer)) {
        const QString message = QStringLiteral("EnvTrainer.exe 不存在：%1").arg(trainer);
        setTrainingStatus_(message);
        QMessageBox::warning(this, QStringLiteral("训练"), message);
        return;
    }

    const QString runId = QStringLiteral("ui_train_%1").arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    const QString outputRoot = currentTrainingOutputRootPath_();
    const QString runRoot = QDir(outputRoot).filePath(runId);
    QDir().mkpath(runRoot);
    const QString resultPath = QDir(runRoot).filePath("training_result.json");
    const QString statusPath = QDir(runRoot).filePath("status.jsonl");
    trainingLastResultPath_ = resultPath;

    QStringList args;
    args << QStringLiteral("--project") << currentTrainingProjectConfigPath_()
         << QStringLiteral("--output-root") << outputRoot
         << QStringLiteral("--run-id") << runId
         << QStringLiteral("--result") << resultPath
         << QStringLiteral("--status") << statusPath;

    if (trainingLogEdit_) {
        trainingLogEdit_->clear();
    }
    appendTrainingLog_(QStringLiteral("> %1 %2").arg(trainer, args.join(QStringLiteral(" "))));

    trainingProcess_ = new QProcess(this);
    trainingProcess_->setWorkingDirectory(workspaceRootPath_());
    trainingProcess_->setProcessChannelMode(QProcess::MergedChannels);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString binDir = QFileInfo(trainer).absolutePath();
    env.insert(QStringLiteral("PATH"), binDir + QStringLiteral(";") + env.value(QStringLiteral("PATH")));
    trainingProcess_->setProcessEnvironment(env);

    connect(trainingProcess_, &QProcess::readyReadStandardOutput, this, [this]() {
        if (!trainingProcess_) {
            return;
        }
        const QString output = decode_process_output(trainingProcess_->readAllStandardOutput());
        if (!output.isEmpty()) {
            appendTrainingLog_(output);
            QString normalized = output;
            normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
            normalized.replace(QChar('\r'), QChar('\n'));
            const QStringList lines = normalized.split(QChar('\n'), Qt::SkipEmptyParts);
            if (!lines.isEmpty()) {
                setTrainingStatus_(lines.last().trimmed().right(500));
            }
        }
    });
    connect(trainingProcess_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
        [this](int exitCode, QProcess::ExitStatus status) {
            finishTrainingCli_(exitCode, status == QProcess::NormalExit);
        });

    if (trainingStartButton_) {
        trainingStartButton_->setEnabled(false);
    }
    if (trainingSaveConfigButton_) {
        trainingSaveConfigButton_->setEnabled(false);
    }
    setTrainingStatus_(QStringLiteral("开始训练：%1").arg(runId));
    trainingProcess_->start(trainer, args);
    if (!trainingProcess_->waitForStarted(30000)) {
        appendTrainingLog_(QStringLiteral("[启动失败] %1").arg(trainingProcess_->errorString()));
        finishTrainingCli_(-1, false);
    }
}

void EnvPredictorUI::finishTrainingCli_(int exitCode, bool normalExit)
{
    QString detail;
    if (trainingProcess_) {
        detail = decode_process_output(trainingProcess_->readAllStandardOutput());
        appendTrainingLog_(detail);
        detail = detail.trimmed();
        trainingProcess_->deleteLater();
        trainingProcess_ = nullptr;
    }
    if (trainingStartButton_) {
        trainingStartButton_->setEnabled(true);
    }
    if (trainingSaveConfigButton_) {
        trainingSaveConfigButton_->setEnabled(true);
    }

    if (normalExit && exitCode == 0) {
        setTrainingStatus_(QStringLiteral("训练完成，结果：%1").arg(trainingLastResultPath_));
        return;
    }

    const QString message = QStringLiteral("训练失败 exit=%1：%2").arg(exitCode).arg(detail.right(1000));
    setTrainingStatus_(message);
    QMessageBox::warning(this, QStringLiteral("训练失败"), message);
}
