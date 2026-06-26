param(
    [string]$WorkspaceRoot = "",
    [string]$MainlineRunRoot = ""
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($WorkspaceRoot)) {
    $WorkspaceRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
}
if ([string]::IsNullOrWhiteSpace($MainlineRunRoot)) {
    $MainlineRunRoot = Join-Path $WorkspaceRoot "_local_artifacts\platform-pdk\mainline-runs\platform_ui_tp5_full_acceptance_20260614"
}

function Assert-True {
    param(
        [bool]$Condition,
        [string]$Message
    )
    if (-not $Condition) {
        throw $Message
    }
}

function Read-Json {
    param([string]$Path)
    Assert-True (Test-Path -LiteralPath $Path -PathType Leaf) "missing JSON: $Path"
    $text = Get-Content -LiteralPath $Path -Raw -Encoding UTF8
    $text = $text.Replace("-Infinity", "null").Replace("Infinity", "null").Replace("NaN", "null")
    return $text | ConvertFrom-Json
}

function Read-Text {
    param([string]$Path)
    Assert-True (Test-Path -LiteralPath $Path -PathType Leaf) "missing file: $Path"
    return Get-Content -LiteralPath $Path -Raw -Encoding UTF8
}

$summaryPath = Join-Path $MainlineRunRoot "mainline_summary.json"
$summary = Read-Json $summaryPath
$branchRegistryPath = Join-Path $MainlineRunRoot "branch_registry.json"
$timelineIndexPath = Join-Path $MainlineRunRoot "run_timeline_index.json"
$cursorPath = Join-Path $MainlineRunRoot "runtime_cursor.json"
$hasBranchTimeline = (Test-Path -LiteralPath $branchRegistryPath -PathType Leaf) -and
                     (Test-Path -LiteralPath $timelineIndexPath -PathType Leaf)

