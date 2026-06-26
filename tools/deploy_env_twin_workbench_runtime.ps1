param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",

    [ValidateSet("x64", "Win32")]
    [string]$Platform = "x64",

    [string]$WorkspaceRoot = "",
    [string]$QtInstall = ""
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($WorkspaceRoot)) {
    $WorkspaceRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
}

function Resolve-QtInstall {
    param([string]$Hint)

    $candidates = @()
    if (-not [string]::IsNullOrWhiteSpace($Hint)) {
        $candidates += $Hint
    }
    if ($env:FlightEnvQtInstall) {
        $candidates += $env:FlightEnvQtInstall
    }
    $candidates += @(
        "C:\Qt6\6.2.0\msvc2019_64",
        "C:\Qt\6.2.0\msvc2019_64",
        "C:\Qt\6.2.0\msvc2022_64",
        "C:\Qt6\6.2.0\msvc2022_64"
    )

    foreach ($candidate in $candidates) {
        $tool = Join-Path $candidate "bin\windeployqt.exe"
        if (Test-Path -LiteralPath $tool -PathType Leaf) {
            return (Resolve-Path $candidate).Path
        }
    }

    throw "windeployqt.exe not found. Pass -QtInstall <Qt install dir>."
}

function Assert-File {
    param(
        [string]$Path,
        [string]$Label
    )
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Label missing: $Path"
    }
}

function Copy-RuntimeDlls {
    param(
        [string]$SourceDir,
        [string]$Label,
        [scriptblock]$Filter
    )

    if (-not (Test-Path -LiteralPath $SourceDir -PathType Container)) {
        Write-Host "Skipping $Label runtime, directory not found: $SourceDir"
        return
    }

    $files = Get-ChildItem -LiteralPath $SourceDir -File -Filter "*.dll" |
        Where-Object { & $Filter $_ }

    foreach ($file in $files) {
        $target = Join-Path $outputDir $file.Name
        Copy-Item -LiteralPath $file.FullName -Destination $target -Force
    }

    Write-Host "$Label runtime DLLs deployed: $($files.Count)"
}

$outputDir = Join-Path $WorkspaceRoot ("_deps\workspace\{0}\{1}" -f $Platform, $Configuration)
$exePath = Join-Path $outputDir "EnvTwinWorkbench.exe"
Assert-File -Path $exePath -Label "EnvTwinWorkbench executable"

$resolvedQt = Resolve-QtInstall -Hint $QtInstall
$deployTool = Join-Path $resolvedQt "bin\windeployqt.exe"

& $deployTool --release --compiler-runtime $exePath
if ($LASTEXITCODE -ne 0) {
    throw "windeployqt failed with exit code $LASTEXITCODE"
}

Assert-File -Path (Join-Path $outputDir "Qt6Core.dll") -Label "Qt6Core"
Assert-File -Path (Join-Path $outputDir "Qt6Widgets.dll") -Label "Qt6Widgets"
Assert-File -Path (Join-Path $outputDir "platforms\qwindows.dll") -Label "Qt platform plugin qwindows"

$depsRoot = Join-Path $WorkspaceRoot "_deps"
foreach ($runtimeDir in @(
    "bin\qt_runtime",
    "bin\visualization_runtime",
    "bin\third_party_runtime",
    "bin\legacy_support_runtime",
    "bin",
    ("bin\{0}" -f $Platform)
)) {
    Copy-RuntimeDlls `
        -SourceDir (Join-Path $depsRoot $runtimeDir) `
        -Label $runtimeDir `
        -Filter { param($File) return $true }
}

Copy-RuntimeDlls `
    -SourceDir (Join-Path $depsRoot "bin\VTK") `
    -Label "VTK release" `
    -Filter {
        param($File)
        return ($File.Name -notmatch "d\.dll$")
    }

Assert-File -Path (Join-Path $outputDir "vtkCommonCore-9.4.dll") -Label "VTK common core"
Assert-File -Path (Join-Path $outputDir "vtkGUISupportQt-9.4.dll") -Label "VTK Qt support"
Assert-File -Path (Join-Path $outputDir "fmt.dll") -Label "fmt runtime"

Write-Host "[OK] EnvTwinWorkbench Qt runtime deployed."
Write-Host "exe=$exePath"
Write-Host "qt=$resolvedQt"
