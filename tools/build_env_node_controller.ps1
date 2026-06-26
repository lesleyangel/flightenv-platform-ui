param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",

    [ValidateSet("x64", "Win32")]
    [string]$Platform = "x64",

    [string]$QtInstall = "",

    [string]$WorkspaceRoot = "",

    [string]$SharedDepsRoot = "",

    [string]$NodeSdkRoot = "",

    [string]$RuntimePrivateRoot = "",

    [switch]$SkipNodeSdkBuild,

    [switch]$UsePlatformBackend,

    [switch]$UseLegacyBackend,

    [switch]$VerboseMSBuild
)

$ErrorActionPreference = "Stop"

if ($UseLegacyBackend) {
    $UsePlatformBackend = $false
}
else {
    $UsePlatformBackend = $true
}

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$projectPath = Join-Path $repoRoot "EnvPlatformController\EnvPlatformController.vcxproj"
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

function Get-QtRegistryInstallDir {
    param(
        [Parameter(Mandatory = $true)]
        [string]$QtKey
    )

    $registryCandidates = @(
        "HKCU:\Software\QtProject\QtVsTools\Versions\$QtKey",
        "HKLM:\Software\QtProject\QtVsTools\Versions\$QtKey",
        "HKCU:\Software\WOW6432Node\QtProject\QtVsTools\Versions\$QtKey",
        "HKLM:\Software\WOW6432Node\QtProject\QtVsTools\Versions\$QtKey"
    )

    foreach ($registryPath in $registryCandidates) {
        if (-not (Test-Path $registryPath)) {
            continue
        }
        $installDir = (Get-ItemProperty -Path $registryPath -ErrorAction SilentlyContinue).InstallDir
        if ($installDir -and (Test-Path $installDir)) {
            return (Resolve-Path $installDir).Path
        }
    }

    return $null
}

