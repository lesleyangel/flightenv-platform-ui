param(
    [string]$WorkspaceRoot = "",
    [string]$AcceptanceSummary = ""
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($WorkspaceRoot)) {
    $WorkspaceRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
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

function Get-StringArray {
    param($Value)
    if ($null -eq $Value) {
        return @()
    }
    return @($Value)
}

function Get-WorkflowOperatorRefs {
    param($Workflow)
    $refs = New-Object System.Collections.Generic.List[string]
    foreach ($phase in @($Workflow.phases)) {
        foreach ($stage in @($phase.stages)) {
            foreach ($node in @($stage.subgraph.nodes)) {
                if (-not [string]::IsNullOrWhiteSpace($node.operator_ref)) {
                    $refs.Add([string]$node.operator_ref)
                }
            }
        }
    }
    return $refs
}

function Test-ObjectPackage {
    param([string]$Root)
    Assert-True (Test-Path -LiteralPath $Root -PathType Container) "object package missing: $Root"
    $twin = Read-Json (Join-Path $Root "object\twin_object.json")
    $resources = Read-Json (Join-Path $Root "assets\resources.json")
    Assert-True (-not [string]::IsNullOrWhiteSpace($twin.object_id)) "twin_object.object_id missing"
    Assert-True (@($resources.asset_groups).Count -gt 0) "resources.asset_groups missing"

    $operatorIds = New-Object "System.Collections.Generic.HashSet[string]"
    $operatorFiles = Get-ChildItem -LiteralPath (Join-Path $Root "operators") -Filter "*.atomic.json"
    Assert-True ($operatorFiles.Count -ge 21) "expected at least 21 operator specs"

    foreach ($file in $operatorFiles) {
        $operator = Read-Json $file.FullName
        Assert-True (-not [string]::IsNullOrWhiteSpace($operator.operator_id)) "operator_id missing: $($file.Name)"
        [void]$operatorIds.Add([string]$operator.operator_id)
        Assert-True ($null -ne $operator.display_descriptor) "display_descriptor missing: $($file.Name)"
        Assert-True (-not [string]::IsNullOrWhiteSpace($operator.display_descriptor.renderer_id)) "renderer_id missing: $($file.Name)"
        Assert-True (-not [string]::IsNullOrWhiteSpace($operator.display_descriptor.fallback_renderer)) "fallback_renderer missing: $($file.Name)"
        $outputs = @($operator.outputs | ForEach-Object { $_.port_id })
        foreach ($port in @($operator.display_descriptor.primary_outputs)) {
            Assert-True ($outputs -contains $port) "display primary output not found: $($operator.operator_id) -> $port"
        }
    }

    $workflowFiles = Get-ChildItem -LiteralPath (Join-Path $Root "workflows") -Filter "*.json"
    Assert-True ($workflowFiles.Count -gt 0) "workflow specs missing"
    foreach ($file in $workflowFiles) {
        $workflow = Read-Json $file.FullName
        foreach ($ref in Get-WorkflowOperatorRefs $workflow) {
            Assert-True ($operatorIds.Contains($ref)) "workflow references missing operator: $($file.Name) -> $ref"
        }
    }
}

function Test-CompiledWorkflow {
    param([string]$CompiledDir)
    $execution = Read-Json (Join-Path $CompiledDir "execution_plan.json")
    $activation = Read-Json (Join-Path $CompiledDir "activation_snapshot.json")
    $timePlan = Read-Json (Join-Path $CompiledDir "time_plan.json")
    $scheduler = Read-Json (Join-Path $CompiledDir "scheduler_plan.json")
    Assert-True ($execution.schema_version -eq "flightenv.platform.execution_plan.v1") "bad execution_plan schema"
    Assert-True ($activation.schema_version -eq "flightenv.platform.activation_snapshot.v1") "bad activation_snapshot schema"
    Assert-True ($timePlan.schema_version -eq "flightenv.platform.time_plan.v1") "bad time_plan schema"
    Assert-True ($scheduler.schema_version -eq "flightenv.platform.scheduler_plan.v1") "bad scheduler_plan schema"
    Assert-True (@($execution.nodes).Count -gt 0) "execution_plan.nodes empty"
    Assert-True (@($activation.nodes).Count -gt 0) "activation_snapshot.nodes empty"
    foreach ($node in @($execution.nodes)) {
        Assert-True ($null -ne $node.display_descriptor) "compiled node missing display_descriptor: $($node.node_id)"
    }
}

function Test-RuntimeEvidence {
    param([string]$SummaryPath)
    $summary = Read-Json $SummaryPath
    $onlineRun = [string]$summary.online.run_dir
    Assert-True (Test-Path -LiteralPath $onlineRun -PathType Container) "online run missing: $onlineRun"
    $sensorStream = Read-Json (Join-Path $onlineRun "sensor_stream.json")
    Assert-True ($sensorStream.schema_version -eq "flightenv.platform.sensor_stream.v1") "bad sensor_stream schema"
    Assert-True (@($sensorStream.frames).Count -eq [int]$summary.online.frame_count) "sensor frame count mismatch"

    $prediction = @($summary.predictions)[0]
    $predictionRun = [string]$prediction.run_dir
    Assert-True (Test-Path -LiteralPath $predictionRun -PathType Container) "prediction run missing: $predictionRun"
    $health = Read-Json (Join-Path $predictionRun "health_trend_summary.json")
    Assert-True ($health.schema_version -eq "flightenv.platform.health_trend_summary.v1") "bad health trend schema"
    Assert-True ([int]$health.iteration_count -gt 1) "health trend has no multi-step prediction"
    $dataPlane = Read-Json (Join-Path $predictionRun "data_plane_manifest.json")
    Assert-True ($dataPlane.schema_version -eq "flightenv.platform.data_plane_manifest.v1") "bad data plane schema"
    Assert-True (@($dataPlane.entries).Count -gt 0) "data_plane_manifest.entries empty"
}

function Test-NegativeCases {
    param([string]$WorkspaceRoot)
    $scratch = Join-Path $WorkspaceRoot ("_local_artifacts\platform-ui-acceptance\tp2_facade_negative_" + [guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Force -Path $scratch | Out-Null

    $missingPackage = Join-Path $scratch "missing_object_package"
    Assert-True (-not (Test-Path -LiteralPath $missingPackage)) "negative missing object package setup failed"

    $badPackage = Join-Path $scratch "bad_object_package"
    New-Item -ItemType Directory -Force -Path (Join-Path $badPackage "object") | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $badPackage "assets") | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $badPackage "operators") | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $badPackage "workflows") | Out-Null
    '{"object_id":"bad_object"}' | Set-Content -LiteralPath (Join-Path $badPackage "object\twin_object.json") -Encoding UTF8
    '{"object_id":"bad_object","asset_groups":[]}' | Set-Content -LiteralPath (Join-Path $badPackage "assets\resources.json") -Encoding UTF8
    '{"workflow_id":"bad.workflow","object_id":"bad_object","phase":"test","phases":[{"phase_id":"p","stages":[{"stage_id":"s","subgraph":{"nodes":[{"node_id":"n","operator_ref":"missing.operator"}]}}]}]}' |
        Set-Content -LiteralPath (Join-Path $badPackage "workflows\bad.json") -Encoding UTF8
    $badWorkflow = Read-Json (Join-Path $badPackage "workflows\bad.json")
    $operatorIds = New-Object "System.Collections.Generic.HashSet[string]"
    $missingRefs = @(Get-WorkflowOperatorRefs $badWorkflow | Where-Object { -not $operatorIds.Contains($_) })
    Assert-True ($missingRefs -contains "missing.operator") "negative missing operator was not detected"

    $badDataPlane = Join-Path $scratch "bad_data_plane_manifest.json"
    '{"schema_version":"flightenv.platform.data_plane_manifest.v1","entries":[{"representation":"artifact_ref","port_id":"field.temperature"}]}' |
        Set-Content -LiteralPath $badDataPlane -Encoding UTF8
    $manifest = Read-Json $badDataPlane
    $badArtifactRefs = @($manifest.entries | Where-Object { $_.representation -eq "artifact_ref" -and [string]::IsNullOrWhiteSpace($_.ref) -and [string]::IsNullOrWhiteSpace($_.artifact_uri) })
    Assert-True ($badArtifactRefs.Count -eq 1) "negative missing artifact ref was not detected"

    $badExecutionPlan = Join-Path $scratch "bad_execution_plan.json"
    '{"schema_version":"flightenv.platform.execution_plan.v999","nodes":[]}' |
        Set-Content -LiteralPath $badExecutionPlan -Encoding UTF8
    $plan = Read-Json $badExecutionPlan
    Assert-True ($plan.schema_version -ne "flightenv.platform.execution_plan.v1") "negative schema mismatch was not detected"
}

