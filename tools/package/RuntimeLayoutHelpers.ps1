Set-StrictMode -Version Latest

function Resolve-FlightEnvPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$PathValue,

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

function Get-FlightEnvLocalArtifactsRoot {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RepoRoot
    )

    if ($env:FLIGHTENV_LOCAL_ARTIFACTS_ROOT) {
        return Resolve-FlightEnvPath -PathValue $env:FLIGHTENV_LOCAL_ARTIFACTS_ROOT -BasePath $RepoRoot
    }

    return Resolve-FlightEnvPath -PathValue '..\_local_artifacts\flightenv-controller-ui' -BasePath $RepoRoot
}

function Get-FlightEnvShadowLayoutRoot {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RepoRoot,

        [ValidateSet('Release', 'Debug')]
        [string]$Configuration = 'Release',

        [ValidateSet('x64', 'Win32')]
        [string]$Platform = 'x64'
    )

    return Join-Path -Path (Get-FlightEnvLocalArtifactsRoot -RepoRoot $RepoRoot) -ChildPath ("shadow_layout\{0}\{1}" -f $Configuration, $Platform)
}

function Get-FlightEnvDepsWorkspaceRoot {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RepoRoot
    )

    if ($env:FLIGHTENV_DEPS_WORKSPACE_ROOT -and
        (Test-Path -LiteralPath $env:FLIGHTENV_DEPS_WORKSPACE_ROOT -PathType Container)) {
        return (Resolve-Path -LiteralPath $env:FLIGHTENV_DEPS_WORKSPACE_ROOT).Path
    }
    if ($env:FLIGHTENV_WORKSPACE_ROOT -and
        (Test-Path -LiteralPath $env:FLIGHTENV_WORKSPACE_ROOT -PathType Container)) {
        return (Resolve-Path -LiteralPath $env:FLIGHTENV_WORKSPACE_ROOT).Path
    }

    $sharedRoot = [System.IO.Path]::GetFullPath((Join-Path -Path $RepoRoot -ChildPath '..\_deps\workspace'))
    if (Test-Path -LiteralPath $sharedRoot -PathType Container) {
        return (Resolve-Path -LiteralPath $sharedRoot).Path
    }

    return $sharedRoot
}

function Get-FlightEnvRuntimeBinRoot {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RepoRoot,

        [ValidateSet('Release', 'Debug')]
        [string]$Configuration = 'Release',

        [ValidateSet('x64', 'Win32')]
        [string]$Platform = 'x64',

        [string]$PreferredRoot = ''
    )

    if ($PreferredRoot -and (Test-Path -LiteralPath $PreferredRoot -PathType Container)) {
        return (Resolve-Path -LiteralPath $PreferredRoot).Path
    }

    if ($env:FLIGHTENV_RUNTIME_BIN_ROOT -and
        (Test-Path -LiteralPath $env:FLIGHTENV_RUNTIME_BIN_ROOT -PathType Container)) {
        return (Resolve-Path -LiteralPath $env:FLIGHTENV_RUNTIME_BIN_ROOT).Path
    }

    $shadowRuntimeRoot = Join-Path -Path (Get-FlightEnvShadowLayoutRoot -RepoRoot $RepoRoot -Configuration $Configuration -Platform $Platform) -ChildPath 'runtime-private'
    $shadowProjectBinRoot = Join-Path -Path $shadowRuntimeRoot -ChildPath 'bin\project'
    if (Test-Path -LiteralPath $shadowProjectBinRoot -PathType Container) {
        return (Resolve-Path -LiteralPath $shadowProjectBinRoot).Path
    }

    $shadowBinRoot = Join-Path -Path $shadowRuntimeRoot -ChildPath 'bin'
    if (Test-Path -LiteralPath $shadowBinRoot -PathType Container) {
        return (Resolve-Path -LiteralPath $shadowBinRoot).Path
    }

    $legacyRoot = Join-Path -Path (Get-FlightEnvDepsWorkspaceRoot -RepoRoot $RepoRoot) -ChildPath ("{0}\{1}" -f $Platform, $Configuration)
    return (Resolve-Path -LiteralPath $legacyRoot).Path
}

