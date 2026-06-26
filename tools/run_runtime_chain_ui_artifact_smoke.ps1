param(
    [string]$WorkspaceRoot = '',

    [string]$ReportPath = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Resolve-PathValue {
    param(
        [Parameter(Mandatory = $true)][string]$PathValue,
        [Parameter(Mandatory = $true)][string]$BasePath
    )

    if ([System.IO.Path]::IsPathRooted($PathValue)) {
        return [System.IO.Path]::GetFullPath($PathValue)
    }
    return [System.IO.Path]::GetFullPath((Join-Path -Path $BasePath -ChildPath $PathValue))
}

function Read-Json {
    param([Parameter(Mandatory = $true)][string]$PathValue)
    if (-not (Test-Path -LiteralPath $PathValue -PathType Leaf)) {
        throw "JSON file not found: $PathValue"
    }
    return Get-Content -LiteralPath $PathValue -Raw -Encoding UTF8 | ConvertFrom-Json
}

function Assert-StatusObject {
    param(
        [Parameter(Mandatory = $true)]$Object,
        [Parameter(Mandatory = $true)][string]$Name
    )

    $status = [string]$Object.status
    if ([string]::IsNullOrWhiteSpace($status)) {
        throw "$Name missing status"
    }
    $reason = [string]$Object.reason
    return [ordered]@{
        status = $status
        reason = $reason
    }
}

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$workspaceHome = [System.IO.Path]::GetFullPath((Join-Path $repoRoot '..'))

if ([string]::IsNullOrWhiteSpace($WorkspaceRoot)) {
    $WorkspaceRoot = $workspaceHome
}
$WorkspaceRoot = Resolve-PathValue -PathValue $WorkspaceRoot -BasePath $workspaceHome

if ([string]::IsNullOrWhiteSpace($ReportPath)) {
    $ReportPath = Join-Path $WorkspaceRoot '_local_artifacts\flightenv-controller-ui\runtime-chain-ui-smoke\ui_runtime_chain_report.json'
}
$ReportPath = Resolve-PathValue -PathValue $ReportPath -BasePath $WorkspaceRoot
$reportDir = [System.IO.Path]::GetDirectoryName($ReportPath)
if (-not (Test-Path -LiteralPath $reportDir -PathType Container)) {
    New-Item -ItemType Directory -Path $reportDir -Force | Out-Null
}

$openChainRoot = Join-Path $WorkspaceRoot '_local_artifacts\runtime-private\open-chain-stub'
if (-not (Test-Path -LiteralPath $openChainRoot -PathType Container)) {
    throw "Open-chain artifact root not found: $openChainRoot"
}
$summaryFile = Get-ChildItem -LiteralPath $openChainRoot -Filter 'open_chain_summary.json' -File -Recurse |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1
if ($null -eq $summaryFile) {
    throw "No open_chain_summary.json found under $openChainRoot"
}

$ledgerSummaryPath = Join-Path $WorkspaceRoot '_local_artifacts\runtime-private\health-ledger-smoke\health_ledger_summary.json'
$openChain = Read-Json -PathValue $summaryFile.FullName
$ledger = Read-Json -PathValue $ledgerSummaryPath

$trajectory = Assert-StatusObject -Object $openChain.trajectory -Name 'trajectory'
$field = Assert-StatusObject -Object $openChain.field -Name 'field'
$damage = Assert-StatusObject -Object $openChain.damage -Name 'damage'
$life = Assert-StatusObject -Object $openChain.life -Name 'life'

if ([int]$openChain.trajectory.sample_count -lt 2) {
    throw 'Trajectory summary must expose at least two future samples for UI display'
}
if ([int]$openChain.field.field_count -lt 1) {
    throw 'Field summary must expose at least one field item for UI display'
}
if ([int]$openChain.damage.increment_count -lt 1) {
    throw 'Damage summary must expose at least one increment for UI display'
}
if ($null -eq $ledger.object_id -or [string]::IsNullOrWhiteSpace([string]$ledger.object_id)) {
    throw 'Health ledger summary missing object_id'
}
if ([int]$ledger.correction_count -lt 1) {
    throw 'Health ledger summary must include at least one maintenance correction'
}
if ([string]$ledger.current_status -eq 'ok' -and [double]$ledger.current_rul_s -lt 0.0) {
    throw 'Health ledger summary reports ok status with negative RUL'
}
if ([string]$ledger.current_status -ne 'ok' -and [string]::IsNullOrWhiteSpace([string]$ledger.current_reason)) {
    throw 'Unknown or failed health status must carry a reason'
}

$report = [ordered]@{
    ok = $true
    source = 'flightenv-controller-ui.runtime-chain-artifact-smoke'
    open_chain_summary = $summaryFile.FullName
    health_ledger_summary = $ledgerSummaryPath
    display_rows = [ordered]@{
        trajectory = $trajectory
        field = $field
        damage = $damage
        life = $life
    }
    health_ledger = [ordered]@{
        object_id = [string]$ledger.object_id
        damage_count = [int]$ledger.damage_count
        life_count = [int]$ledger.life_count
        correction_count = [int]$ledger.correction_count
        current_status = [string]$ledger.current_status
        current_rul_s = [double]$ledger.current_rul_s
        current_reason = [string]$ledger.current_reason
    }
}
$report | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $ReportPath -Encoding UTF8

Write-Host 'Runtime chain UI artifact smoke passed.'
Write-Host "  report = $ReportPath"
