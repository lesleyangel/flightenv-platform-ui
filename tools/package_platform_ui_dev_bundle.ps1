param(
    [ValidateSet('Release', 'Debug')]
    [string]$Configuration = 'Release',

    [ValidateSet('x64', 'Win32')]
    [string]$Platform = 'x64',

    [string]$WorkspaceRoot = '',

    [string]$OutputRoot = '',

    [string]$PackageName = '',

    [string]$QtInstall = '',

    [switch]$Clean,

    [switch]$Overwrite,

    [switch]$SkipZip,

    [switch]$SkipHeavyData,

    [switch]$IncludeDebugSymbols,

    [switch]$IncludeQtSdk,

    [switch]$IncludeLatestRun
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'FlightEnvDepsWorkspace.ps1')

function Ensure-Directory {
    param([Parameter(Mandatory = $true)][string]$PathValue)
    if (-not (Test-Path -LiteralPath $PathValue -PathType Container)) {
        New-Item -ItemType Directory -Path $PathValue -Force | Out-Null
    }
}

function Assert-PathUnderRoot {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$PathValue
    )
    $rootFull = [System.IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    $pathFull = [System.IO.Path]::GetFullPath($PathValue).TrimEnd('\', '/')
    if ($pathFull -eq $rootFull) {
        return
    }
    $prefix = $rootFull + [System.IO.Path]::DirectorySeparatorChar
    if (-not $pathFull.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to operate outside output root. Root='$rootFull' Path='$pathFull'"
    }
}

function Remove-DirectorySafely {
    param(
        [Parameter(Mandatory = $true)][string]$AllowedRoot,
        [Parameter(Mandatory = $true)][string]$PathValue
    )
    if (Test-Path -LiteralPath $PathValue -PathType Container) {
        Assert-PathUnderRoot -Root $AllowedRoot -PathValue $PathValue
        Remove-Item -LiteralPath $PathValue -Recurse -Force
    }
}

function Get-RelativePath {
    param(
        [Parameter(Mandatory = $true)][string]$SourceRoot,
        [Parameter(Mandatory = $true)][string]$PathValue
    )
    $sourceFull = [System.IO.Path]::GetFullPath($SourceRoot).TrimEnd('\', '/')
    $targetFull = [System.IO.Path]::GetFullPath($PathValue)
    $prefix = $sourceFull + [System.IO.Path]::DirectorySeparatorChar
    if (-not $targetFull.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Path is not under source root: $PathValue"
    }
    return $targetFull.Substring($prefix.Length)
}

function Test-HasExcludedDirectory {
    param(
        [Parameter(Mandatory = $true)][string]$RelativePath,
        [Parameter(Mandatory = $true)][string[]]$ExcludedDirectoryNames
    )
    $parts = $RelativePath -split '[\\/]'
    foreach ($part in $parts) {
        if ($ExcludedDirectoryNames -contains $part) {
            return $true
        }
    }
    return $false
}

function Copy-TreeFiltered {
    param(
        [Parameter(Mandatory = $true)][string]$SourceRoot,
        [Parameter(Mandatory = $true)][string]$DestinationRoot,
        [string[]]$ExcludedDirectoryNames = @(),
        [string[]]$ExcludedFileNames = @(),
        [string[]]$ExcludedExtensions = @(),
        [switch]$SkipLargeDatabaseFiles,
        [int64]$LargeFileThresholdBytes = 268435456
    )

    if (-not (Test-Path -LiteralPath $SourceRoot -PathType Container)) {
        return [ordered]@{
            source = $SourceRoot
            destination = $DestinationRoot
            files = 0
            bytes = 0
            skipped = @("missing")
        }
    }

    $sourceFull = [System.IO.Path]::GetFullPath($SourceRoot)
    Ensure-Directory -PathValue $DestinationRoot

    $filesCopied = 0
    [int64]$bytesCopied = 0
    $skipped = New-Object System.Collections.Generic.List[string]
    $excludedExt = [System.Collections.Generic.HashSet[string]]::new(
        [string[]]$ExcludedExtensions,
        [System.StringComparer]::OrdinalIgnoreCase)
    $excludedNames = [System.Collections.Generic.HashSet[string]]::new(
        [string[]]$ExcludedFileNames,
        [System.StringComparer]::OrdinalIgnoreCase)

    $items = Get-ChildItem -LiteralPath $sourceFull -Recurse -Force -File
    foreach ($item in $items) {
        $relative = Get-RelativePath -SourceRoot $sourceFull -PathValue $item.FullName
        if (Test-HasExcludedDirectory -RelativePath $relative -ExcludedDirectoryNames $ExcludedDirectoryNames) {
            continue
        }
        if ($excludedNames.Contains($item.Name)) {
            continue
        }
        if ($excludedExt.Contains($item.Extension)) {
            continue
        }
        if ($item.Name -like 'cdds.log.*') {
            continue
        }
        if ($SkipLargeDatabaseFiles -and
            $item.Extension.Equals('.db', [System.StringComparison]::OrdinalIgnoreCase) -and
            $item.Length -gt $LargeFileThresholdBytes) {
            $skipped.Add("heavy:$relative")
            continue
        }

        $targetPath = Join-Path -Path $DestinationRoot -ChildPath $relative
        Ensure-Directory -PathValue ([System.IO.Path]::GetDirectoryName($targetPath))
        Copy-Item -LiteralPath $item.FullName -Destination $targetPath -Force
        $filesCopied++
        $bytesCopied += $item.Length
    }

    return [ordered]@{
        source = $SourceRoot
        destination = $DestinationRoot
        files = $filesCopied
        bytes = $bytesCopied
        skipped = @($skipped)
    }
}

function Copy-BuildArtifacts {
    param(
        [Parameter(Mandatory = $true)][string]$SourceRoot,
        [Parameter(Mandatory = $true)][string]$DestinationRoot,
        [switch]$WithSymbols
    )
    if (-not (Test-Path -LiteralPath $SourceRoot -PathType Container)) {
        throw "Build output root not found: $SourceRoot"
    }
    Ensure-Directory -PathValue $DestinationRoot

    $allowedExtensions = @('.exe', '.dll', '.lib', '.json', '.xml', '.yaml', '.yml', '.txt', '.qss', '.qm')
    if ($WithSymbols) {
        $allowedExtensions += @('.pdb')
    }
    $allowed = [System.Collections.Generic.HashSet[string]]::new(
        [string[]]$allowedExtensions,
        [System.StringComparer]::OrdinalIgnoreCase)

    $filesCopied = 0
    [int64]$bytesCopied = 0
    foreach ($item in (Get-ChildItem -LiteralPath $SourceRoot -Recurse -Force -File)) {
        if (-not $allowed.Contains($item.Extension)) {
            continue
        }
        $relative = Get-RelativePath -SourceRoot $SourceRoot -PathValue $item.FullName
        $targetPath = Join-Path -Path $DestinationRoot -ChildPath $relative
        Ensure-Directory -PathValue ([System.IO.Path]::GetDirectoryName($targetPath))
        Copy-Item -LiteralPath $item.FullName -Destination $targetPath -Force
        $filesCopied++
        $bytesCopied += $item.Length
    }

    return [ordered]@{
        source = $SourceRoot
        destination = $DestinationRoot
        files = $filesCopied
        bytes = $bytesCopied
    }
}

function Copy-FileIfExists {
    param(
        [Parameter(Mandatory = $true)][string]$SourcePath,
        [Parameter(Mandatory = $true)][string]$DestinationPath
    )
    if (Test-Path -LiteralPath $SourcePath -PathType Leaf) {
        Ensure-Directory -PathValue ([System.IO.Path]::GetDirectoryName($DestinationPath))
        Copy-Item -LiteralPath $SourcePath -Destination $DestinationPath -Force
        return $true
    }
    return $false
}

function Compress-DirectoryToZip {
    param(
        [Parameter(Mandatory = $true)][string]$SourceDirectory,
        [Parameter(Mandatory = $true)][string]$DestinationZip
    )
    if ((Test-Path -LiteralPath $DestinationZip -PathType Leaf) -and -not $Overwrite) {
        throw "Zip file already exists: $DestinationZip. Use -Overwrite to replace it."
    }
    if (Test-Path -LiteralPath $DestinationZip -PathType Leaf) {
        Remove-Item -LiteralPath $DestinationZip -Force
    }

    $tar = Get-Command tar.exe -ErrorAction SilentlyContinue
    if ($tar) {
        $sourceFullPath = [System.IO.Path]::GetFullPath($SourceDirectory)
        $parent = Split-Path -Path $sourceFullPath -Parent
        $leaf = Split-Path -Path $sourceFullPath -Leaf
        & $tar.Source -a -cf $DestinationZip -C $parent $leaf
        if ($LASTEXITCODE -ne 0) {
            throw "tar.exe failed to create platform UI dev bundle zip: $DestinationZip"
        }
        return
    }

    Compress-Archive -LiteralPath $SourceDirectory -DestinationPath $DestinationZip -CompressionLevel Optimal
}

function New-Sha256File {
    param([Parameter(Mandatory = $true)][string]$PathValue)
    $hash = (Get-FileHash -LiteralPath $PathValue -Algorithm SHA256).Hash.ToLowerInvariant()
    $name = [System.IO.Path]::GetFileName($PathValue)
    $shaPath = "$PathValue.sha256"
    Set-Content -LiteralPath $shaPath -Encoding ASCII -Value "$hash  $name"
    return $shaPath
}

function Get-GitCommitOrEmpty {
    param([Parameter(Mandatory = $true)][string]$RepoPath)
    if (-not (Test-Path -LiteralPath (Join-Path $RepoPath '.git'))) {
        return ''
    }
    try {
        return (& git -C $RepoPath rev-parse HEAD 2>$null)
    }
    catch {
        return ''
    }
}

function Copy-LatestMainlineRun {
    param(
        [Parameter(Mandatory = $true)][string]$WorkspaceHome,
        [Parameter(Mandatory = $true)][string]$PackageRoot
    )
    $mainlineRoot = Join-Path $WorkspaceHome '_local_artifacts\platform-runtime\mainline-runs'
    if (-not (Test-Path -LiteralPath $mainlineRoot -PathType Container)) {
        return [ordered]@{ included = $false; reason = 'mainline run root not found' }
    }
    $latest = Get-ChildItem -LiteralPath $mainlineRoot -Directory |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
    if (-not $latest) {
        return [ordered]@{ included = $false; reason = 'no mainline run found' }
    }

    $relativeDest = Join-Path '_local_artifacts\platform-runtime\mainline-runs' $latest.Name
    $summary = Copy-TreeFiltered `
        -SourceRoot $latest.FullName `
        -DestinationRoot (Join-Path $PackageRoot $relativeDest) `
        -ExcludedDirectoryNames @('.git', '.vs', 'temp') `
        -ExcludedExtensions @('.pdb', '.ilk', '.exp')

    $hostRoot = Join-Path $WorkspaceHome '_local_artifacts\platform-runtime\runtime-host-runs'
    $hostSummaries = @()
    if (Test-Path -LiteralPath $hostRoot -PathType Container) {
        foreach ($workflowDir in (Get-ChildItem -LiteralPath $hostRoot -Directory)) {
            foreach ($runDir in (Get-ChildItem -LiteralPath $workflowDir.FullName -Directory | Where-Object { $_.Name -like ($latest.Name + '*') })) {
                $hostDest = Join-Path $PackageRoot (Join-Path '_local_artifacts\platform-runtime\runtime-host-runs' (Join-Path $workflowDir.Name $runDir.Name))
                $hostSummaries += Copy-TreeFiltered `
                    -SourceRoot $runDir.FullName `
                    -DestinationRoot $hostDest `
                    -ExcludedDirectoryNames @('.git', '.vs', 'temp') `
                    -ExcludedExtensions @('.pdb', '.ilk', '.exp')
            }
        }
    }

    return [ordered]@{
        included = $true
        run_id = $latest.Name
        mainline = $summary
        runtime_host_runs = $hostSummaries
    }
}

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$workspaceHome = [System.IO.Path]::GetFullPath((Join-Path $repoRoot '..'))
$depsRoot = Get-FlightEnvSharedDepsRoot -RepoRoot $repoRoot
$workspaceDepsRoot = Get-FlightEnvSharedWorkspaceRoot -RepoRoot $repoRoot -PreferredWorkspaceRoot $WorkspaceRoot
$buildRoot = Join-Path -Path $workspaceDepsRoot -ChildPath ("{0}\{1}" -f $Platform, $Configuration)

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = [System.IO.Path]::GetFullPath((Join-Path $workspaceHome '_local_artifacts\flightenv-platform-ui\platform-ui-dev-bundle'))
}
else {
    $OutputRoot = Resolve-FlightEnvPath -PathValue $OutputRoot -BasePath $workspaceHome
}

if ([string]::IsNullOrWhiteSpace($PackageName)) {
    $dateTag = Get-Date -Format 'yyyyMMdd_HHmmss'
    $PackageName = "flightenv-platform-ui-dev-$Platform-$Configuration-$dateTag"
}

$packageRoot = Join-Path $OutputRoot 'platform-ui-dev'
$zipPath = Join-Path $OutputRoot ($PackageName + '.zip')
Ensure-Directory -PathValue $OutputRoot
if ($Clean) {
    Remove-DirectorySafely -AllowedRoot $OutputRoot -PathValue $packageRoot
}
Ensure-Directory -PathValue $packageRoot

$commonExcludedDirs = @(
    '.git',
    '.vs',
    '.deps',
    '.pytest_cache',
    '__pycache__',
    'node_modules',
    'x64',
    'Win32',
    'Debug',
    'Release',
    'GeneratedFiles',
    'TestResults',
    'log'
)
$commonExcludedExtensions = @('.pdb', '.ilk', '.exp', '.suo', '.user', '.ncb', '.opensdf', '.zip')
if ($IncludeDebugSymbols) {
    $commonExcludedExtensions = @('.ilk', '.exp', '.suo', '.user', '.ncb', '.opensdf', '.zip')
}

$summaries = New-Object System.Collections.Generic.List[object]

# Source-like repos kept in workspace-compatible locations.
# UI source is editable. Object packages and PDK are included because UI
# developers need to inspect/edit object definitions and run PDK validation.
$repoNames = @(
    'flightenv-platform-ui',
    'flightenv-platform-pdk',
    'flightenv-object-reentry-vehicle',
    'flightenv-object-toy-thermal-plate'
)
foreach ($name in $repoNames) {
    $source = Join-Path $workspaceHome $name
    $dest = Join-Path $packageRoot $name
    $summaries.Add((Copy-TreeFiltered `
        -SourceRoot $source `
        -DestinationRoot $dest `
        -ExcludedDirectoryNames $commonExcludedDirs `
        -ExcludedExtensions $commonExcludedExtensions))
}

# Non-UI producer repos are represented by public headers/manifests only.
# Their implementation source stays out of this UI development bundle.
$publicTrees = @(
    @{ Source = 'flightenv-platform-runtime\include'; Destination = 'flightenv-platform-runtime\include' },
    @{ Source = 'flightenv-node-sdk\include'; Destination = 'flightenv-node-sdk\include' },
    @{ Source = 'flightenv-contracts\include'; Destination = 'flightenv-contracts\include' },
    @{ Source = 'flightenv-trajectory\include'; Destination = 'flightenv-trajectory\include' },
    @{ Source = 'flightenv-reentry-operators\include'; Destination = 'flightenv-reentry-operators\include' },
    @{ Source = 'flightenv-reentry-operators\tools'; Destination = 'flightenv-reentry-operators\tools' }
)
foreach ($tree in $publicTrees) {
    $summaries.Add((Copy-TreeFiltered `
        -SourceRoot (Join-Path $workspaceHome $tree.Source) `
        -DestinationRoot (Join-Path $packageRoot $tree.Destination) `
        -ExcludedDirectoryNames @('.git', '.vs', '__pycache__', 'obj', 'x64', 'Debug', 'Release', 'build', 'out') `
        -ExcludedExtensions @('.cpp', '.c', '.cc', '.cxx', '.pdb', '.ilk', '.exp', '.zip')))
}

# Root shared tooling needed by the VS projects and launch scripts.
$toolDest = Join-Path $packageRoot 'tools'
$summaries.Add((Copy-TreeFiltered `
    -SourceRoot (Join-Path $workspaceHome 'tools') `
    -DestinationRoot $toolDest `
    -ExcludedDirectoryNames @('.git', '.vs', 'log', 'temp') `
    -ExcludedExtensions $commonExcludedExtensions))

# Shared third-party and generated dependency workspace.
$summaries.Add((Copy-TreeFiltered `
    -SourceRoot (Join-Path $depsRoot 'include') `
    -DestinationRoot (Join-Path $packageRoot '_deps\include') `
    -ExcludedDirectoryNames @('.git', '.vs', '__pycache__') `
    -ExcludedExtensions @()))
$summaries.Add((Copy-TreeFiltered `
    -SourceRoot (Join-Path $depsRoot 'lib') `
    -DestinationRoot (Join-Path $packageRoot '_deps\lib') `
    -ExcludedDirectoryNames @('.git', '.vs') `
    -ExcludedExtensions @('.pdb', '.ilk', '.exp')))
$summaries.Add((Copy-TreeFiltered `
    -SourceRoot (Join-Path $depsRoot 'bin') `
    -DestinationRoot (Join-Path $packageRoot '_deps\bin') `
    -ExcludedDirectoryNames @('.git', '.vs', 'log') `
    -ExcludedExtensions @('.pdb', '.ilk', '.exp')))
$summaries.Add((Copy-TreeFiltered `
    -SourceRoot (Join-Path $depsRoot 'share') `
    -DestinationRoot (Join-Path $packageRoot '_deps\share') `
    -ExcludedDirectoryNames @('.git', '.vs', '__pycache__') `
    -ExcludedExtensions @('.pdb', '.ilk', '.exp')))
$summaries.Add((Copy-TreeFiltered `
    -SourceRoot (Join-Path $depsRoot 'data') `
    -DestinationRoot (Join-Path $packageRoot '_deps\data') `
    -ExcludedDirectoryNames @('.git', '.vs', 'log') `
    -ExcludedExtensions @('.pdb', '.ilk', '.exp')))
$summaries.Add((Copy-TreeFiltered `
    -SourceRoot (Join-Path $depsRoot 'example') `
    -DestinationRoot (Join-Path $packageRoot '_deps\example') `
    -ExcludedDirectoryNames @('.git', '.vs', 'log') `
    -ExcludedExtensions @('.pdb', '.ilk', '.exp', '.shm', '.wal') `
    -SkipLargeDatabaseFiles:$SkipHeavyData))

$workspaceArtifactDest = Join-Path $packageRoot ("_deps\workspace\{0}\{1}" -f $Platform, $Configuration)
$summaries.Add((Copy-BuildArtifacts `
    -SourceRoot $buildRoot `
    -DestinationRoot $workspaceArtifactDest `
    -WithSymbols:$IncludeDebugSymbols))

# Compatibility/public headers staged by producer packages into _deps/workspace.
# EnvTwinWorkbench still includes these through $(FlightEnvDepsWorkspaceRoot).
foreach ($workspaceHeaderDir in @('EnvContracts', 'EnvNodeSupport', 'EnvNodeTools', 'EnvPredictorIO')) {
    $workspaceHeaderSource = Join-Path $workspaceDepsRoot $workspaceHeaderDir
    $workspaceHeaderDest = Join-Path (Join-Path $packageRoot '_deps\workspace') $workspaceHeaderDir
    $summaries.Add((Copy-TreeFiltered `
        -SourceRoot $workspaceHeaderSource `
        -DestinationRoot $workspaceHeaderDest `
        -ExcludedDirectoryNames @('.git', '.vs', '__pycache__', 'obj', 'x64', 'Debug', 'Release') `
        -ExcludedExtensions @('.pdb', '.ilk', '.exp')))
}

# Generated PDK headers and compiled plan artifacts used by platform UI.
$generatedRoot = Join-Path $workspaceHome '_local_artifacts\platform-pdk\codegen'
$summaries.Add((Copy-TreeFiltered `
    -SourceRoot $generatedRoot `
    -DestinationRoot (Join-Path $packageRoot '_local_artifacts\platform-pdk\codegen') `
    -ExcludedDirectoryNames @('.git', '.vs', '__pycache__') `
    -ExcludedExtensions @('.pdb', '.ilk', '.exp')))

$adapterRegistryRoot = Join-Path $workspaceHome '_local_artifacts\platform-runtime\adapter-registries'
$summaries.Add((Copy-TreeFiltered `
    -SourceRoot $adapterRegistryRoot `
    -DestinationRoot (Join-Path $packageRoot '_local_artifacts\platform-runtime\adapter-registries') `
    -ExcludedDirectoryNames @('.git', '.vs', '__pycache__') `
    -ExcludedExtensions @('.pdb', '.ilk', '.exp')))

$latestRunSummary = [ordered]@{ included = $false; reason = 'not requested' }
if ($IncludeLatestRun) {
    $latestRunSummary = Copy-LatestMainlineRun -WorkspaceHome $workspaceHome -PackageRoot $packageRoot
}

if ($IncludeQtSdk) {
    if ([string]::IsNullOrWhiteSpace($QtInstall)) {
        if (-not [string]::IsNullOrWhiteSpace($env:QT6_ROOT)) {
            $QtInstall = $env:QT6_ROOT
        }
        elseif (-not [string]::IsNullOrWhiteSpace($env:QTDIR)) {
            $QtInstall = $env:QTDIR
        }
        else {
            $QtInstall = 'C:\Qt6\6.2.0\msvc2019_64'
        }
    }
    if (-not (Test-Path -LiteralPath $QtInstall -PathType Container)) {
        throw "QtInstall not found: $QtInstall"
    }
    $summaries.Add((Copy-TreeFiltered `
        -SourceRoot $QtInstall `
        -DestinationRoot (Join-Path $packageRoot '_deps\qt\msvc2019_64') `
        -ExcludedDirectoryNames @('.git', '.vs', 'examples', 'Docs', 'doc', 'debug') `
        -ExcludedExtensions @('.pdb', '.ilk', '.exp')))
}

$requiredRuntimeFiles = @(
    'EnvTwinWorkbench.exe',
    'EnvPlatformController.exe',
    'FlightEnvPlatformRuntimeHost.exe',
    'ReentryFieldOperators.dll',
    'ReentryBallisticOperators.dll',
    'FlightEnvNodeSdk.dll',
    'EnvPredictorIO.dll'
)
$missingRuntimeFiles = New-Object System.Collections.Generic.List[string]
foreach ($fileName in $requiredRuntimeFiles) {
    if (-not (Test-Path -LiteralPath (Join-Path $workspaceArtifactDest $fileName) -PathType Leaf)) {
        $missingRuntimeFiles.Add($fileName)
    }
}
if ($missingRuntimeFiles.Count -gt 0) {
    throw "Dev bundle is missing required runtime files: $($missingRuntimeFiles -join ', '). Build platform UI/runtime artifacts first."
}

$readmePath = Join-Path $packageRoot 'README_PLATFORM_UI_DEV_BUNDLE.md'
$readme = @"
# FlightEnv platform UI development bundle

This directory is a workspace-shaped development bundle for UI work.

## Layout

- flightenv-platform-ui/ : platform UI source and Visual Studio solutions.
- flightenv-object-reentry-vehicle/ : reentry object package used by the UI.
- flightenv-platform-pdk/ : schemas, validators, codegen tools, and public PDK headers.
- flightenv-platform-runtime/include : runtime public headers only.
- flightenv-node-sdk/include and flightenv-contracts/include : compatibility public headers only.
- flightenv-reentry-operators/include and flightenv-reentry-operators/tools : operator public headers/manifests only.
- _deps/include, _deps/lib, _deps/bin, _deps/share : shared third-party SDK/runtime dependencies.
- _deps/workspace/$Platform/$Configuration : built FlightEnv binaries and import libraries.
- _deps/example and _deps/data : local object resources, meshes, configs, and optional heavy DB files.
- _local_artifacts/platform-pdk/codegen : generated typed DTO/binding headers and codegen manifests.
- _local_artifacts/platform-runtime/adapter-registries : runtime adapter registry artifacts.
- tools/ : shared launch and MSBuild helper scripts.

## Build

Open:

```
flightenv-platform-ui/flightenv-platform-ui.sln
```

or run:

```
powershell -NoProfile -ExecutionPolicy Bypass -File .\flightenv-platform-ui\tools\build_platform_workbench.ps1 -Configuration $Configuration -Platform $Platform
```

If Qt SDK is not included in this bundle, install Qt 6.2 msvc2019_64 and set QT6_ROOT or QTDIR before building.

## Run

```
.\tools\start_flightenv_exe.ps1 -Exe EnvTwinWorkbench
```

The package preserves the normal workspace-relative layout, so existing VS debugger paths and scripts continue to resolve _deps and _local_artifacts from this bundle root.

## Notes

- Private runtime source and non-UI producer implementation source are not included.
- Headers, schemas, object package definitions, generated DTOs, import libraries, DLLs, and runtime resources are included.
- Use -SkipHeavyData when generating a light bundle without large database files.
- Use -IncludeQtSdk only when you intentionally want to copy the local Qt SDK into the bundle.
"@
Set-Content -LiteralPath $readmePath -Encoding UTF8 -Value $readme

$repoCommits = [ordered]@{
    flightenv_platform_ui = (Get-GitCommitOrEmpty -RepoPath (Join-Path $workspaceHome 'flightenv-platform-ui'))
    flightenv_platform_pdk = (Get-GitCommitOrEmpty -RepoPath (Join-Path $workspaceHome 'flightenv-platform-pdk'))
    flightenv_platform_runtime = (Get-GitCommitOrEmpty -RepoPath (Join-Path $workspaceHome 'flightenv-platform-runtime'))
    flightenv_object_reentry_vehicle = (Get-GitCommitOrEmpty -RepoPath (Join-Path $workspaceHome 'flightenv-object-reentry-vehicle'))
    flightenv_reentry_operators = (Get-GitCommitOrEmpty -RepoPath (Join-Path $workspaceHome 'flightenv-reentry-operators'))
    flightenv_node_sdk = (Get-GitCommitOrEmpty -RepoPath (Join-Path $workspaceHome 'flightenv-node-sdk'))
    flightenv_contracts = (Get-GitCommitOrEmpty -RepoPath (Join-Path $workspaceHome 'flightenv-contracts'))
    flightenv_trajectory = (Get-GitCommitOrEmpty -RepoPath (Join-Path $workspaceHome 'flightenv-trajectory'))
}

$packageManifest = @{
    package_name = 'flightenv-platform-ui-dev-bundle'
    package_layout = 'workspace-shaped-dev-bundle'
    configuration = $Configuration
    platform = $Platform
    created_at_utc = (Get-Date).ToUniversalTime().ToString('o')
    package_root = $packageRoot
    workspace_home = $workspaceHome
    skip_heavy_data = [bool]$SkipHeavyData
    include_debug_symbols = [bool]$IncludeDebugSymbols
    include_qt_sdk = [bool]$IncludeQtSdk
    include_latest_run = [bool]$IncludeLatestRun
    repositories = $repoCommits
    dependency_policy = @{
        private_runtime_source = 'not included'
        public_headers = 'included for producer repos'
        built_binaries = 'included under _deps/workspace'
        shared_third_party = 'included under _deps'
        object_resources = 'included under _deps/example and _deps/data'
    }
    latest_run = $latestRunSummary
    copy_summaries = @($summaries.ToArray())
}
$packageManifest | ConvertTo-Json -Depth 40 | Set-Content -LiteralPath (Join-Path $packageRoot 'package-manifest.json') -Encoding UTF8

$zipCreated = $false
$shaPath = ''
if (-not $SkipZip) {
    Compress-DirectoryToZip -SourceDirectory $packageRoot -DestinationZip $zipPath
    $shaPath = New-Sha256File -PathValue $zipPath
    $zipCreated = $true
}

Write-Host 'Created platform UI development bundle:'
Write-Host "  Root: $packageRoot"
if ($zipCreated) {
    Write-Host "  Zip:  $zipPath"
    Write-Host "  SHA:  $shaPath"
}
else {
    Write-Host '  Zip:  skipped'
}
Write-Host "  Heavy data included: $(-not [bool]$SkipHeavyData)"
Write-Host "  Latest run included: $([bool]$IncludeLatestRun)"