$objectPackage = Join-Path $WorkspaceRoot "flightenv-object-reentry-vehicle"
$compiledWorkflow = Join-Path $WorkspaceRoot "_local_artifacts\platform-ui-acceptance\tp4_run_profiles\reentry.posterior_future_prediction.v1.trajectory_fields_health\reentry.posterior_future_prediction.v1"
if (-not (Test-Path -LiteralPath (Join-Path $compiledWorkflow "activation_snapshot.json") -PathType Leaf)) {
    $compileScript = Join-Path $WorkspaceRoot "flightenv-platform-pdk\tools\compile_object_workflow.ps1"
    $compileOut = Join-Path $WorkspaceRoot "_local_artifacts\platform-ui-acceptance\tp4_ui_facade_compile"
    & powershell -NoProfile -ExecutionPolicy Bypass -File $compileScript `
        -ObjectPackage $objectPackage `
        -Workflow "posterior_future_prediction" `
        -RunProfile "trajectory_fields_health" `
        -OutDir $compileOut `
        -RunId "tp4_ui_facade"
    if ($LASTEXITCODE -ne 0) {
        throw "failed to compile TP4 workflow for UI facade smoke"
    }
    $compiledWorkflow = Join-Path $compileOut "reentry.posterior_future_prediction.v1"
}
$defaultAcceptanceSummary = Join-Path $WorkspaceRoot "_local_artifacts\platform-pdk\mainline-runs\platform_full_acceptance_20260613\full_acceptance_summary.json"
if ([string]::IsNullOrWhiteSpace($AcceptanceSummary)) {
    $AcceptanceSummary = $defaultAcceptanceSummary
}
$uiProject = Join-Path $WorkspaceRoot "flightenv-platform-ui\EnvTwinWorkbench\EnvTwinWorkbench.vcxproj"

Test-ObjectPackage $objectPackage
Test-CompiledWorkflow $compiledWorkflow
if (Test-Path -LiteralPath $AcceptanceSummary -PathType Leaf) {
    Test-RuntimeEvidence $AcceptanceSummary
}
else {
    Write-Warning "runtime evidence smoke skipped; acceptance summary not found: $AcceptanceSummary"
}
Test-NegativeCases $WorkspaceRoot

$projectXml = Get-Content -LiteralPath $uiProject -Raw -Encoding UTF8
Assert-True ($projectXml -notmatch "source-supported") "UI project must not compile flightenv-node-sdk/source-supported directly"

Write-Host "[OK] PDK UI facade smoke passed."
Write-Host "object_package=$objectPackage"
Write-Host "compiled_workflow=$compiledWorkflow"
Write-Host "acceptance_summary=$AcceptanceSummary"
