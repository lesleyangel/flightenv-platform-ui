param(
    [ValidateSet('Release', 'Debug')]
    [string]$Configuration = 'Release',

    [ValidateSet('x64', 'Win32')]
    [string]$Platform = 'x64',

    [string]$ContractsRoot,

    [string]$ContractsVersion = '',

    [string]$ContractsAssetName = '',

    [string]$NodeSdkRoot,

    [string]$RuntimePrivateRoot,

    [string]$ArtifactRepo = 'lesleyangel/flightenv-artifacts',

    [string]$RuntimeVersion = '',

    [string]$RuntimeAssetName = '',

    [string]$NodeSdkVersion = '',

    [string]$NodeSdkAssetName = '',

    [string]$ArtifactCacheRoot = '',

    [string]$GitHubToken = '',

    [switch]$SkipChecksum,

    [Parameter(Mandatory = $true)]
    [string]$ExternalDepsRoot,

    [string]$WorkspaceRoot = '',

    [string]$SharedDepsRoot = '',

    [switch]$Clean
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'FlightEnvDepsWorkspace.ps1')

function Resolve-Directory {
    param(
        [Parameter(Mandatory = $true)][string]$PathValue,
        [Parameter(Mandatory = $true)][string]$Label
    )

    if (-not (Test-Path -LiteralPath $PathValue -PathType Container)) {
        throw "$Label directory not found: $PathValue"
    }
    return (Resolve-Path -LiteralPath $PathValue).Path
}

function Ensure-Directory {
    param([Parameter(Mandatory = $true)][string]$PathValue)

    if (-not (Test-Path -LiteralPath $PathValue -PathType Container)) {
        New-Item -ItemType Directory -Path $PathValue -Force | Out-Null
    }
}

function Copy-TreeContents {
    param(
        [Parameter(Mandatory = $true)][string]$SourceRoot,
        [Parameter(Mandatory = $true)][string]$DestinationRoot
    )

    Ensure-Directory -PathValue $DestinationRoot
    Copy-Item -Path (Join-Path $SourceRoot '*') -Destination $DestinationRoot -Recurse -Force
}

function Resolve-ReleaseDownloadScript {
    $candidates = @(
        (Join-Path $repoRoot 'tools\download_github_release_asset.ps1'),
        (Join-Path $repoRoot '..\tools\download_github_release_asset.ps1')
    )
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return [System.IO.Path]::GetFullPath($candidate)
        }
    }
    throw 'GitHub release download script not found. Expected tools\download_github_release_asset.ps1 in this repo or workspace root.'
}

function Expand-ReleasePackage {
    param(
        [Parameter(Mandatory = $true)][string]$Repository,
        [Parameter(Mandatory = $true)][string]$Version,
        [Parameter(Mandatory = $true)][string]$AssetName,
        [Parameter(Mandatory = $true)][string]$ExpectedLeaf
    )

    $cacheRoot = $ArtifactCacheRoot
    if ([string]::IsNullOrWhiteSpace($cacheRoot)) {
        $cacheRoot = [System.IO.Path]::GetFullPath((Join-Path $repoRoot '..\_local_artifacts\packages'))
    }
    else {
        $cacheRoot = [System.IO.Path]::GetFullPath($cacheRoot)
    }

    $downloadDir = Join-Path -Path $cacheRoot -ChildPath $Version
    Ensure-Directory -PathValue $downloadDir
    $zipPath = Join-Path -Path $downloadDir -ChildPath $AssetName
    $extractRoot = Join-Path -Path $downloadDir -ChildPath ([System.IO.Path]::GetFileNameWithoutExtension($AssetName))

    $downloadScript = Resolve-ReleaseDownloadScript
    $downloadArgs = @(
        '-NoProfile',
        '-ExecutionPolicy', 'Bypass',
        '-File', $downloadScript,
        '-Repository', $Repository,
        '-Tag', $Version,
        '-AssetName', $AssetName,
        '-OutputPath', $zipPath
    )
    if (-not [string]::IsNullOrWhiteSpace($GitHubToken)) {
        $downloadArgs += @('-Token', $GitHubToken)
    }
    if ($SkipChecksum) {
        $downloadArgs += '-SkipChecksum'
    }
    $downloadOutput = & powershell @downloadArgs
    foreach ($line in $downloadOutput) {
        Write-Host $line
    }
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to download release asset: $Repository $Version $AssetName"
    }

    if (Test-Path -LiteralPath $extractRoot -PathType Container) {
        Remove-Item -LiteralPath $extractRoot -Recurse -Force
    }
    Ensure-Directory -PathValue $extractRoot
    Expand-Archive -LiteralPath $zipPath -DestinationPath $extractRoot -Force

    $packageRoot = Join-Path -Path $extractRoot -ChildPath $ExpectedLeaf
    if (-not (Test-Path -LiteralPath $packageRoot -PathType Container)) {
        throw "Downloaded package does not contain expected root '$ExpectedLeaf': $zipPath"
    }
    return $packageRoot
}

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$externalRoot = Resolve-Directory -PathValue $ExternalDepsRoot -Label 'External dependency root'

