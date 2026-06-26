param(
    [string]$WorkspaceRoot = "",
    [string]$OnlineMainlineRunRoot = "",
    [string]$FieldHealthMainlineRunRoot = ""
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($WorkspaceRoot)) {
    $WorkspaceRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
}

if ([string]::IsNullOrWhiteSpace($OnlineMainlineRunRoot)) {
    $OnlineMainlineRunRoot = Join-Path $WorkspaceRoot "_local_artifacts\platform-pdk\mainline-runs\platform_ui_tp7_online50_20260614"
}

if ([string]::IsNullOrWhiteSpace($FieldHealthMainlineRunRoot)) {
    $FieldHealthMainlineRunRoot = Join-Path $WorkspaceRoot "_local_artifacts\platform-pdk\mainline-runs\platform_ui_tp8_rul_field_20260614"
}

function Invoke-Step {
    param(
        [string]$Name,
        [scriptblock]$Script
    )
    Write-Host ""
    Write-Host "== $Name =="
    $global:LASTEXITCODE = 0
    & $Script
    if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) {
        throw "$Name failed with exit code $LASTEXITCODE"
    }
}

Invoke-Step -Name "Workbench skeleton and boundary smoke" -Script {
    & (Join-Path $PSScriptRoot "run_env_twin_workbench_skeleton_smoke.ps1") `
        -WorkspaceRoot $WorkspaceRoot
}

Invoke-Step -Name "Online timeline smoke" -Script {
    & (Join-Path $PSScriptRoot "run_env_twin_workbench_runtime_ui_smoke.ps1") `
        -WorkspaceRoot $WorkspaceRoot `
        -MainlineRunRoot $OnlineMainlineRunRoot
}

Invoke-Step -Name "Field health and RUL smoke" -Script {
    & (Join-Path $PSScriptRoot "run_env_twin_workbench_field_health_ui_smoke.ps1") `
        -WorkspaceRoot $WorkspaceRoot `
        -MainlineRunRoot $FieldHealthMainlineRunRoot
}

$outputDir = Join-Path $WorkspaceRoot "_deps\workspace\x64\Release"
foreach ($requiredRuntime in @(
    "EnvTwinWorkbench.exe",
    "Qt6Core.dll",
    "Qt6Gui.dll",
    "Qt6Widgets.dll",
    "Qt6OpenGL.dll",
    "platforms\qwindows.dll",
    "vtkCommonCore-9.4.dll",
    "vtkGUISupportQt-9.4.dll"
)) {
    $runtimePath = Join-Path $outputDir $requiredRuntime
    if (-not (Test-Path -LiteralPath $runtimePath -PathType Leaf)) {
        throw "EnvTwinWorkbench runtime file missing: $runtimePath. Run flightenv-platform-ui\tools\deploy_env_twin_workbench_runtime.ps1 after build."
    }
}

Write-Host ""
Write-Host "[OK] Platform UI smoke passed."
Write-Host "workspace=$WorkspaceRoot"
Write-Host "online_mainline=$OnlineMainlineRunRoot"
Write-Host "field_health_mainline=$FieldHealthMainlineRunRoot"
