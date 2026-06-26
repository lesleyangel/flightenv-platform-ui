param(
    [Parameter(Mandatory = $true)]
    [string]$Version,

    [ValidateSet('Release', 'Debug')]
    [string]$Configuration = 'Release',

    [ValidateSet('x64', 'Win32')]
    [string]$Platform = 'x64',

    [string]$ArtifactRepo = 'lesleyangel/flightenv-artifacts',

    [string]$OutputRoot = '',

    [string]$WorkspaceRoot = '',

    [string]$ReleaseName = '',

    [string]$Body = '',

    [string]$Token = '',

    [switch]$SkipPackage,

    [switch]$SkipUpload,

    [switch]$Overwrite,

    [switch]$Draft,

    [switch]$Prerelease
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = [System.IO.Path]::GetFullPath((Join-Path $repoRoot '..\_local_artifacts\local-release\' + $Version))
}
else {
    $OutputRoot = [System.IO.Path]::GetFullPath($OutputRoot)
}

$packageName = "flightenv-controller-ui-$Version-win-$Platform-$Configuration"
$zipPath = Join-Path $OutputRoot ($packageName + '.zip')

if (-not $SkipPackage) {
    $packageArgs = @(
        '-NoProfile',
        '-ExecutionPolicy', 'Bypass',
        '-File', (Join-Path $repoRoot 'tools\package_controller_ui.ps1'),
        '-Configuration', $Configuration,
        '-Platform', $Platform,
        '-OutputRoot', $OutputRoot,
        '-PackageName', $packageName,
        '-Clean'
    )
    if (-not [string]::IsNullOrWhiteSpace($WorkspaceRoot)) { $packageArgs += @('-WorkspaceRoot', $WorkspaceRoot) }
    if ($Overwrite) { $packageArgs += '-Overwrite' }
    & powershell @packageArgs
    if ($LASTEXITCODE -ne 0) {
        throw "Controller UI packaging failed: $packageName"
    }
}

if ($SkipUpload) {
    Write-Host "Controller UI package ready; upload skipped: $zipPath"
    return
}

if (-not (Test-Path -LiteralPath $zipPath -PathType Leaf)) {
    throw "Controller UI zip not found: $zipPath"
}

$publishArgs = @(
    '-NoProfile',
    '-ExecutionPolicy', 'Bypass',
    '-File', (Join-Path $repoRoot 'tools\publish_github_release_asset.ps1'),
    '-Repository', $ArtifactRepo,
    '-Tag', $Version,
    '-AssetPath', $zipPath
)
if (-not [string]::IsNullOrWhiteSpace($ReleaseName)) { $publishArgs += @('-ReleaseName', $ReleaseName) }
if (-not [string]::IsNullOrWhiteSpace($Body)) { $publishArgs += @('-Body', $Body) }
if (-not [string]::IsNullOrWhiteSpace($Token)) { $publishArgs += @('-Token', $Token) }
if ($Draft) { $publishArgs += '-Draft' }
if ($Prerelease) { $publishArgs += '-Prerelease' }

& powershell @publishArgs
if ($LASTEXITCODE -ne 0) {
    throw "Controller UI upload failed: $ArtifactRepo $Version"
}