if ([string]::IsNullOrWhiteSpace($ContractsRoot) -and -not [string]::IsNullOrWhiteSpace($ContractsVersion)) {
    if ([string]::IsNullOrWhiteSpace($ArtifactRepo)) {
        throw '-ArtifactRepo is required when -ContractsVersion is used.'
    }
    if ([string]::IsNullOrWhiteSpace($ContractsAssetName)) {
        $ContractsAssetName = "flightenv-contracts-$ContractsVersion.zip"
    }
    $ContractsRoot = Expand-ReleasePackage `
        -Repository $ArtifactRepo `
        -Version $ContractsVersion `
        -AssetName $ContractsAssetName `
        -ExpectedLeaf 'contracts'
}
if ([string]::IsNullOrWhiteSpace($ContractsRoot)) {
    throw 'Contracts root is required. Pass -ContractsRoot or use -ContractsVersion with -ArtifactRepo.'
}

$contractsRoot = Resolve-Directory -PathValue $ContractsRoot -Label 'Contracts root'

if ([string]::IsNullOrWhiteSpace($RuntimePrivateRoot) -and -not [string]::IsNullOrWhiteSpace($RuntimeVersion)) {
    if ([string]::IsNullOrWhiteSpace($ArtifactRepo)) {
        throw '-ArtifactRepo is required when -RuntimeVersion is used.'
    }
    if ([string]::IsNullOrWhiteSpace($RuntimeAssetName)) {
        $RuntimeAssetName = "flightenv-runtime-private-runtime-$RuntimeVersion-win-$Platform-$Configuration.zip"
    }
    $RuntimePrivateRoot = Expand-ReleasePackage `
        -Repository $ArtifactRepo `
        -Version $RuntimeVersion `
        -AssetName $RuntimeAssetName `
        -ExpectedLeaf 'runtime-private'
}

if ([string]::IsNullOrWhiteSpace($NodeSdkRoot) -and -not [string]::IsNullOrWhiteSpace($NodeSdkVersion)) {
    if ([string]::IsNullOrWhiteSpace($ArtifactRepo)) {
        throw '-ArtifactRepo is required when -NodeSdkVersion is used.'
    }
    if ([string]::IsNullOrWhiteSpace($NodeSdkAssetName)) {
        $NodeSdkAssetName = "flightenv-node-sdk-$NodeSdkVersion-win-$Platform-$Configuration.zip"
    }
    $NodeSdkRoot = Expand-ReleasePackage `
        -Repository $ArtifactRepo `
        -Version $NodeSdkVersion `
        -AssetName $NodeSdkAssetName `
        -ExpectedLeaf 'node-sdk'
}

if ([string]::IsNullOrWhiteSpace($RuntimePrivateRoot)) {
    throw 'Runtime-private artifact root is required. Pass -RuntimePrivateRoot or use -RuntimeVersion with -ArtifactRepo.'
}
if ([string]::IsNullOrWhiteSpace($NodeSdkRoot)) {
    throw 'Node SDK root is required. Pass -NodeSdkRoot or use -NodeSdkVersion with -ArtifactRepo.'
}

$nodeSdkRoot = Resolve-Directory -PathValue $NodeSdkRoot -Label 'Node SDK root'
$runtimeRoot = Resolve-Directory -PathValue $RuntimePrivateRoot -Label 'Runtime-private artifact root'

