param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",

    [ValidateSet("x64", "Win32")]
    [string]$Platform = "x64",

    [string]$TestFunction = "",

    [string]$QtRoot = "",

    [string]$RuntimeBinRoot = "",

    [string]$NodeSdkRoot = "",

    [string]$RuntimePrivateRoot = "",

    [switch]$SkipNodeSdkBuild,

    [switch]$IncludeNodeIntegration
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
. (Join-Path $repoRoot "tools\package\RuntimeLayoutHelpers.ps1")
$workspaceRoot = Get-FlightEnvDepsWorkspaceRoot -RepoRoot $repoRoot
$sharedDepsRoot = [System.IO.Path]::GetFullPath((Join-Path -Path $repoRoot -ChildPath "..\_deps"))
$env:FLIGHTENV_DEPS_WORKSPACE_ROOT = $workspaceRoot
$env:FLIGHTENV_SHARED_DEPS_ROOT = $sharedDepsRoot

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

    throw "MSBuild not found. Install Visual Studio Build Tools, add MSBuild.exe to PATH, or set MSBUILD_EXE."
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

Normalize-ProcessPathEnvironment
$msbuild = Resolve-MSBuild

function Resolve-QtRoot {
    param([string]$PreferredRoot)

    $candidates = @()
    if (-not [string]::IsNullOrWhiteSpace($PreferredRoot)) {
        $candidates += $PreferredRoot
    }
    if (-not [string]::IsNullOrWhiteSpace($env:QT6_ROOT)) {
        $candidates += $env:QT6_ROOT
    }
    if (-not [string]::IsNullOrWhiteSpace($env:QtInstall)) {
        $candidates += $env:QtInstall
    }

    foreach ($candidate in $candidates) {
        $bin = Join-Path $candidate "bin"
        if ((Test-Path -LiteralPath $candidate -PathType Container) -and
            (Test-Path -LiteralPath $bin -PathType Container)) {
            return (Resolve-Path $candidate).Path
        }
    }

    throw "Qt root not found. Set QT6_ROOT or pass -QtRoot to the Qt UI test runner."
}

$resolvedQtRoot = Resolve-QtRoot -PreferredRoot $QtRoot
$env:QT6_ROOT = $resolvedQtRoot
Write-Host "Using Qt root: $resolvedQtRoot"
if (-not [string]::IsNullOrWhiteSpace($RuntimePrivateRoot)) {
    Write-Host "Ignoring -RuntimePrivateRoot for UI tests; tests consume only binary runtime artifacts from the dependency workspace."
}

$requiredLocalDeps = @(
    'EnvContracts',
    'EnvNodeSupport',
    'EnvPredictorIO',
    'x64\Release\EnvPredictorIO.lib',
    'x64\Release\FlightEnvNodeSdk.lib',
    'x64\Release\FlightEnvNodeSdk.dll'
)
foreach ($relativePath in $requiredLocalDeps) {
    $candidate = Join-Path $workspaceRoot $relativePath
    if (-not (Test-Path -LiteralPath $candidate)) {
        throw "Local dependency workspace is incomplete: $candidate. Run tools\bootstrap_local_deps.ps1 first."
    }
}

if ($IncludeNodeIntegration) {
    foreach ($exeName in @('EnvNodeSenser.exe', 'EnvNodeFilter.exe')) {
        $candidate = Join-Path $workspaceRoot "x64\Release\$exeName"
        if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            throw "Node integration executable missing: $candidate. Re-run tools\bootstrap_local_deps.ps1 with a runtime-private artifact that contains node binaries."
        }
    }
}

$project = Join-Path $repoRoot "tests\EnvNodeControllerUITests\EnvNodeControllerUITests.vcxproj"

$sdkLib = Join-Path -Path $workspaceRoot -ChildPath ("{0}\{1}\FlightEnvNodeSdk.lib" -f $Platform, $Configuration)
$sdkDll = Join-Path -Path $workspaceRoot -ChildPath ("{0}\{1}\FlightEnvNodeSdk.dll" -f $Platform, $Configuration)
if ((-not $SkipNodeSdkBuild) -and (-not (Test-Path -LiteralPath $sdkLib -PathType Leaf) -or -not (Test-Path -LiteralPath $sdkDll -PathType Leaf))) {
    if ([string]::IsNullOrWhiteSpace($NodeSdkRoot)) {
        $NodeSdkRoot = [System.IO.Path]::GetFullPath((Join-Path -Path $repoRoot -ChildPath "..\flightenv-node-sdk"))
    }
    $nodeSdkProject = Join-Path -Path $NodeSdkRoot -ChildPath "FlightEnvNodeSdk\FlightEnvNodeSdk.vcxproj"
    if (Test-Path -LiteralPath $nodeSdkProject -PathType Leaf) {
        $nodeSdkBuildArgs = @(
            $nodeSdkProject,
            "/m",
            "/p:Configuration=$Configuration",
            "/p:Platform=$Platform",
            "/p:FlightEnvDepsWorkspaceRoot=$workspaceRoot",
            "/v:minimal"
        )
        & $msbuild @nodeSdkBuildArgs
        if ($LASTEXITCODE -ne 0) {
            throw "Build failed: FlightEnvNodeSdk"
        }
    }
}
if (-not (Test-Path -LiteralPath $sdkLib -PathType Leaf)) {
    throw "SDK library missing: $sdkLib. Build flightenv-node-sdk first or rerun without -SkipNodeSdkBuild."
}
if (-not (Test-Path -LiteralPath $sdkDll -PathType Leaf)) {
    throw "SDK DLL missing: $sdkDll. Bootstrap/download flightenv-node-sdk-x64-release.zip first."
}