if ($hasBranchTimeline) {
    $branchRegistry = Read-Json $branchRegistryPath
    $timelineIndex = Read-Json $timelineIndexPath
    $cursor = if (Test-Path -LiteralPath $cursorPath -PathType Leaf) { Read-Json $cursorPath } else { $null }

    $frames = @($timelineIndex.online_frames)
    Assert-True ($frames.Count -gt 0) "run_timeline_index has no online frames"
    Assert-True ($frames.Count -eq [int]$summary.online.effective_frames) "timeline frame count mismatch"

    $branches = @($branchRegistry.branches)
    $onlineBranches = @($branches | Where-Object { $_.branch_kind -eq "online_mainline" })
    $realtimeBranches = @($branches | Where-Object { $_.branch_kind -eq "realtime_prediction" })
    $predictionBranches = @($branches | Where-Object { $_.branch_kind -eq "future_prediction" })
    Assert-True ($branches.Count -gt 0) "branch_registry has no branches"
    Assert-True ($onlineBranches.Count -eq 1) "branch_registry must expose one online filtering branch"
    Assert-True ($realtimeBranches.Count -eq 1) "branch_registry must expose one realtime prediction branch"
    Assert-True ($predictionBranches.Count -gt 0) "branch_registry has no prediction branches"
    foreach ($branch in $predictionBranches) {
        Assert-True ([string]$branch.parent_branch_id -eq "main.realtime_prediction") "future prediction branch must be parented by realtime prediction branch"
    }

    $artifactRefs = @($timelineIndex.artifact_refs)
    $fieldRefs = @($artifactRefs | Where-Object { $_.representation -eq "artifact_ref" -and [int]$_.node_count -gt 0 })
    $onlineArtifactRefs = @($artifactRefs | Where-Object { $_.branch_id -eq "main.online" })
    $onlineFieldRefs = @($fieldRefs | Where-Object { $_.branch_id -eq "main.online" })
    $realtimeFieldRefs = @($fieldRefs | Where-Object { $_.branch_id -eq "main.realtime_prediction" })
    $futureFieldRefs = @($fieldRefs | Where-Object { $_.branch_id -like "predict.*" })
    $qoiRefs = @($timelineIndex.qoi_refs)
    Assert-True ($artifactRefs.Count -gt 0) "timeline index has no artifact refs"
    Assert-True ($fieldRefs.Count -gt 0) "timeline index has no full field artifact refs"
    Assert-True ($onlineArtifactRefs.Count -eq 0) "online filtering branch must only expose fusion frames"
    Assert-True ($onlineFieldRefs.Count -eq 0) "online filtering branch must not own field artifacts"
    Assert-True ($realtimeFieldRefs.Count -gt 0) "realtime prediction branch has no field artifacts"
    Assert-True ($futureFieldRefs.Count -gt 0) "future prediction branches have no field artifacts"
    Assert-True ($qoiRefs.Count -gt 0) "timeline index has no qoi refs"

    $branchSteps = @($timelineIndex.branch_steps)
    $onlineSteps = @($branchSteps | Where-Object { $_.branch_id -eq "main.online" })
    $realtimeSteps = @($branchSteps | Where-Object { $_.branch_id -eq "main.realtime_prediction" })
    $futureSteps = @($branchSteps | Where-Object { $_.branch_id -like "predict.*" })
    Assert-True ($onlineSteps.Count -eq 0) "online filtering branch must not own runtime operator steps"
    Assert-True ($realtimeSteps.Count -gt 0) "realtime prediction branch has no timeline points"
    Assert-True (($realtimeSteps | Where-Object { $_.step_role -eq "online_operator_step" }).Count -gt 0) "realtime prediction branch has no online operator steps"
    Assert-True (($frames | Where-Object { $_.display_role -eq "online_filter.fusion_frame" }).Count -eq $frames.Count) "online frames must be tagged as fusion frames"
    Assert-True (($futureSteps | Where-Object { $null -ne $_.altitude_m -and $null -ne $_.state_time_s }).Count -gt 0) "future prediction steps must carry time/altitude labels"

    if ($null -ne $cursor) {
        Assert-True ($null -ne $cursor.follow_live) "runtime cursor must declare follow_live"
    }

    $predictionRuns = @($summary.prediction.runs)
    if ($predictionRuns.Count -eq 0) {
        $predictionRuns = $predictionBranches
    }
    foreach ($run in $predictionRuns) {
        Assert-True (Test-Path -LiteralPath ([string]$run.run_dir) -PathType Container) "prediction run dir missing: $($run.run_dir)"
    }
}
else {
    $onlineRunDir = [string]$summary.online.run_dir
    Assert-True (Test-Path -LiteralPath $onlineRunDir -PathType Container) "online run dir missing: $onlineRunDir"

    $sensorStream = Read-Json (Join-Path $onlineRunDir "sensor_stream.json")
    $nodeSnapshot = Read-Json (Join-Path $onlineRunDir "runtime_node_snapshot.json")
    $schedulerTimeline = Read-Json (Join-Path $onlineRunDir "scheduler_timeline.json")
    $runtimeLoop = Read-Json (Join-Path $onlineRunDir "runtime_loop_summary.json")

    $frames = @($sensorStream.frames)
    Assert-True ($frames.Count -gt 0) "sensor_stream has no online frames"
    Assert-True ($frames.Count -eq [int]$summary.online.sensor_frame_count) "sensor_stream frame count mismatch"
    Assert-True ([int]$sensorStream.summary.sensor_count_min -gt 0) "sensor_count_min missing"
    Assert-True ([int]$sensorStream.summary.sensor_count_min -eq [int]$sensorStream.summary.sensor_count_max) "sensor_count should be stable in current replay"

    $pfNodes = @($nodeSnapshot.nodes | Where-Object { $_.operator_id -eq "reentry.particle_filter.atomic.v1" })
    Assert-True ($pfNodes.Count -eq $frames.Count) "PF node count must match online frames"
    foreach ($node in $pfNodes) {
        $posterior = $node.runtime_packet.payload.outputs.'state.posterior'
        Assert-True ($null -ne $posterior) "PF posterior output missing"
        Assert-True ($null -ne $posterior.uncertainty.effective_sample_size) "PF ESS missing"
        Assert-True ($null -ne $posterior.uncertainty.max_abs_residual) "PF residual summary missing"
    }

    $events = @($schedulerTimeline.events)
    Assert-True ($events.Count -gt 0) "scheduler_timeline has no events"
    Assert-True ((@($events | Where-Object { $_.event -eq "tick" })).Count -gt 0) "scheduler_timeline has no tick events"
    Assert-True ((@($events | Where-Object { $_.event -eq "start" })).Count -gt 0) "scheduler_timeline has no start events"
    Assert-True ((@($events | Where-Object { $_.event -eq "finish" })).Count -gt 0) "scheduler_timeline has no finish events"
    Assert-True ([int]$runtimeLoop.summary.iteration_count -eq $frames.Count) "runtime loop iteration count mismatch"

    $predictionRuns = @($summary.prediction.runs)
    Assert-True ($predictionRuns.Count -gt 0) "mainline summary has no prediction runs"
    foreach ($run in $predictionRuns) {
        Assert-True (Test-Path -LiteralPath ([string]$run.run_dir) -PathType Container) "prediction run dir missing: $($run.run_dir)"
        $loop = Read-Json (Join-Path ([string]$run.run_dir) "runtime_loop_summary.json")
        Assert-True ([int]$loop.summary.iteration_count -gt 0) "prediction runtime loop has no steps"
        Assert-True ([int]$run.iteration_count -eq [int]$loop.summary.iteration_count) "prediction iteration count mismatch"
    }
}