function Get-FlightEnvRuntimeDependencyRoots {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RepoRoot,

        [ValidateSet('Release', 'Debug')]
        [string]$Configuration = 'Release',

        [ValidateSet('x64', 'Win32')]
        [string]$Platform = 'x64'
    )

    $roots = New-Object System.Collections.Generic.List[string]
    $seen = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)

    function Add-DependencyRoot {
        param([string]$PathValue)

        if ([string]::IsNullOrWhiteSpace($PathValue)) {
            return
        }
        if (-not (Test-Path -LiteralPath $PathValue -PathType Container)) {
            return
        }
        $resolved = (Resolve-Path -LiteralPath $PathValue).Path
        if ($seen.Add($resolved)) {
            $roots.Add($resolved) | Out-Null
        }
    }

    if ($env:FLIGHTENV_RUNTIME_DEPS_ROOT) {
        foreach ($item in ($env:FLIGHTENV_RUNTIME_DEPS_ROOT -split ';')) {
            Add-DependencyRoot -PathValue $item
        }
    }

    $runtimeRoot = Join-Path -Path (Get-FlightEnvShadowLayoutRoot -RepoRoot $RepoRoot -Configuration $Configuration -Platform $Platform) -ChildPath 'runtime-private'
    $depsManifestPath = Join-Path -Path $runtimeRoot -ChildPath 'deps-manifest.json'
    if (Test-Path -LiteralPath $depsManifestPath -PathType Leaf) {
        try {
            $depsManifest = Get-Content -LiteralPath $depsManifestPath -Raw | ConvertFrom-Json
            Add-DependencyRoot -PathValue $depsManifest.source_root
        }
        catch {
            Write-Warning "Failed to parse runtime dependency manifest: $depsManifestPath"
        }
    }

    $workspaceRoot = Get-FlightEnvDepsWorkspaceRoot -RepoRoot $RepoRoot
    foreach ($relativeRoot in @(
        ("{0}\{1}" -f $Platform, $Configuration),
        'tests'
    )) {
        Add-DependencyRoot -PathValue (Join-Path -Path $workspaceRoot -ChildPath $relativeRoot)
    }

    $sharedDepsRoot = Split-Path -Path $workspaceRoot -Parent
    if ($sharedDepsRoot -and (Test-Path -LiteralPath $sharedDepsRoot -PathType Container)) {
        $platformBinRoot = if ($Platform -eq 'Win32') { 'bin\x86' } else { 'bin\x64' }
        foreach ($relativeRoot in @(
            ("{0}\{1}" -f $Platform, $Configuration),
            'bin',
            $platformBinRoot,
            'bin\ros2_runtime',
            'bin\sensor_msgs_plus',
            'bin\third_party_runtime',
            'bin\legacy_support_runtime',
            'bin\visualization_runtime',
            'bin\qt_runtime',
            'lib\sensor_msgs_plus',
            'bin\VTK',
            'lib\ros2',
            'lib\x64',
            'lib'
        )) {
            Add-DependencyRoot -PathValue (Join-Path -Path $sharedDepsRoot -ChildPath $relativeRoot)
        }
    }

    return @($roots)
}

function Get-FlightEnvRuntimeLibRoot {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RepoRoot,

        [ValidateSet('Release', 'Debug')]
        [string]$Configuration = 'Release',

        [ValidateSet('x64', 'Win32')]
        [string]$Platform = 'x64',

        [string]$PreferredRoot = ''
    )

    if ($PreferredRoot -and (Test-Path -LiteralPath $PreferredRoot -PathType Container)) {
        return (Resolve-Path -LiteralPath $PreferredRoot).Path
    }

    if ($env:FLIGHTENV_RUNTIME_LIB_ROOT -and
        (Test-Path -LiteralPath $env:FLIGHTENV_RUNTIME_LIB_ROOT -PathType Container)) {
        return (Resolve-Path -LiteralPath $env:FLIGHTENV_RUNTIME_LIB_ROOT).Path
    }

    $shadowLibRoot = Join-Path -Path (Get-FlightEnvShadowLayoutRoot -RepoRoot $RepoRoot -Configuration $Configuration -Platform $Platform) -ChildPath 'runtime-private\lib'
    if (Test-Path -LiteralPath $shadowLibRoot -PathType Container) {
        return (Resolve-Path -LiteralPath $shadowLibRoot).Path
    }

    $legacyRoot = Join-Path -Path (Get-FlightEnvDepsWorkspaceRoot -RepoRoot $RepoRoot) -ChildPath ("{0}\{1}" -f $Platform, $Configuration)
    return (Resolve-Path -LiteralPath $legacyRoot).Path
}

function Get-FlightEnvRuntimePublicIncludeRoot {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RepoRoot,

        [ValidateSet('Release', 'Debug')]
        [string]$Configuration = 'Release',

        [ValidateSet('x64', 'Win32')]
        [string]$Platform = 'x64',

        [string]$PreferredRoot = ''
    )

    if ($PreferredRoot -and (Test-Path -LiteralPath $PreferredRoot -PathType Container)) {
        return (Resolve-Path -LiteralPath $PreferredRoot).Path
    }

    if ($env:FLIGHTENV_RUNTIME_PUBLIC_INCLUDE_ROOT -and
        (Test-Path -LiteralPath $env:FLIGHTENV_RUNTIME_PUBLIC_INCLUDE_ROOT -PathType Container)) {
        return (Resolve-Path -LiteralPath $env:FLIGHTENV_RUNTIME_PUBLIC_INCLUDE_ROOT).Path
    }

    $shadowPublicIncludeRoot = Join-Path -Path (Get-FlightEnvShadowLayoutRoot -RepoRoot $RepoRoot -Configuration $Configuration -Platform $Platform) -ChildPath 'runtime-private\include_public'
    if (Test-Path -LiteralPath $shadowPublicIncludeRoot -PathType Container) {
        return (Resolve-Path -LiteralPath $shadowPublicIncludeRoot).Path
    }

    return $shadowPublicIncludeRoot
}
