param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",

    [ValidateSet("x64", "Win32")]
    [string]$Platform = "x64",

    [string]$QtInstall = "",

    [string]$WorkspaceRoot = "",

    [string]$SharedDepsRoot = "",

    [switch]$VerboseMSBuild
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$projectPath = Join-Path $repoRoot "EnvTwinWorkbench\EnvTwinWorkbench.vcxproj"
. (Join-Path $repoRoot "tools\FlightEnvDepsWorkspace.ps1")

function Resolve-MSBuild {
    if ($env:MSBUILD_EXE -and (Test-Path $env:MSBUILD_EXE)) {
        return (Resolve-Path $env:MSBUILD_EXE).Path
    }

    $vswhereCandidates = @(
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe",
        "$env:ProgramFiles\Microsoft Visual Studio\Installer\vswhere.exe"
    )

    foreach ($vswhere in $vswhereCandidates) {
        if (-not (Test-Path $vswhere)) {
            continue
        }
        $candidate = & $vswhere -latest -products "*" -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1
        if ($candidate -and (Test-Path $candidate)) {
            return $candidate
        }
    }

    $pathCandidate = Get-Command MSBuild.exe -ErrorAction SilentlyContinue
    if ($pathCandidate -and (Test-Path $pathCandidate.Source)) {
        return $pathCandidate.Source
    }

    $fallbacks = @(
        "$env:ProgramFiles\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe",
        "$env:ProgramFiles\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe",
        "$env:ProgramFiles\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe",
        "$env:ProgramFiles\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
    )

    foreach ($candidate in $fallbacks) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }

    throw "MSBuild not found. Install Visual Studio Build Tools, add MSBuild.exe to PATH, or set MSBUILD_EXE."
}

function Resolve-QtInstallDir {
    param([string]$QtInstallHint)

    if ($QtInstallHint -and (Test-Path $QtInstallHint)) {
        return (Resolve-Path $QtInstallHint).Path
    }
    if ($env:QT6_ROOT -and (Test-Path $env:QT6_ROOT)) {
        return (Resolve-Path $env:QT6_ROOT).Path
    }
    if ($env:QTDIR -and (Test-Path $env:QTDIR)) {
        return (Resolve-Path $env:QTDIR).Path
    }

    foreach ($candidate in @(
        "C:\Qt6\6.2.0\msvc2019_64",
        "C:\Qt\6.2.0\msvc2019_64",
        "C:\Qt\6.2.0\msvc2022_64",
        "C:\Qt6\6.2.0\msvc2022_64"
    )) {
        if (Test-Path (Join-Path $candidate "bin\qmake.exe")) {
            return (Resolve-Path $candidate).Path
        }
    }

    throw "Unable to resolve Qt. Pass -QtInstall <path> or set QT6_ROOT/QTDIR."
}

function Add-ProcessPath {
    param([Parameter(Mandatory = $true)][string]$PathValue)

    $currentPath = [System.Environment]::GetEnvironmentVariable("Path", "Process")
    if ($currentPath -notlike "*$PathValue*") {
        [System.Environment]::SetEnvironmentVariable("Path", "$PathValue;$currentPath", "Process")
    }
}

function Normalize-ProcessPathEnvironment {
    $envMap = [System.Environment]::GetEnvironmentVariables()
    $pathValue = $envMap["Path"]
    if ([string]::IsNullOrEmpty($pathValue)) {
        $pathValue = $envMap["PATH"]
    }
    if (-not [string]::IsNullOrEmpty($pathValue)) {
        [System.Environment]::SetEnvironmentVariable("PATH", $null, "Process")
        [System.Environment]::SetEnvironmentVariable("Path", $pathValue, "Process")
    }
}

if (-not (Test-Path -LiteralPath $projectPath -PathType Leaf)) {
    throw "EnvTwinWorkbench project not found: $projectPath"
}

$msbuild = Resolve-MSBuild
Normalize-ProcessPathEnvironment
$qtInstallDir = Resolve-QtInstallDir -QtInstallHint $QtInstall
$qtBinDir = Join-Path $qtInstallDir "bin"
foreach ($tool in @("rcc.exe", "uic.exe", "moc.exe", "qmake.exe")) {
    $toolPath = Join-Path $qtBinDir $tool
    if (-not (Test-Path $toolPath)) {
        throw "Required Qt tool not found: $toolPath"
    }
}

$workspaceRoot = Get-FlightEnvSharedWorkspaceRoot `
    -RepoRoot $repoRoot `
    -SharedDepsRoot $SharedDepsRoot `
    -PreferredWorkspaceRoot $WorkspaceRoot
Ensure-FlightEnvDirectory -PathValue $workspaceRoot

$sharedRoot = Get-FlightEnvSharedDepsRoot -RepoRoot $repoRoot -PreferredRoot $SharedDepsRoot
$sdkLib = Join-Path -Path $workspaceRoot -ChildPath ("{0}\{1}\FlightEnvNodeSdk.lib" -f $Platform, $Configuration)
if (-not (Test-Path -LiteralPath $sdkLib -PathType Leaf)) {
    throw "FlightEnvNodeSdk.lib is missing: $sdkLib. Build or install node-sdk artifacts first."
}

$env:QTDIR = $qtInstallDir
$env:FLIGHTENV_DEPS_WORKSPACE_ROOT = $workspaceRoot
Add-ProcessPath -PathValue $qtBinDir

Write-Host "Using MSBuild: $msbuild"
Write-Host "Using Qt install: $qtInstallDir"
Write-Host "Using dependency workspace: $workspaceRoot"
Write-Host "Using shared deps: $sharedRoot"
Write-Host "Building platform-native workbench: $projectPath"

$msbuildArgs = @(
    $projectPath,
    "/m",
    "/p:Configuration=$Configuration",
    "/p:Platform=$Platform",
    "/p:QtInstall=$qtInstallDir",
    "/p:FlightEnvDepsWorkspaceRoot=$workspaceRoot",
    "/p:FlightEnvSharedDepsRoot=$sharedRoot"
)

if ($VerboseMSBuild) {
    $msbuildArgs += "/v:normal"
}
else {
    $msbuildArgs += "/v:minimal"
}

& $msbuild @msbuildArgs
if ($LASTEXITCODE -ne 0) {
    throw "EnvTwinWorkbench build failed."
}