$contractsIncludeRoot = Resolve-Directory -PathValue (Join-Path $contractsRoot 'include\EnvContracts') -Label 'Contracts include root'
$nodeSdkIncludeRoot = Resolve-Directory -PathValue (Join-Path $nodeSdkRoot 'include') -Label 'Node SDK include root'
$runtimeLibRoot = Resolve-Directory -PathValue (Join-Path $runtimeRoot 'lib') -Label 'Runtime lib root'
$runtimeProjectBinRoot = Resolve-Directory -PathValue (Join-Path $runtimeRoot 'bin\project') -Label 'Runtime project bin root'

$workspaceRoot = Get-FlightEnvSharedWorkspaceRoot `
    -RepoRoot $repoRoot `
    -SharedDepsRoot $SharedDepsRoot `
    -PreferredWorkspaceRoot $WorkspaceRoot
if ($Clean -and (Test-Path -LiteralPath $workspaceRoot)) {
    Remove-Item -LiteralPath $workspaceRoot -Recurse -Force
}

Ensure-FlightEnvDirectory -PathValue $workspaceRoot
Add-FlightEnvExternalDependencyLinks -ExternalRoot $externalRoot -WorkspaceRoot $workspaceRoot

Copy-FlightEnvTreeContents -SourceRoot $contractsIncludeRoot -DestinationRoot (Join-Path $workspaceRoot 'EnvContracts')
Copy-FlightEnvTreeContents -SourceRoot (Join-Path $nodeSdkIncludeRoot 'EnvNodeSupport') -DestinationRoot (Join-Path $workspaceRoot 'EnvNodeSupport')
Copy-FlightEnvTreeContents -SourceRoot (Join-Path $nodeSdkIncludeRoot 'EnvNodeTools') -DestinationRoot (Join-Path $workspaceRoot 'EnvNodeTools')
Copy-FlightEnvTreeContents -SourceRoot (Join-Path $nodeSdkIncludeRoot 'EnvPredictorIO') -DestinationRoot (Join-Path $workspaceRoot 'EnvPredictorIO')

$legacyBuildRoot = Join-Path $workspaceRoot 'x64\Release'
Ensure-FlightEnvDirectory -PathValue $legacyBuildRoot
Copy-FlightEnvTreeContents -SourceRoot $runtimeLibRoot -DestinationRoot $legacyBuildRoot
Copy-FlightEnvTreeContents -SourceRoot $runtimeProjectBinRoot -DestinationRoot $legacyBuildRoot

$nodeSdkLibRoot = Join-Path $nodeSdkRoot 'lib'
if (Test-Path -LiteralPath $nodeSdkLibRoot -PathType Container) {
    Copy-FlightEnvTreeContents -SourceRoot $nodeSdkLibRoot -DestinationRoot $legacyBuildRoot
}
$nodeSdkBinRoot = Join-Path $nodeSdkRoot 'bin'
if (Test-Path -LiteralPath $nodeSdkBinRoot -PathType Container) {
    Copy-FlightEnvTreeContents -SourceRoot $nodeSdkBinRoot -DestinationRoot $legacyBuildRoot
}
$nodeSdkBuildRoot = Join-Path $nodeSdkRoot 'x64\Release'
if (Test-Path -LiteralPath $nodeSdkBuildRoot -PathType Container) {
    Copy-FlightEnvTreeContents -SourceRoot $nodeSdkBuildRoot -DestinationRoot $legacyBuildRoot
}

$sdkLib = Join-Path $legacyBuildRoot 'FlightEnvNodeSdk.lib'
$sdkDll = Join-Path $legacyBuildRoot 'FlightEnvNodeSdk.dll'
if (-not (Test-Path -LiteralPath $sdkLib -PathType Leaf)) {
    throw "Node SDK import library not found after bootstrap: $sdkLib"
}
if (-not (Test-Path -LiteralPath $sdkDll -PathType Leaf)) {
    throw "Node SDK DLL not found after bootstrap: $sdkDll"
}

Write-Host 'Prepared controller-ui shared dependency workspace:'
Write-Host "  workspace = $workspaceRoot"
Write-Host "  runtime   = $runtimeRoot"
Write-Host "  external  = $externalRoot"