& $msbuild $project /t:Build "/p:Configuration=$Configuration;Platform=$Platform;QtInstall=$resolvedQtRoot;FlightEnvDepsWorkspaceRoot=$workspaceRoot"
if ($LASTEXITCODE -ne 0) {
    throw "Build failed: tests\EnvNodeControllerUITests\EnvNodeControllerUITests.vcxproj"
}

$exePlatformDir = if ($Platform -eq "Win32") { "Win32" } else { $Platform }
$exe = Join-Path $workspaceRoot "tests\$exePlatformDir\$Configuration\EnvNodeControllerUITests.exe"
if (-not (Test-Path $exe)) {
    throw "Test executable not found: $exe"
}

$resolvedRuntimeBinRoot = Get-FlightEnvRuntimeBinRoot `
    -RepoRoot $repoRoot `
    -Configuration $Configuration `
    -Platform $Platform `
    -PreferredRoot $RuntimeBinRoot
$runtimeDependencyRoots = Get-FlightEnvRuntimeDependencyRoots `
    -RepoRoot $repoRoot `
    -Configuration $Configuration `
    -Platform $Platform

$runtimePaths = @(
    (Join-Path $resolvedQtRoot "bin"),
    $resolvedRuntimeBinRoot
) + $runtimeDependencyRoots + @(
    (Join-Path $sharedDepsRoot "lib\ros2"),
    (Join-Path $sharedDepsRoot "lib\x64"),
    (Join-Path $sharedDepsRoot "lib"),
    (Join-Path $sharedDepsRoot "lib\sensor_msgs_plus"),
    (Join-Path $sharedDepsRoot "bin\VTK")
)
$currentPath = [System.Environment]::GetEnvironmentVariable("Path", "Process")
[System.Environment]::SetEnvironmentVariable("Path", ($runtimePaths -join ";") + ";" + $currentPath, "Process")

function Convert-ToUnixPath([string]$Path) {
    return ($Path -replace "\\", "/")
}

$cycloneConfig = Join-Path $repoRoot "tools\launch\cyclonedds_localhost.xml"
if (Test-Path $cycloneConfig) {
    $env:RMW_IMPLEMENTATION = "rmw_cyclonedds_cpp"
    $env:ROS_LOCALHOST_ONLY = "0"
    $env:CYCLONEDDS_URI = "file://$(Convert-ToUnixPath ((Resolve-Path $cycloneConfig).Path))"
}

if ($IncludeNodeIntegration) {
    $env:FLIGHTENV_RUN_NODE_UI_INTEGRATION = "1"
    if (-not $env:FLIGHTENV_FILTER_INIT_TIMEOUT_SECONDS) {
        $env:FLIGHTENV_FILTER_INIT_TIMEOUT_SECONDS = "240"
    }
    if (-not $env:FLIGHTENV_SENSOR_INIT_TIMEOUT_SECONDS) {
        $env:FLIGHTENV_SENSOR_INIT_TIMEOUT_SECONDS = "120"
    }
    Get-Process -Name EnvNodeFilter, EnvNodeSenser -ErrorAction SilentlyContinue |
        Stop-Process -Force -ErrorAction SilentlyContinue
} else {
    Remove-Item Env:\FLIGHTENV_RUN_NODE_UI_INTEGRATION -ErrorAction SilentlyContinue
}

try {
    if ($TestFunction) {
        & $exe $TestFunction
    } else {
        & $exe
    }
    if ($LASTEXITCODE -ne 0) {
        throw "Qt UI test failed."
    }
} finally {
    if ($IncludeNodeIntegration) {
        Get-Process -Name EnvNodeFilter, EnvNodeSenser -ErrorAction SilentlyContinue |
            Stop-Process -Force -ErrorAction SilentlyContinue
    }
}
