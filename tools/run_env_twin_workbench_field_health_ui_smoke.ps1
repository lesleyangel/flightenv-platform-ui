param(
    [string]$WorkspaceRoot = "",
    [string]$MainlineRunRoot = ""
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($WorkspaceRoot)) {
    $WorkspaceRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
}
if ([string]::IsNullOrWhiteSpace($MainlineRunRoot)) {
    $MainlineRunRoot = Join-Path $WorkspaceRoot "_local_artifacts\platform-pdk\mainline-runs\platform_ui_tp8_field_health_20260614"
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
    return Get-Content -LiteralPath $Path -Raw -Encoding UTF8 | ConvertFrom-Json
}

function Read-Text {
    param([string]$Path)
    Assert-True (Test-Path -LiteralPath $Path -PathType Leaf) "missing file: $Path"
    return Get-Content -LiteralPath $Path -Raw -Encoding UTF8
}

$summaryPath = Join-Path $MainlineRunRoot "full_acceptance_summary.json"
$summary = Read-Json $summaryPath
Assert-True ($summary.acceptance.every_future_step_has_required_fields -eq $true) "full acceptance does not prove every future step has fields"
Assert-True ($summary.acceptance.damage_non_decreasing -eq $true) "damage trend is not non-decreasing"
Assert-True ($summary.acceptance.rul_non_increasing -eq $true) "RUL trend is not non-increasing"
Assert-True (Test-Path -LiteralPath (Join-Path $MainlineRunRoot "runtime_snapshot.json") -PathType Leaf) "mainline runtime_snapshot missing"

$prediction = @($summary.predictions)[0]
$predictionRunDir = [string]$prediction.run_dir
Assert-True (Test-Path -LiteralPath $predictionRunDir -PathType Container) "prediction run missing: $predictionRunDir"
Assert-True (Test-Path -LiteralPath (Join-Path $predictionRunDir "runtime_snapshot.json") -PathType Leaf) "prediction runtime_snapshot missing"

$health = Read-Json (Join-Path $predictionRunDir "health_trend_summary.json")
Assert-True ([int]$health.iteration_count -eq [int]$prediction.iteration_count) "health iteration count mismatch"
Assert-True ($health.damage_non_decreasing -eq $true) "health damage trend is not non-decreasing"
Assert-True ($health.rul_non_increasing -eq $true) "health RUL trend is not non-increasing"

$lastStep = [int]$health.iteration_count - 1
$stepDir = Join-Path $predictionRunDir ("field_artifacts\step_{0:D6}" -f $lastStep)
Assert-True (Test-Path -LiteralPath $stepDir -PathType Container) "last field step dir missing: $stepDir"

$fieldSpecs = @(
    @{ name = "pressure"; file = "future_step.state_transition.pressure_field.pressure.json"; node_count = 9006 },
    @{ name = "heatflux"; file = "future_step.state_transition.heatflux_field.heatflux.json"; node_count = 9006 },
    @{ name = "strain"; file = "future_step.state_transition.strain_field.strain.json"; node_count = 24385 },
    @{ name = "temperature"; file = "future_step.state_transition.temperature_field.temperature.json"; node_count = 24385 },
    @{ name = "damage"; file = "future_step.state_transition.structure_damage.damage.json"; node_count = 24385 },
    @{ name = "ablation"; file = "future_step.state_transition.tps_ablation.ablation.json"; node_count = 9006 },
    @{ name = "rul"; file = "future_step.failure_qoi.remaining_life.rul.json"; node_count = 24385; optional = $true }
)

foreach ($spec in $fieldSpecs) {
    $fieldPath = Join-Path $stepDir $spec.file
    if (($spec.optional -eq $true) -and -not (Test-Path -LiteralPath $fieldPath -PathType Leaf)) {
        continue
    }
    $field = Read-Json $fieldPath
    Assert-True ([string]$field.frame_contract -eq "FieldTensor.v1") "field contract mismatch: $($spec.name)"
    Assert-True ([string]$field.field_name -eq [string]$spec.name) "field name mismatch: $($spec.name)"
    Assert-True ([int]$field.node_count -eq [int]$spec.node_count) "field node count mismatch: $($spec.name)"
    Assert-True ($null -ne $field.statistics.max) "field statistics missing: $($spec.name)"
}