$liveDataHub = Read-Text (Join-Path $WorkspaceRoot "flightenv-platform-ui\EnvTwinWorkbench\datahub\LiveDataHub.cpp")
foreach ($token in @(
    "BranchRunExplorerReader",
    "branch_registry.json",
    "run_timeline_index.json",
    "runtime_cursor.json",
    "mainline_summary.json",
    "sensor_stream.json",
    "runtime_node_snapshot.json",
    "scheduler_timeline.json",
    "mainline_progress.json",
    "mainline_progress",
    "completed_frames",
    "clock",
    "latestRuntimeRunDirUnder",
    "platform_online_run",
    "field_artifact_options",
    "timeline_points",
    "public_time_s",
    "time_summary",
    "branch_descriptors",
    "realtime_prediction_frame",
    "view_mode",
    "prediction_runs",
    "effective_sample_size"
)) {
    Assert-True ($liveDataHub.Contains($token)) "LiveDataHub does not consume token: $token"
}

$onlinePage = Read-Text (Join-Path $WorkspaceRoot "flightenv-platform-ui\EnvTwinWorkbench\pages\OnlinePage.cpp")
foreach ($token in @(
    "BranchTreeWidget",
    "BranchTimelineWidget",
    "BranchStatePanel",
    "BranchFieldPanel",
    "BranchSeriesPanel",
    "applyBranchSnapshot",
    "selectTimelinePoint",
    "runtimeVal_",
    "online_frames",
    "prediction_runs",
    "timeline_points",
    "branch_descriptors",
    "latestTimeline_",
    "QProgressBar",
    "totalProgressBar_",
    "predictionProgressBar_",
    "onRunProgress",
    "prepareMainlineRequested",
    "clockProgressVal_",
    "initializationStatusVal_",
    "initTable_",
    "preflight_runs"
)) {
    Assert-True ($onlinePage.Contains($token)) "OnlinePage does not display token: $token"
}

foreach ($forbidden in @(
    "PdkDataPlaneReader",
    "resolveArtifactPath",
    "data_plane_manifest.json",
    "findRuntimeSnapshotPath",
    "latestFieldStepIndex",
    "loadFieldArtifactAsync",
    "VtkModelFieldWidget",
    "fieldTabButtons_",
    "renderEvidenceField",
    "latestFieldStepForBranch",
    "activePredictionBranchId_"
)) {
    Assert-True (-not $onlinePage.Contains($forbidden)) "OnlinePage still owns path assembly: $forbidden"
}

