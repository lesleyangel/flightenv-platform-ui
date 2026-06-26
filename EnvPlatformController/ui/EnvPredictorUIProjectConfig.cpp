#include "EnvPredictorUI.h"
#include "EnvPredictorUiHelpers.h"

#include <QDebug>

#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

using json = nlohmann::json;

bool write_pretty_json_file(const std::string& path, const json& payload, std::string* err = nullptr)
{
    std::ofstream ofs(path);
    if (!ofs) {
        if (err) {
            *err = "failed to open output file: " + path;
        }
        return false;
    }
    ofs << payload.dump(2);
    return true;
}

} // namespace
void EnvPredictorUI::on_pushButton_37_clicked() {//反演模型保存
    auto append_subject_if = [](std::vector<std::string>& subjects, bool enabled, const char* key) {
        if (enabled) {
            subjects.emplace_back(key);
        }
    };

    auto make_pod_train_json = [](int base_order,
        double energy_ratio,
        int max_iterations,
        double regularization,
        const std::string& save_dir,
        bool overwrite) {
            return json{
                {"base_order", base_order},
                {"regularization", regularization},
                {"max_iterations", max_iterations},
                {"energy_ratio", energy_ratio},
                {"save_dir", save_dir},
                {"overwrite", overwrite}
            };
    };

    auto make_pod_use_json = [](const std::string& load_dir, bool strict) {
        return json{
            {"load_dir", load_dir},
            {"strict", strict}
        };
    };

    auto make_core_model_json = [](double learning_rate,
        double momentum,
        const std::vector<int>& hidden_layers,
        int max_epochs,
        double error_threshold,
        int batch_size,
        double val_split,
        int seed,
        bool use_gpu,
        bool input_normalize,
        bool output_normalize,
        const std::string& save_dir,
        const std::string& load_dir,
        bool overwrite,
        bool strict) {
            return json{
                {"type", "BPNN"},
                {"bpnn", {
                    {"learning_rate", learning_rate},
                    {"momentum", momentum},
                    {"hidden_layers", hidden_layers},
                    {"max_epochs", max_epochs},
                    {"error_threshold", error_threshold},
                    {"batch_size", batch_size},
                    {"val_split", val_split},
                    {"use_gpu", use_gpu},
                    {"seed", seed}
                }},
                {"input_normalize", input_normalize},
                {"output_normalize", output_normalize},
                {"train_io", {
                    {"save_dir", save_dir},
                    {"overwrite", overwrite}
                }},
                {"use_io", {
                    {"load_dir", load_dir},
                    {"strict", strict}
                }}
            };
    };

    auto make_mapping_json = [&](const char* name,
        double learning_rate,
        double momentum,
        const std::vector<int>& hidden_layers,
        int max_epochs,
        double error_threshold,
        int batch_size,
        double val_split,
        int seed,
        bool use_gpu,
        bool input_normalize,
        bool output_normalize,
        const std::string& save_dir,
        const std::string& load_dir,
        bool overwrite,
        bool strict,
        const std::vector<std::string>& inputs,
        const std::vector<std::string>& outputs) {
            return json{
                {"name", name},
                {"inputs", inputs},
                {"outputs", outputs},
                {"train_type", "default"},
                {"model", make_core_model_json(
                    learning_rate,
                    momentum,
                    hidden_layers,
                    max_epochs,
                    error_threshold,
                    batch_size,
                    val_split,
                    seed,
                    use_gpu,
                    input_normalize,
                    output_normalize,
                    save_dir,
                    load_dir,
                    overwrite,
                    strict)}
            };
    };

    auto make_source_json = [](const char* name, const char* channel, const std::string& db_path, int task_id) {
        return json{
            {"name", name},
            {"channel", channel},
            {"kind", "database"},
            {"enabled", true},
            {"time_base", "event"},
            {"bucket_ms", 50},
            {"linger_ms", 600},
            {"com_port", ""},
            {"baud", 115200},
            {"driver_lib", ""},
            {"db_path", db_path},
            {"task_id", task_id},
            {"field_id", 0},
            {"preload_to_memory", false},
            {"period_ms", 1000},
            {"waveform", "sine"},
            {"amp", 1.0},
            {"offset", 0.0},
            {"drop", 0.0},
            {"jitter_min", 0},
            {"jitter_max", 200},
            {"reorder", 0.0}
        };
    };

    auto make_runtime_pipeline_json = []() {
        return json{
            {"mode", "pf_coupled"},
            {"state_order", json::array()},
            {"obs_order", json::array()},
            {"online_steps", json::array()},
            {"transition_steps", json::array()},
            {"observation_steps", json::array()},
            {"observation_relations", json::array()},
            {"inverse_relations", json::array()}
        };
    };

    std::string db_path = ui.lineEdit_DDFN_1->text().toStdString();
    if (db_path.empty()) {
        qWarning() << "model.data.db_path cannot be empty.";
        return;
    }

    const int task_train_id = ui.lineEdit_DDFN_2->text().toInt();
    if (task_train_id <= 0) {
        qWarning() << "model.data.task_train_id must be positive.";
        return;
    }

    constexpr size_t kSubjectCount = 4;
    std::vector<int> field_ids;
    std::vector<int> sensor_ids;
    if (!parseIntVectorExact(ui.lineEdit_DDFN_3->text().toStdString(), kSubjectCount, "field_ids", field_ids) ||
        !parseIntVectorExact(ui.lineEdit_DDFN_4->text().toStdString(), kSubjectCount, "sensor_ids", sensor_ids)) {
        return;
    }

    json pod_train = json::object();
    json pod_use = json::object();
    json inv_models = json::object();
    //反演
    {
        pod_train["P"] = make_pod_train_json(
            ui.lineEdit_PODInvert_1->text().toInt(),
            ui.lineEdit_PODInvert_2->text().toDouble(),
            ui.lineEdit_PODInvert_3->text().toInt(),
            ui.lineEdit_PODInvert_4->text().toDouble(),
            ui.lineEdit_PODInvert_5->text().toStdString(),
            ui.checkBox_PODInvertCover->isChecked());
        pod_use["P"] = make_pod_use_json(
            ui.lineEdit_PODInvert_6->text().toStdString(),
            ui.checkBox_PODInvertVerify->isChecked());
        inv_models["P"] = make_core_model_json(
            ui.lineEdit_BPNNInvert_1->text().toDouble(),
            ui.lineEdit_BPNNInvert_2->text().toDouble(),
            stringToVector(ui.lineEdit_BPNNInvert_3->text().toStdString()),
            ui.lineEdit_BPNNInvert_4->text().toInt(),
            ui.lineEdit_BPNNInvert_5->text().toDouble(),
            ui.lineEdit_BPNNInvert_6->text().toInt(),
            ui.lineEdit_BPNNInvert_7->text().toDouble(),
            ui.lineEdit_BPNNInvert_8->text().toInt(),
            ui.checkBox_BPNNInvertGpu->isChecked(),
            ui.checkBox_InvertIn->isChecked(),
            ui.checkBox_InvertOut->isChecked(),
            ui.lineEdit_Invert_Savedir->text().toStdString(),
            ui.lineEdit_Invert_Loaddir->text().toStdString(),
            ui.checkBox_IOInvertCover->isChecked(),
            ui.checkBox_IOInvertVerify->isChecked());

        //气动热流反演
        pod_train["K"] = make_pod_train_json(
            ui.lineEdit_PODInvert_7->text().toInt(),
            ui.lineEdit_PODInvert_8->text().toDouble(),
            ui.lineEdit_PODInvert_9->text().toInt(),
            ui.lineEdit_PODInvert_10->text().toDouble(),
            ui.lineEdit_PODInvert_11->text().toStdString(),
            ui.checkBox_PODInvertCover_2->isChecked());
        pod_use["K"] = make_pod_use_json(
            ui.lineEdit_PODInvert_12->text().toStdString(),
            ui.checkBox_PODInvertVerify_2->isChecked());
        inv_models["K"] = make_core_model_json(
            ui.lineEdit_BPNNInvert_9->text().toDouble(),
            ui.lineEdit_BPNNInvert_10->text().toDouble(),
            stringToVector(ui.lineEdit_BPNNInvert_11->text().toStdString()),
            ui.lineEdit_BPNNInvert_12->text().toInt(),
            ui.lineEdit_BPNNInvert_13->text().toDouble(),
            ui.lineEdit_BPNNInvert_14->text().toInt(),
            ui.lineEdit_BPNNInvert_15->text().toDouble(),
            ui.lineEdit_BPNNInvert_16->text().toInt(),
            ui.checkBox_BPNNInvertGpu_2->isChecked(),
            ui.checkBox_InvertIn_2->isChecked(),
            ui.checkBox_InvertOut_2->isChecked(),
            ui.lineEdit_Invert_Savedir_2->text().toStdString(),
            ui.lineEdit_Invert_Loaddir_2->text().toStdString(),
            ui.checkBox_IOInvertCover_2->isChecked(),
            ui.checkBox_IOInvertVerify_2->isChecked());

        //结构应变反演
        pod_train["S"] = make_pod_train_json(
            ui.lineEdit_PODInvert_13->text().toInt(),
            ui.lineEdit_PODInvert_14->text().toDouble(),
            ui.lineEdit_PODInvert_15->text().toInt(),
            ui.lineEdit_PODInvert_16->text().toDouble(),
            ui.lineEdit_PODInvert_17->text().toStdString(),
            ui.checkBox_PODInvertCover_3->isChecked());
        pod_use["S"] = make_pod_use_json(
            ui.lineEdit_PODInvert_18->text().toStdString(),
            ui.checkBox_PODInvertVerify_3->isChecked());
        inv_models["S"] = make_core_model_json(
            ui.lineEdit_BPNNInvert_17->text().toDouble(),
            ui.lineEdit_BPNNInvert_18->text().toDouble(),
            stringToVector(ui.lineEdit_BPNNInvert_19->text().toStdString()),
            ui.lineEdit_BPNNInvert_20->text().toInt(),
            ui.lineEdit_BPNNInvert_21->text().toDouble(),
            ui.lineEdit_BPNNInvert_22->text().toInt(),
            ui.lineEdit_BPNNInvert_23->text().toDouble(),
            ui.lineEdit_BPNNInvert_24->text().toInt(),
            ui.checkBox_BPNNInvertGpu_3->isChecked(),
            ui.checkBox_InvertIn_3->isChecked(),
            ui.checkBox_InvertOut_3->isChecked(),
            ui.lineEdit_Invert_Savedir_3->text().toStdString(),
            ui.lineEdit_Invert_Loaddir_3->text().toStdString(),
            ui.checkBox_IOInvertCover_3->isChecked(),
            ui.checkBox_IOInvertVerify_3->isChecked());

        //结构温度反演
        pod_train["T"] = make_pod_train_json(
            ui.lineEdit_PODInvert_19->text().toInt(),
            ui.lineEdit_PODInvert_20->text().toDouble(),
            ui.lineEdit_PODInvert_21->text().toInt(),
            ui.lineEdit_PODInvert_22->text().toDouble(),
            ui.lineEdit_PODInvert_23->text().toStdString(),
            ui.checkBox_PODInvertCover_4->isChecked());
        pod_use["T"] = make_pod_use_json(
            ui.lineEdit_PODInvert_24->text().toStdString(),
            ui.checkBox_PODInvertVerify_4->isChecked());
        inv_models["T"] = make_core_model_json(
            ui.lineEdit_BPNNInvert_25->text().toDouble(),
            ui.lineEdit_BPNNInvert_26->text().toDouble(),
            stringToVector(ui.lineEdit_BPNNInvert_27->text().toStdString()),
            ui.lineEdit_BPNNInvert_28->text().toInt(),
            ui.lineEdit_BPNNInvert_29->text().toDouble(),
            ui.lineEdit_BPNNInvert_30->text().toInt(),
            ui.lineEdit_BPNNInvert_31->text().toDouble(),
            ui.lineEdit_BPNNInvert_32->text().toInt(),
            ui.checkBox_BPNNInvertGpu_4->isChecked(),
            ui.checkBox_InvertIn_4->isChecked(),
            ui.checkBox_InvertOut_4->isChecked(),
            ui.lineEdit_Invert_Savedir_4->text().toStdString(),
            ui.lineEdit_Invert_Loaddir_4->text().toStdString(),
            ui.checkBox_IOInvertCover_4->isChecked(),
            ui.checkBox_IOInvertVerify_4->isChecked());
    }

    json pfkit = {
        {"pf", {
            {"N", ui.lineEdit_FDFN_1->text().toInt()},
            {"ess_ratio", ui.lineEdit_FDFN_2->text().toDouble()},
            {"seed", ui.lineEdit_FDFN_3->text().toInt()},
            {"resampler", ui.comboBox_FDFN_1->currentText().toStdString()},
            {"dt", 1.0},
            {"lag", 15},
            {"auto_smoothing", true},
            {"init_distribution", "gaussian_prev_posterior"},
            {"estimate_mode", "posterior_mean"}
        }},
        {"noise", {
            {"pred_var", {
                {"P", {ui.lineEdit_NDFN_11->text().toDouble()}},
                {"K", {ui.lineEdit_NDFN_12->text().toDouble()}},
                {"S", {
                    ui.lineEdit_NDFN_14->text().toDouble(),
                    ui.lineEdit_NDFN_15->text().toDouble(),
                    ui.lineEdit_NDFN_16->text().toDouble(),
                    ui.lineEdit_NDFN_17->text().toDouble(),
                    ui.lineEdit_NDFN_18->text().toDouble(),
                    ui.lineEdit_NDFN_19->text().toDouble()
                }},
                {"T", {ui.lineEdit_NDFN_13->text().toDouble()}}
            }},
            {"inv_var", {
                {"P", {ui.lineEdit_NDFN_1->text().toDouble()}},
                {"K", {ui.lineEdit_NDFN_2->text().toDouble()}},
                {"S", {
                    ui.lineEdit_NDFN_4->text().toDouble(),
                    ui.lineEdit_NDFN_5->text().toDouble(),
                    ui.lineEdit_NDFN_6->text().toDouble(),
                    ui.lineEdit_NDFN_7->text().toDouble(),
                    ui.lineEdit_NDFN_8->text().toDouble(),
                    ui.lineEdit_NDFN_9->text().toDouble()
                }},
                {"T", {ui.lineEdit_NDFN_3->text().toDouble()}}
            }}
        }},
        {"uncertainty", {
            {"process", {{"q_diag", json::object()}}},
            {"observation", {{"r_diag", json::object()}}},
            {"model_discrepancy", {
                {"enabled", false},
                {"m_diag", json::object()}
            }}
        }},
        {"init_state", {
            {"mode", "runtime_seeded_init"}
        }}
    };

    json data = {
        {"db_path", db_path},
        {"task_train_id", task_train_id},
        {"task_train_ids", {task_train_id}},
        {"field_ids", {
            {"K", field_ids[0]},
            {"P", field_ids[1]},
            {"S", field_ids[2]},
            {"T", field_ids[3]}
        }},
        {"sensor_ids", {
            {"K", sensor_ids[0]},
            {"P", sensor_ids[1]},
            {"S", sensor_ids[2]},
            {"T", sensor_ids[3]}
        }},
        {"use_pod_database", ui.checkBox_DDFN_1->isChecked()},
        {"base_ids", json::object()}
    };

    //预测映射
    json prediction_mappings = json::array();
    {
        //气动压力预测映射
        std::vector<std::string> predPInputs;
        std::vector<std::string> predPOutputs;
        append_subject_if(predPInputs, ui.PIn->isChecked(), "P");
        append_subject_if(predPInputs, ui.KIn->isChecked(), "K");
        append_subject_if(predPInputs, ui.SIn->isChecked(), "S");
        append_subject_if(predPInputs, ui.TIn->isChecked(), "T");
        append_subject_if(predPOutputs, ui.POut->isChecked(), "P");
        append_subject_if(predPOutputs, ui.KOut->isChecked(), "K");
        append_subject_if(predPOutputs, ui.SOut->isChecked(), "S");
        append_subject_if(predPOutputs, ui.TOut->isChecked(), "T");
        prediction_mappings.push_back(make_mapping_json(
            "predP",
            ui.lineEdit_BPNNForecast_1->text().toDouble(),
            ui.lineEdit_BPNNForecast_2->text().toDouble(),
            stringToVector(ui.lineEdit_BPNNForecast_3->text().toStdString()),
            ui.lineEdit_BPNNForecast_4->text().toInt(),
            ui.lineEdit_BPNNForecast_5->text().toDouble(),
            ui.lineEdit_BPNNForecast_6->text().toInt(),
            ui.lineEdit_BPNNForecast_7->text().toDouble(),
            ui.lineEdit_BPNNForecast_8->text().toInt(),
            ui.checkBox_BPNNForecastGpu->isChecked(),
            ui.checkBox_ForecastIn->isChecked(),
            ui.checkBox_ForecastOut->isChecked(),
            ui.lineEdit_Forecast_Savedir->text().toStdString(),
            ui.lineEdit_Forecast_Loaddir->text().toStdString(),
            ui.checkBox_IOForecastCover->isChecked(),
            ui.checkBox_IOForecastVerify->isChecked(),
            predPInputs,
            predPOutputs));

        //气动热流预测映射
        std::vector<std::string> predKInputs;
        std::vector<std::string> predKOutputs;
        append_subject_if(predKInputs, ui.PIn_3->isChecked(), "P");
        append_subject_if(predKInputs, ui.KIn_3->isChecked(), "K");
        append_subject_if(predKInputs, ui.SIn_3->isChecked(), "S");
        append_subject_if(predKInputs, ui.TIn_3->isChecked(), "T");
        append_subject_if(predKOutputs, ui.POut_3->isChecked(), "P");
        append_subject_if(predKOutputs, ui.KOut_3->isChecked(), "K");
        append_subject_if(predKOutputs, ui.SOut_3->isChecked(), "S");
        append_subject_if(predKOutputs, ui.TOut_3->isChecked(), "T");
        prediction_mappings.push_back(make_mapping_json(
            "predK",
            ui.lineEdit_BPNNForecast_17->text().toDouble(),
            ui.lineEdit_BPNNForecast_18->text().toDouble(),
            stringToVector(ui.lineEdit_BPNNForecast_19->text().toStdString()),
            ui.lineEdit_BPNNForecast_20->text().toInt(),
            ui.lineEdit_BPNNForecast_21->text().toDouble(),
            ui.lineEdit_BPNNForecast_22->text().toInt(),
            ui.lineEdit_BPNNForecast_23->text().toDouble(),
            ui.lineEdit_BPNNForecast_24->text().toInt(),
            ui.checkBox_BPNNForecastGpu_3->isChecked(),
            ui.checkBox_ForecastIn_3->isChecked(),
            ui.checkBox_ForecastOut_3->isChecked(),
            ui.lineEdit_Forecast_Savedir_3->text().toStdString(),
            ui.lineEdit_Forecast_Loaddir_3->text().toStdString(),
            ui.checkBox_IOForecastCover_3->isChecked(),
            ui.checkBox_IOForecastVerify_3->isChecked(),
            predKInputs,
            predKOutputs));

        //结构力热预测映射
        std::vector<std::string> predSTInputs;
        std::vector<std::string> predSTOutputs;
        append_subject_if(predSTInputs, ui.PIn_2->isChecked(), "P");
        append_subject_if(predSTInputs, ui.KIn_2->isChecked(), "K");
        append_subject_if(predSTInputs, ui.SIn_2->isChecked(), "S");
        append_subject_if(predSTInputs, ui.TIn_2->isChecked(), "T");
        append_subject_if(predSTOutputs, ui.POut_2->isChecked(), "P");
        append_subject_if(predSTOutputs, ui.KOut_2->isChecked(), "K");
        append_subject_if(predSTOutputs, ui.SOut_2->isChecked(), "S");
        append_subject_if(predSTOutputs, ui.TOut_2->isChecked(), "T");
        prediction_mappings.push_back(make_mapping_json(
            "predST",
            ui.lineEdit_BPNNForecast_9->text().toDouble(),
            ui.lineEdit_BPNNForecast_10->text().toDouble(),
            stringToVector(ui.lineEdit_BPNNForecast_11->text().toStdString()),
            ui.lineEdit_BPNNForecast_12->text().toInt(),
            ui.lineEdit_BPNNForecast_13->text().toDouble(),
            ui.lineEdit_BPNNForecast_14->text().toInt(),
            ui.lineEdit_BPNNForecast_15->text().toDouble(),
            ui.lineEdit_BPNNForecast_16->text().toInt(),
            ui.checkBox_BPNNForecastGpu_2->isChecked(),
            ui.checkBox_ForecastIn_2->isChecked(),
            ui.checkBox_ForecastOut_2->isChecked(),
            ui.lineEdit_Forecast_Savedir_2->text().toStdString(),
            ui.lineEdit_Forecast_Loaddir_2->text().toStdString(),
            ui.checkBox_IOForecastCover_2->isChecked(),
            ui.checkBox_IOForecastVerify_2->isChecked(),
            predSTInputs,
            predSTOutputs));
    }

    json model = {
        {"enabled_subjects", json::array()},
        {"data", data},
        {"pod", {
            {"train", pod_train},
            {"use", pod_use}
        }},
        {"inv", {
            {"model_per_subject", inv_models}
        }},
        {"obs", {
            {"model_per_subject", json::object()}
        }},
        {"pred", {
            {"mappings", prediction_mappings}
        }},
        {"runtime", make_runtime_pipeline_json()},
        {"pfkit", pfkit},
        {"allow_train_in_init", false}
    };

    json project = {
        {"pipeline_mode", "pf_coupled"},
        {"sources", json::array({
            make_source_json("SensorsPacked", "Sensor", db_path, task_train_id),
            make_source_json("StatePacked", "BallisticState", db_path, task_train_id)
        })},
        {"sync", {
            {"policy", "all"},
            {"atleast_m", 2},
            {"bucket_ms", 1000},
            {"linger_ms", 600},
            {"time_base", "event"},
            {"required_names", {"SensorsPacked", "StatePacked"}}
        }},
        {"runtime", {
            {"mode", "default"},
            {"strict", false},
            {"enabled_subjects", json::array()},
            {"init_steps", json::array()},
            {"online_steps", json::array()}
        }},
        {"model_ref", {
            {"path", ""},
            {"version", ""},
            {"checksum", ""},
            {"json_text", model.dump()}
        }},
        {"test_eval", {
            {"enabled", true},
            {"retrain_models", false},
            {"exit_after_retrain", true},
            {"report_dir", "model_retrain_diagnostics"},
            {"print_bpnn_eval_to_console", true},
            {"print_data_check_summary_to_console", true},
            {"write_bpnn_eval_reports", true},
            {"write_training_data_reports", true},
            {"write_sample_reports", false},
            {"write_train_sample_reports", false},
            {"write_val_sample_reports", false},
            {"write_test_sample_reports", true},
            {"sample_report_limit", 50},
            {"offline_compare_target", "both"},
            {"online_db_compare_enabled", true},
            {"online_compare_target", "both"},
            {"write_online_sample_reports", false},
            {"write_online_field_node_data", false},
            {"val_tail_ratio", 0.2}
        }},
        {"model", model}
    };

    std::string write_err;
    if (!write_pretty_json_file("project.json", project, &write_err)) {
        qWarning() << "Failed to write project.json:" << QString::fromStdString(write_err);
    }

}