$healthPage = Read-Text (Join-Path $WorkspaceRoot "flightenv-platform-ui\EnvTwinWorkbench\pages\HealthPage.cpp")
foreach ($token in @(
    "PdkHealthTrendReader",
    "PdkDataPlaneReader",
    "healthDisplayOutputsForWorkflow",
    "selectHealthField",
    "display_outputs",
    "health.default_field",
    "loadFieldArtifactAsync",
    "fieldRenderHintFromEntry",
    "runtime_snapshot.json",
    "renderFlattenedValues",
    "health_trend_summary.json",
    "data_plane",
    "metricKeysFor",
    "runtimeProfileJson"
)) {
    Assert-True ($healthPage.Contains($token)) "HealthPage does not consume token: $token"
}
foreach ($forbidden in @(
    "subjectForArtifact",
    "subjectForArtifactPath",
    "subjectForField",
    "subjectForFieldName",
    "preferredFieldPath",
    "stepDirName",
    "future_step.state_transition.structure_damage.damage.json",
    "future_step.failure_qoi.remaining_life.rul.json",
    "FieldRenderHint{}"
)) {
    Assert-True (-not $healthPage.Contains($forbidden)) "HealthPage must not guess legacy subject: $forbidden"
}

$workflowPath = Join-Path $WorkspaceRoot "flightenv-object-reentry-vehicle\workflows\posterior_future_prediction.json"
$workflow = Read-Json $workflowPath
$defaultDisplay = @($workflow.display_outputs) | Where-Object { [string]$_.role -eq "health.default_field" } | Select-Object -First 1
Assert-True ($null -ne $defaultDisplay) "posterior workflow must declare health.default_field display output"
Assert-True ([string]$defaultDisplay.renderer_id -eq "field.vtk.scalar.v1") "health.default_field must use field renderer"
Assert-True ([string]$defaultDisplay.port_id -eq "field.rul") "health.default_field must select RUL field by workflow output role"

$operatorSpec = Read-Json (Join-Path $WorkspaceRoot "flightenv-object-reentry-vehicle\operators\vehicle_remaining_life_estimator.atomic.json")
$rulOutput = @($operatorSpec.outputs) | Where-Object { [string]$_.port_id -eq "field.rul" } | Select-Object -First 1
Assert-True ($null -ne $rulOutput) "remaining life operator must declare field.rul output"
Assert-True ([string]$rulOutput.contract_id -eq "reentry.remaining_life_field.v1") "field.rul contract mismatch"

$vtkWidget = Read-Text (Join-Path $WorkspaceRoot "flightenv-platform-ui\EnvTwinWorkbench\widgets\VtkModelFieldWidget.cpp")
Assert-True ($vtkWidget.Contains("renderFlattenedValues")) "VTK widget must render platform artifact values"
Assert-True ($vtkWidget.Contains("expectedValueCount")) "VTK widget must enforce exact field value count"

$fieldGuard = Read-Text (Join-Path $WorkspaceRoot "flightenv-platform-ui\EnvTwinWorkbench\datahub\FieldRenderGuard.cpp")
foreach ($token in @(
    "bindFieldArtifactForRendering",
    "artifact.values.size() != artifactNodeCount",
    "nodeCountMatches.empty()"
)) {
    Assert-True ($fieldGuard.Contains($token)) "FieldRenderGuard missing U2 binding check: $token"
}

Write-Host "[OK] EnvTwinWorkbench field/health UI smoke passed."
Write-Host "mainline=$summaryPath"
Write-Host "prediction=$predictionRunDir"
Write-Host "last_step=$lastStep"