$replayPage = Read-Text (Join-Path $WorkspaceRoot "flightenv-platform-ui\EnvTwinWorkbench\pages\ReplayPage.cpp")
foreach ($token in @(
    "LiveDataHub",
    "BranchTreeWidget",
    "BranchTimelineWidget",
    "BranchStatePanel",
    "BranchFieldPanel",
    "BranchSeriesPanel",
    "applyBranchSnapshot",
    "setEvidenceRoot",
    "timelineUpdated"
)) {
    Assert-True ($replayPage.Contains($token)) "ReplayPage must reuse branch snapshot components: $token"
}

$branchWidgets = Read-Text (Join-Path $WorkspaceRoot "flightenv-platform-ui\EnvTwinWorkbench\widgets\BranchRunExplorerWidgets.cpp")
foreach ($token in @(
    "BranchFieldPanel",
    "VtkModelFieldWidget",
    "loadFieldArtifactAsync",
    "renderFlattenedValues",
    "runtime_snapshot_path",
    "artifact_ref",
    "fieldLoadSerial_",
    "fieldLoadInFlight_",
    "fieldLoadPending_",
    "publicTimeOf",
    "samePublicTime",
    "online_observation_time_s",
    "operator_internal_summary"
)) {
    Assert-True ($branchWidgets.Contains($token)) "Branch field VTK panel missing token: $token"
}
Assert-True (-not $branchWidgets.Contains("PdkFieldArtifactReader")) "BranchFieldPanel must not parse field artifact JSON on the UI side"

$branchReader = Read-Text (Join-Path $WorkspaceRoot "flightenv-platform-ui\EnvTwinWorkbench\datahub\BranchRunExplorer.cpp")
foreach ($token in @(
    "publicTimeValue",
    "timeSummaryFromItem",
    "public_time_s",
    "effective_delta_t_s_by_node",
    "held_outputs",
    "samePublicTime"
)) {
    Assert-True ($branchReader.Contains($token)) "BranchRunExplorer must preserve public materialized timeline semantics: $token"
}

$nativeRunnerPath = Join-Path $WorkspaceRoot "flightenv-platform-runtime\src\NativeWorkflowRunner.cpp"
if (Test-Path -LiteralPath $nativeRunnerPath -PathType Leaf) {
    $nativeRunner = Read-Text $nativeRunnerPath
    foreach ($token in @(
        '"public_time_s"',
        '"effective_delta_t_s"',
        '"output_period_s"',
        '"time_summary"'
    )) {
        Assert-True ($nativeRunner.Contains($token)) "NativeWorkflowRunner data-plane timing metadata missing: $token"
    }
}

$runController = Read-Text (Join-Path $WorkspaceRoot "flightenv-platform-ui\EnvTwinWorkbench\datahub\PlatformRunController.cpp")
foreach ($token in @(
    "FlightEnvPlatformRuntimeHost.exe",
    "loadObjectRuntimeProfile",
    "workflowIdForRole",
    "profileIdForRole",
    "compiledWorkflowRootFromProfile",
    "compile_online_workflow",
    "compile_future_workflow",
    "native_adapter_sessions",
    "external-observation-stream",
    "replay-by-platform-clock",
    "preflight-adapters",
    "mainline_progress.json",
    "resolveControllerRuntimeSnapshotPath",
    "emitSyntheticProgress",
    "prepareDefaultMainline",
    "QProcess",
    "progressUpdated"
)) {
    Assert-True ($runController.Contains($token)) "PlatformRunController missing token: $token"
}

Write-Host "[OK] EnvTwinWorkbench runtime UI smoke passed."
Write-Host "mainline=$summaryPath"
Write-Host "online_frames=$($frames.Count)"
Write-Host "prediction_runs=$($predictionRuns.Count)"
