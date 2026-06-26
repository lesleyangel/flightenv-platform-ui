param(
    [ValidateSet('Release', 'Debug')]
    [string]$Configuration = 'Release',

    [ValidateSet('x64', 'Win32')]
    [string]$Platform = 'x64',

    [string]$WorkspaceRoot = '',

    [string]$OutputRoot = '',

    [string]$PackageName = '',

    [switch]$Clean,

    [switch]$Overwrite
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

function Copy-FileRequired {
    param(
        [Parameter(Mandatory = $true)][string]$SourcePath,
        [Parameter(Mandatory = $true)][string]$DestinationPath
    )
    if (-not (Test-Path -LiteralPath $SourcePath -PathType Leaf)) {
        throw "Required file not found: $SourcePath"
    }
    Ensure-Directory -PathValue ([System.IO.Path]::GetDirectoryName($DestinationPath))
    Copy-Item -LiteralPath $SourcePath -Destination $DestinationPath -Force
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
            throw "tar.exe failed to create controller-ui package zip: $DestinationZip"
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

function Get-FileManifestEntry {
    param([Parameter(Mandatory = $true)][System.IO.FileInfo]$File)
    [ordered]@{
        name = $File.Name
        relative_path = $File.FullName.Substring($packageRoot.Length).TrimStart('\', '/')
        length = $File.Length
        sha256 = (Get-FileHash -LiteralPath $File.FullName -Algorithm SHA256).Hash
    }
}

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$workspaceRoot = Get-FlightEnvSharedWorkspaceRoot -RepoRoot $repoRoot -PreferredWorkspaceRoot $WorkspaceRoot
$buildRoot = Join-Path -Path $workspaceRoot -ChildPath ("{0}\{1}" -f $Platform, $Configuration)

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = [System.IO.Path]::GetFullPath((Join-Path $repoRoot '..\_local_artifacts\flightenv-platform-controller-ui\controller-ui-package'))
}
else {
    $OutputRoot = [System.IO.Path]::GetFullPath($OutputRoot)
}
if ([string]::IsNullOrWhiteSpace($PackageName)) {
    $PackageName = "flightenv-platform-controller-ui-win-$Platform-$Configuration"
}

$packageRoot = Join-Path $OutputRoot 'controller-ui'
$zipPath = Join-Path $OutputRoot ($PackageName + '.zip')
if ($Clean -and (Test-Path -LiteralPath $packageRoot -PathType Container)) {
    Remove-Item -LiteralPath $packageRoot -Recurse -Force
}
Ensure-Directory -PathValue $packageRoot

Copy-FileRequired -SourcePath (Join-Path $buildRoot 'EnvPlatformController.exe') -DestinationPath (Join-Path $packageRoot 'bin\EnvPlatformController.exe')

$packageManifest = [ordered]@{
    package_name = 'flightenv-platform-controller-ui'
    package_layout = 'artifact-only'
    configuration = $Configuration
    platform = $Platform
    source_repository = 'flightenv-platform-ui'
    source_commit = (& git -C $repoRoot rev-parse HEAD)
    created_at_utc = (Get-Date).ToUniversalTime().ToString('o')
    dependency_policy = [ordered]@{
        delivery = 'controller UI executable only'
        required_runtime = 'consume runtime-private artifact package separately'
        required_node_sdk = 'consume node-sdk artifact package separately'
        required_external_deps = 'consume workspace _deps or documented dependency package separately'
    }
    files = @()
}
$packageManifest.files = @(Get-ChildItem -LiteralPath $packageRoot -Recurse -File | ForEach-Object { Get-FileManifestEntry -File $_ })
$packageManifest | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath (Join-Path $packageRoot 'package-manifest.json') -Encoding UTF8

$forbiddenSourceExtensions = [System.Collections.Generic.HashSet[string]]::new(
    [string[]]@('.cpp', '.c', '.cc', '.cxx'),
    [System.StringComparer]::OrdinalIgnoreCase)
$forbiddenSourceFiles = @(Get-ChildItem -LiteralPath $packageRoot -Recurse -File |
    Where-Object { $forbiddenSourceExtensions.Contains($_.Extension) })
if ($forbiddenSourceFiles.Count -gt 0) {
    $paths = $forbiddenSourceFiles | ForEach-Object { $_.FullName }
    throw "Controller UI artifact package contains implementation source files:`n$($paths -join "`n")"
}

Compress-DirectoryToZip -SourceDirectory $packageRoot -DestinationZip $zipPath
$shaPath = New-Sha256File -PathValue $zipPath

Write-Host 'Created controller-ui package:'
Write-Host "  Root: $packageRoot"
Write-Host "  Zip:  $zipPath"
Write-Host "  SHA:  $shaPath"