function Resolve-QtInstallDir {
    param(
        [string]$QtInstallHint
    )

    if ($QtInstallHint) {
        if (Test-Path $QtInstallHint) {
            return (Resolve-Path $QtInstallHint).Path
        }

        $fromRegistry = Get-QtRegistryInstallDir -QtKey $QtInstallHint
        if ($fromRegistry) {
            return $fromRegistry
        }
    }

    [xml]$projectXml = Get-Content -LiteralPath $projectPath
    $ns = New-Object System.Xml.XmlNamespaceManager($projectXml.NameTable)
    $ns.AddNamespace("msb", "http://schemas.microsoft.com/developer/msbuild/2003")

    $qtCondition = "'`$(Configuration)|`$(Platform)'=='$Configuration|$Platform'"
    $qtInstallXPath = "//msb:PropertyGroup[@Label='QtSettings' and @Condition=`"$qtCondition`"]/msb:QtInstall"
    $qtInstallNode = $projectXml.SelectSingleNode($qtInstallXPath, $ns)

    if ($qtInstallNode -and $qtInstallNode.InnerText) {
        try {
            $fromProject = Resolve-QtInstallDir -QtInstallHint $qtInstallNode.InnerText
            if ($fromProject) {
                return $fromProject
            }
        }
        catch {
            Write-Host "Qt alias '$($qtInstallNode.InnerText)' did not resolve via registry; falling back to qtvars/common paths."
        }
    }

    $qtvarsPath = Join-Path $repoRoot "EnvPlatformController\$Platform\$Configuration\qt\qtvars.xml"
    if (Test-Path $qtvarsPath) {
        [xml]$qtvarsXml = Get-Content -LiteralPath $qtvarsPath
        $qtPrefix = $qtvarsXml.Project.PropertyGroup.QMake_QT_INSTALL_PREFIX_
        if ($qtPrefix -and (Test-Path $qtPrefix)) {
            return (Resolve-Path $qtPrefix).Path
        }
    }

    $commonCandidates = @(
        "C:\Qt6\6.2.0\msvc2019_64",
        "C:\Qt\6.2.0\msvc2019_64",
        "C:\Qt\6.2.0\msvc2022_64",
        "C:\Qt6\6.2.0\msvc2022_64"
    )
    foreach ($candidate in $commonCandidates) {
        if (Test-Path (Join-Path $candidate "bin\\qmake.exe")) {
            return (Resolve-Path $candidate).Path
        }
    }

    throw "Unable to resolve a Qt installation. Pass -QtInstall <alias-or-path> or configure Qt VS Tools for $Configuration|$Platform."
}

function Assert-QtTools {
    param(
        [Parameter(Mandatory = $true)]
        [string]$QtInstallDir
    )

    $binDir = Join-Path $QtInstallDir "bin"
    foreach ($tool in @("rcc.exe", "uic.exe", "moc.exe", "qmake.exe")) {
        $toolPath = Join-Path $binDir $tool
        if (-not (Test-Path $toolPath)) {
            throw "Required Qt tool not found: $toolPath"
        }
    }

    return $binDir
}

$msbuild = Resolve-MSBuild
Normalize-ProcessPathEnvironment
$qtInstallDir = Resolve-QtInstallDir -QtInstallHint $QtInstall
$qtBinDir = Assert-QtTools -QtInstallDir $qtInstallDir
$workspaceRoot = Get-FlightEnvSharedWorkspaceRoot `
    -RepoRoot $repoRoot `
    -SharedDepsRoot $SharedDepsRoot `
    -PreferredWorkspaceRoot $WorkspaceRoot
Ensure-FlightEnvDirectory -PathValue $workspaceRoot
$env:FLIGHTENV_DEPS_WORKSPACE_ROOT = $workspaceRoot

$env:QTDIR = $qtInstallDir
$currentPath = [System.Environment]::GetEnvironmentVariable("Path", "Process")
[System.Environment]::SetEnvironmentVariable("Path", "$qtBinDir;$currentPath", "Process")

Write-Host "Using MSBuild: $msbuild"
Write-Host "Using Qt install: $qtInstallDir"
Write-Host "Using Qt bin: $qtBinDir"
Write-Host "Using dependency workspace: $workspaceRoot"
if ($UsePlatformBackend) {
    Write-Host "Using controller backend: platform SDK"
}
else {
    Write-Host "Using controller backend: legacy ROS SDK"
}
if (-not [string]::IsNullOrWhiteSpace($RuntimePrivateRoot)) {
    Write-Host "Ignoring -RuntimePrivateRoot for UI build; UI consumes only binary runtime artifacts from the dependency workspace."
}

$sdkLib = Join-Path -Path $workspaceRoot -ChildPath ("{0}\{1}\FlightEnvNodeSdk.lib" -f $Platform, $Configuration)
$sdkDll = Join-Path -Path $workspaceRoot -ChildPath ("{0}\{1}\FlightEnvNodeSdk.dll" -f $Platform, $Configuration)
if ((-not $SkipNodeSdkBuild) -and (-not (Test-Path -LiteralPath $sdkLib -PathType Leaf) -or -not (Test-Path -LiteralPath $sdkDll -PathType Leaf))) {
    if ([string]::IsNullOrWhiteSpace($NodeSdkRoot)) {
        $NodeSdkRoot = [System.IO.Path]::GetFullPath((Join-Path -Path $repoRoot -ChildPath "..\flightenv-node-sdk"))
    }
    if (Test-Path -LiteralPath $NodeSdkRoot -PathType Container) {
        $nodeSdkProject = Join-Path -Path $NodeSdkRoot -ChildPath "FlightEnvNodeSdk\FlightEnvNodeSdk.vcxproj"
        if (-not (Test-Path -LiteralPath $nodeSdkProject -PathType Leaf)) {
            throw "Node SDK project not found: $nodeSdkProject"
        }
        Write-Host "Building FlightEnvNodeSdk.dll/lib from: $nodeSdkProject"
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
    elseif (-not (Test-Path -LiteralPath $sdkLib -PathType Leaf)) {
        throw "Node SDK root not found and SDK library is missing: $NodeSdkRoot"
    }
}
if (-not (Test-Path -LiteralPath $sdkLib -PathType Leaf)) {
    throw "SDK library missing: $sdkLib. Build flightenv-node-sdk first or run this script without -SkipNodeSdkBuild."
}
if (-not (Test-Path -LiteralPath $sdkDll -PathType Leaf)) {
    throw "SDK DLL missing: $sdkDll. Bootstrap/download flightenv-node-sdk-x64-release.zip first."
}

$msbuildArgs = @(
    $projectPath,
    "/m",
    "/p:Configuration=$Configuration",
    "/p:Platform=$Platform",
    "/p:QtInstall=$qtInstallDir",
    "/p:FlightEnvDepsWorkspaceRoot=$workspaceRoot"
)
if ($UsePlatformBackend) {
    $msbuildArgs += "/p:FlightEnvUsePlatformBackend=true"
}

if ($VerboseMSBuild) {
    $msbuildArgs += "/v:normal"
}
else {
    $msbuildArgs += "/v:minimal"
}

& $msbuild @msbuildArgs
if ($LASTEXITCODE -ne 0) {
    throw "EnvNodeController build failed."
}
