Set-StrictMode -Version Latest

function Resolve-FlightEnvPath {
    param(
        [Parameter(Mandatory = $true)][string]$PathValue,
        [string]$BasePath = ''
    )

    if ([System.IO.Path]::IsPathRooted($PathValue)) {
        return [System.IO.Path]::GetFullPath($PathValue)
    }
    if ([string]::IsNullOrWhiteSpace($BasePath)) {
        return [System.IO.Path]::GetFullPath($PathValue)
    }
    return [System.IO.Path]::GetFullPath((Join-Path -Path $BasePath -ChildPath $PathValue))
}

function Get-FlightEnvSharedDepsRoot {
    param(
        [Parameter(Mandatory = $true)][string]$RepoRoot,
        [string]$PreferredRoot = ''
    )

    if (-not [string]::IsNullOrWhiteSpace($PreferredRoot)) {
        return Resolve-FlightEnvPath -PathValue $PreferredRoot -BasePath $RepoRoot
    }
    if (-not [string]::IsNullOrWhiteSpace($env:FLIGHTENV_SHARED_DEPS_ROOT)) {
        return Resolve-FlightEnvPath -PathValue $env:FLIGHTENV_SHARED_DEPS_ROOT -BasePath $RepoRoot
    }
    return [System.IO.Path]::GetFullPath((Join-Path -Path $RepoRoot -ChildPath '..\_deps'))
}

function Get-FlightEnvSharedWorkspaceRoot {
    param(
        [Parameter(Mandatory = $true)][string]$RepoRoot,
        [string]$RepoName = '',
        [string]$SharedDepsRoot = '',
        [string]$PreferredWorkspaceRoot = ''
    )

    if (-not [string]::IsNullOrWhiteSpace($PreferredWorkspaceRoot)) {
        return Resolve-FlightEnvPath -PathValue $PreferredWorkspaceRoot -BasePath $RepoRoot
    }
    if (-not [string]::IsNullOrWhiteSpace($env:FLIGHTENV_DEPS_WORKSPACE_ROOT)) {
        return Resolve-FlightEnvPath -PathValue $env:FLIGHTENV_DEPS_WORKSPACE_ROOT -BasePath $RepoRoot
    }
    if (-not [string]::IsNullOrWhiteSpace($env:FLIGHTENV_WORKSPACE_ROOT)) {
        return Resolve-FlightEnvPath -PathValue $env:FLIGHTENV_WORKSPACE_ROOT -BasePath $RepoRoot
    }
    $depsRoot = Get-FlightEnvSharedDepsRoot -RepoRoot $RepoRoot -PreferredRoot $SharedDepsRoot
    return Join-Path -Path $depsRoot -ChildPath 'workspace'
}

function Ensure-FlightEnvDirectory {
    param([Parameter(Mandatory = $true)][string]$PathValue)

    if (-not (Test-Path -LiteralPath $PathValue -PathType Container)) {
        New-Item -ItemType Directory -Path $PathValue -Force | Out-Null
    }
}

function Get-FlightEnvRelativePath {
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

function Copy-FlightEnvTreeContents {
    param(
        [Parameter(Mandatory = $true)][string]$SourceRoot,
        [Parameter(Mandatory = $true)][string]$DestinationRoot,
        [string[]]$Include = @('*'),
        [string[]]$ExcludedDirectoryNames = @('.git', '.vs', '.deps', 'artifacts', 'x64', 'Win32', 'Debug', 'Release', 'GeneratedFiles', 'TestResults', 'log')
    )

    Ensure-FlightEnvDirectory -PathValue $DestinationRoot
    $sourceFull = [System.IO.Path]::GetFullPath($SourceRoot)
    Get-ChildItem -LiteralPath $sourceFull -Recurse -File -Include $Include | ForEach-Object {
        $relative = Get-FlightEnvRelativePath -SourceRoot $sourceFull -PathValue $_.FullName
        $parts = $relative -split '[\\/]'
        foreach ($part in $parts) {
            if ($ExcludedDirectoryNames -contains $part) {
                return
            }
        }
        $targetPath = Join-Path -Path $DestinationRoot -ChildPath $relative
        Ensure-FlightEnvDirectory -PathValue ([System.IO.Path]::GetDirectoryName($targetPath))
        Copy-Item -LiteralPath $_.FullName -Destination $targetPath -Force
    }
}

function Add-FlightEnvExternalDependencyLinks {
    param(
        [Parameter(Mandatory = $true)][string]$ExternalRoot,
        [Parameter(Mandatory = $true)][string]$WorkspaceRoot,
        [string[]]$DirectoryNames = @('include', 'bin', 'lib', 'example', 'data')
    )

    Ensure-FlightEnvDirectory -PathValue $WorkspaceRoot
    foreach ($name in $DirectoryNames) {
        $source = Join-Path -Path $ExternalRoot -ChildPath $name
        if (Test-Path -LiteralPath $source -PathType Container) {
            Write-Host "External dependency '$name' stays under $source"
        }
    }
}
