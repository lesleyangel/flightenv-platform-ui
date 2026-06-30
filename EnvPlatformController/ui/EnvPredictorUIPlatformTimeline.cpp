#include "EnvPredictorUIInternal.h"

using namespace flightenv::platform_ui::internal;

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

    platformUiLogInfo(
        "Platform field render: branch=%s loop=%d fields=%zu snapshot_fields=%zu",
        platformCurrentBranchId_.toStdString().c_str(),
        platformCurrentLoop_,
        fields.size(),
        platform_snapshot_.field_artifacts.size());

    platformFieldOrder_.clear();

    if (fields.empty()) {
        if (platformFrameStatusLabel_) {
            platformFrameStatusLabel_->setText(
                QStringLiteral("当前分支/帧没有可渲染场：branch=%1 loop=%2，总场=%3")
                    .arg(platformCurrentBranchId_)
                    .arg(platformCurrentLoop_)
                    .arg(static_cast<int>(platform_snapshot_.field_artifacts.size())));
        }
        renderGraphPlatformFieldArtifacts_();
        updatePlatformSensorDisplayForLoop(platformCurrentLoop_);
        return;
    }

    for (const auto& field : fields) {
        const QString key = platformFieldIdentityKey(field);
        platformFieldOrder_.push_back(key);
        if (platformFieldVisibility_.find(key) == platformFieldVisibility_.end()) {
            platformFieldVisibility_[key] = true;
        }

        auto*& widget = platformFieldWidgets_[key];
        if (widget == nullptr) {
            widget = new flightenv::ui::demo::VtkModelFieldWidget(vtkContainer);
            widget->setMinimumHeight(220);
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
        const QString label = platformFieldDisplayLabelFromKey(key);
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
        ? (objectRoot / "fixtures" / "sensor_stream_db70_real_db.json")
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

/**
* @brief 初始化ROS信息参数表格
*/
