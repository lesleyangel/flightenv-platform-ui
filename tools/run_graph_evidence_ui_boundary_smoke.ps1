param(
    [ValidateSet('Release', 'Debug')]
    [string]$Configuration = 'Release',

    [ValidateSet('x64', 'Win32')]
    [string]$Platform = 'x64',

    [string]$WorkspaceRoot = '',

    [string]$SharedDepsRoot = '',

    [string]$NodeSdkRoot = '',

    [string]$RuntimePrivateRoot = '',

    [string]$RunDir = '',

    [switch]$VerboseMSBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Ensure-Directory {
    param([Parameter(Mandatory = $true)][string]$PathValue)
    if (-not (Test-Path -LiteralPath $PathValue -PathType Container)) {
        New-Item -ItemType Directory -Path $PathValue -Force | Out-Null
    }
}

function Resolve-MSBuild {
    if ($env:MSBUILD_EXE -and (Test-Path -LiteralPath $env:MSBUILD_EXE -PathType Leaf)) {
        return (Resolve-Path -LiteralPath $env:MSBUILD_EXE).Path
    }

    $vswhereCandidates = @(
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe",
        "$env:ProgramFiles\Microsoft Visual Studio\Installer\vswhere.exe"
    )
    foreach ($vswhere in $vswhereCandidates) {
        if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
            continue
        }
        $candidate = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
        if ($candidate -and (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            return $candidate
        }
    }

    $pathCandidate = Get-Command MSBuild.exe -ErrorAction SilentlyContinue
    if ($pathCandidate -and (Test-Path -LiteralPath $pathCandidate.Source -PathType Leaf)) {
        return $pathCandidate.Source
    }

    throw 'MSBuild not found. Install Visual Studio Build Tools, add MSBuild.exe to PATH, or set MSBUILD_EXE.'
}

function Normalize-ProcessPathEnvironment {
    $envMap = [System.Environment]::GetEnvironmentVariables()
    $pathValue = $envMap['Path']
    if ([string]::IsNullOrEmpty($pathValue)) {
        $pathValue = $envMap['PATH']
    }
    if (-not [string]::IsNullOrEmpty($pathValue)) {
        [System.Environment]::SetEnvironmentVariable('PATH', $null, 'Process')
        [System.Environment]::SetEnvironmentVariable('Path', $pathValue, 'Process')
    }
}

function Resolve-Directory {
    param(
        [Parameter(Mandatory = $true)][string]$PathValue,
        [Parameter(Mandatory = $true)][string]$Label
    )
    $full = [System.IO.Path]::GetFullPath($PathValue)
    if (-not (Test-Path -LiteralPath $full -PathType Container)) {
        throw "$Label not found: $full"
    }
    return $full
}

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$workspaceHome = [System.IO.Path]::GetFullPath((Join-Path $repoRoot '..'))

if ([string]::IsNullOrWhiteSpace($WorkspaceRoot)) {
    $WorkspaceRoot = Join-Path $workspaceHome '_deps\workspace'
}
$WorkspaceRoot = Resolve-Directory -PathValue $WorkspaceRoot -Label 'workspace root'

if ([string]::IsNullOrWhiteSpace($SharedDepsRoot)) {
    if (-not [string]::IsNullOrWhiteSpace($env:FLIGHTENV_SHARED_DEPS_ROOT)) {
        $SharedDepsRoot = $env:FLIGHTENV_SHARED_DEPS_ROOT
    }
    else {
        $SharedDepsRoot = Join-Path $workspaceHome '_deps'
    }
}
$SharedDepsRoot = Resolve-Directory -PathValue $SharedDepsRoot -Label 'shared deps root'

if ([string]::IsNullOrWhiteSpace($NodeSdkRoot)) {
    $NodeSdkRoot = Join-Path $workspaceHome 'flightenv-node-sdk'
}
$NodeSdkRoot = Resolve-Directory -PathValue $NodeSdkRoot -Label 'node-sdk root'
$nodeSdkIncludeRoot = Join-Path $NodeSdkRoot 'include'
if (-not (Test-Path -LiteralPath (Join-Path $nodeSdkIncludeRoot 'EnvNodeSupport\GraphRunEvidenceReader.h') -PathType Leaf)) {
    throw "GraphRunEvidenceReader public header not found under node-sdk include: $nodeSdkIncludeRoot"
}

if ([string]::IsNullOrWhiteSpace($RuntimePrivateRoot)) {
    if (-not [string]::IsNullOrWhiteSpace($env:FLIGHTENV_RUNTIME_PRIVATE_ROOT)) {
        $RuntimePrivateRoot = $env:FLIGHTENV_RUNTIME_PRIVATE_ROOT
    }
    else {
        $RuntimePrivateRoot = Join-Path $workspaceHome '_local_artifacts\runtime-private\runtime-private'
    }
}
$RuntimePrivateRoot = Resolve-Directory -PathValue $RuntimePrivateRoot -Label 'runtime-private artifact root'

if ([string]::IsNullOrWhiteSpace($RunDir)) {
    $RunDir = Join-Path $workspaceHome '_local_artifacts\flightenv-runtime-private\graph-runtime-controller\ui_live_run'
}
$RunDir = Resolve-Directory -PathValue $RunDir -Label 'graph run evidence directory'
if (-not (Test-Path -LiteralPath (Join-Path $RunDir 'graph_run_evidence.json') -PathType Leaf)) {
    throw "graph_run_evidence.json not found in: $RunDir"
}

$buildRoot = Join-Path $WorkspaceRoot ("{0}\{1}" -f $Platform, $Configuration)
$sdkLib = Join-Path $buildRoot 'FlightEnvNodeSdk.lib'
$sdkDll = Join-Path $buildRoot 'FlightEnvNodeSdk.dll'
if (-not (Test-Path -LiteralPath $sdkLib -PathType Leaf)) {
    throw "SDK import library missing: $sdkLib. Build node-sdk first."
}
if (-not (Test-Path -LiteralPath $sdkDll -PathType Leaf)) {
    throw "SDK DLL missing: $sdkDll. Build node-sdk first."
}

$smokeRoot = Join-Path $workspaceHome '_local_artifacts\flightenv-controller-ui\graph-evidence-ui-boundary-smoke'
Ensure-Directory -PathValue $smokeRoot
$sourcePath = Join-Path $smokeRoot 'GraphEvidenceUiBoundarySmoke.cpp'
$projectPath = Join-Path $smokeRoot 'GraphEvidenceUiBoundarySmoke.vcxproj'
$exePath = Join-Path $smokeRoot 'bin\GraphEvidenceUiBoundarySmoke.exe'
$reportPath = Join-Path $smokeRoot 'graph_evidence_ui_boundary_report.json'

$source = @"
#include <filesystem>
#include <fstream>
#include <iostream>

#include "EnvNodeSupport/GraphRunEvidenceReader.h"
#include <nlohmann/json.hpp>

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "usage: ui-smoke <run_dir> <report_path>\n";
    return 2;
  }

  const auto view = launchsupport::read_graph_run_evidence(argv[1]);
  if (view.graph_nodes.empty() || view.operator_snapshots.empty()) {
    std::cerr << "UI cannot display graph nodes or operator snapshots\n";
    return 10;
  }
  if (view.result_ports.empty()) {
    std::cerr << "UI cannot display graph result DTO ports through SDK\n";
    return 14;
  }
  if (!view.has_runtime_snapshot || !std::filesystem::exists(view.runtime_snapshot_path)) {
    std::cerr << "UI cannot render real VTK fields because runtime_snapshot.json is missing\n";
    return 16;
  }
  if (!view.has_workflow_timeline || !std::filesystem::exists(view.workflow_timeline_path)) {
    std::cerr << "UI cannot display workflow timeline counts because workflow_timeline.json is missing\n";
    return 18;
  }
  {
    std::ifstream timeline_in(view.workflow_timeline_path);
    const auto timeline = nlohmann::json::parse(timeline_in);
    if (timeline.value("observed_frame_count", 0) <= 1 ||
        timeline.value("prediction_run_count", 0) <= 1) {
      std::cerr << "UI timeline view did not receive multi-frame replay and repeated prediction counts\n";
      return 20;
    }
  }
  if (!view.has_state_future || !view.has_qoi_series) {
    std::cerr << "UI cannot display state/qoi forecast outputs\n";
    return 11;
  }
  if (!view.has_field_forecast || !view.has_damage_forecast || !view.has_life_assessment || !view.has_life_field) {
    std::cerr << "UI optional field/damage/life capability flags are incomplete\n";
    return 12;
  }

  std::size_t dll_or_ros_rows = 0;
  for (const auto& op : view.operator_snapshots) {
    if (op.execution_kind == "dll" || op.execution_kind == "ros2_exe" ||
        op.execution_kind == "in_process" || op.execution_kind == "cli_exe") {
      ++dll_or_ros_rows;
    }
  }
  if (dll_or_ros_rows != view.operator_snapshots.size()) {
    std::cerr << "UI operator table would contain an unknown execution kind\n";
    return 13;
  }
  bool saw_life_payload = false;
  bool saw_life_field_payload = false;
  bool saw_standard_trajectory_sample = false;
  for (const auto& port : view.result_ports) {
    if (port.port_name == "life.assessment" && !port.payload_json.empty()) {
      saw_life_payload = true;
    }
    if (port.port_name == "life.field" && !port.payload_json.empty()) {
      const auto life_field = nlohmann::json::parse(port.payload_json);
      saw_life_field_payload = !life_field.value("items", nlohmann::json::array()).empty();
    }
    if (port.port_name == "state.future" && !port.payload_json.empty()) {
      const auto trajectory = nlohmann::json::parse(port.payload_json);
      const auto samples = trajectory.value("samples", nlohmann::json::array());
      if (!samples.empty() && samples.front().contains("lla_rad_m") &&
          !samples.front().contains("trajectory_sample")) {
        saw_standard_trajectory_sample = true;
      }
    }
  }
  if (!saw_life_payload) {
    std::cerr << "UI cannot display life assessment DTO payload through SDK\n";
    return 15;
  }
  if (!saw_life_field_payload) {
    std::cerr << "UI cannot display life.field FieldBundleDTO payload through SDK\n";
    return 19;
  }
  if (!saw_standard_trajectory_sample) {
    std::cerr << "UI trajectory display requires standard TrajectoryPredictionFrame samples\n";
    return 17;
  }

  std::ofstream report(argv[2], std::ios::binary);
  report << "{\n"
         << "  \"ok\": true,\n"
         << "  \"source\": \"flightenv-controller-ui.graph-evidence-ui-boundary-smoke\",\n"
         << "  \"run_id\": \"" << view.run_id << "\",\n"
         << "  \"graph_template_id\": \"" << view.graph_template_id << "\",\n"
         << "  \"graph_rows\": " << view.graph_nodes.size() << ",\n"
         << "  \"operator_rows\": " << view.operator_snapshots.size() << ",\n"
         << "  \"result_rows\": " << view.result_ports.size() << ",\n"
         << "  \"has_runtime_snapshot\": " << (view.has_runtime_snapshot ? "true" : "false") << ",\n"
         << "  \"has_workflow_timeline\": " << (view.has_workflow_timeline ? "true" : "false") << ",\n"
         << "  \"show_state_qoi\": " << ((view.has_state_future && view.has_qoi_series) ? "true" : "false") << ",\n"
         << "  \"show_structural_health\": " << ((view.has_field_forecast && view.has_damage_forecast && view.has_life_assessment && view.has_life_field) ? "true" : "false") << "\n"
         << "}\n";

  std::cout << "Graph evidence UI boundary smoke passed\n";
  std::cout << "graph_rows=" << view.graph_nodes.size()
            << " operator_rows=" << view.operator_snapshots.size()
            << " result_rows=" << view.result_ports.size() << "\n";
  return 0;
}
"@
Set-Content -LiteralPath $sourcePath -Encoding UTF8 -Value $source

$includeDirs = @(
    $nodeSdkIncludeRoot,
    $WorkspaceRoot,
    (Join-Path $workspaceHome 'flightenv-contracts\include'),
    (Join-Path $RuntimePrivateRoot 'include_public'),
    (Join-Path $SharedDepsRoot 'include'),
    (Join-Path $SharedDepsRoot 'include\ros2include'),
    (Join-Path $SharedDepsRoot 'include\sensor_msgs_plus\sensor_msgs_plus')
) | Where-Object { Test-Path -LiteralPath $_ -PathType Container }

$libraryDirs = @(
    $buildRoot,
    (Join-Path $SharedDepsRoot 'lib\ros2'),
    (Join-Path $SharedDepsRoot 'lib\sensor_msgs_plus'),
    (Join-Path $SharedDepsRoot 'lib\x64'),
    (Join-Path $SharedDepsRoot 'lib')
) | Where-Object { Test-Path -LiteralPath $_ -PathType Container }

$project = @"
<?xml version="1.0" encoding="utf-8"?>
<Project DefaultTargets="Build" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup Label="ProjectConfigurations">
    <ProjectConfiguration Include="$Configuration|$Platform">
      <Configuration>$Configuration</Configuration>
      <Platform>$Platform</Platform>
    </ProjectConfiguration>
  </ItemGroup>
  <PropertyGroup Label="Globals">
    <VCProjectVersion>17.0</VCProjectVersion>
    <Keyword>Win32Proj</Keyword>
    <ProjectGuid>{3D8EE674-6D46-4855-B241-53D0E40E18B3}</ProjectGuid>
    <RootNamespace>GraphEvidenceUiBoundarySmoke</RootNamespace>
    <WindowsTargetPlatformVersion>10.0</WindowsTargetPlatformVersion>
  </PropertyGroup>
  <Import Project="`$(VCTargetsPath)\Microsoft.Cpp.Default.props" />
  <PropertyGroup Label="Configuration">
    <ConfigurationType>Application</ConfigurationType>
    <UseDebugLibraries>false</UseDebugLibraries>
    <PlatformToolset>v143</PlatformToolset>
    <CharacterSet>MultiByte</CharacterSet>
  </PropertyGroup>
  <Import Project="`$(VCTargetsPath)\Microsoft.Cpp.props" />
  <PropertyGroup>
    <OutDir>$smokeRoot\bin\</OutDir>
    <IntDir>$smokeRoot\obj\</IntDir>
    <TargetName>GraphEvidenceUiBoundarySmoke</TargetName>
  </PropertyGroup>
  <ItemDefinitionGroup>
    <ClCompile>
      <WarningLevel>Level3</WarningLevel>
      <LanguageStandard>stdcpp17</LanguageStandard>
      <ConformanceMode>true</ConformanceMode>
      <AdditionalOptions>/utf-8 %(AdditionalOptions)</AdditionalOptions>
      <AdditionalIncludeDirectories>$($includeDirs -join ';');%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
      <PreprocessorDefinitions>NOMINMAX;_CRT_SECURE_NO_WARNINGS;%(PreprocessorDefinitions)</PreprocessorDefinitions>
      <RuntimeLibrary>MultiThreadedDLL</RuntimeLibrary>
    </ClCompile>
    <Link>
      <AdditionalLibraryDirectories>$($libraryDirs -join ';');%(AdditionalLibraryDirectories)</AdditionalLibraryDirectories>
      <AdditionalDependencies>FlightEnvNodeSdk.lib;%(AdditionalDependencies)</AdditionalDependencies>
    </Link>
  </ItemDefinitionGroup>
  <ItemGroup>
    <ClCompile Include="$sourcePath" />
  </ItemGroup>
  <Import Project="`$(VCTargetsPath)\Microsoft.Cpp.targets" />
</Project>
"@
Set-Content -LiteralPath $projectPath -Encoding UTF8 -Value $project

$msbuild = Resolve-MSBuild
$verbosity = if ($VerboseMSBuild) { 'normal' } else { 'minimal' }
Normalize-ProcessPathEnvironment
& $msbuild $projectPath /m /nologo "/p:Configuration=$Configuration" "/p:Platform=$Platform" "/v:$verbosity"
if ($LASTEXITCODE -ne 0) {
    throw "Graph evidence UI boundary smoke build failed with exit code $LASTEXITCODE"
}

$runtimePathEntries = @(
    $buildRoot,
    (Join-Path $RuntimePrivateRoot 'bin\project'),
    (Join-Path $SharedDepsRoot 'bin\ros2_runtime'),
    (Join-Path $SharedDepsRoot 'bin\qt_runtime'),
    (Join-Path $SharedDepsRoot 'bin\visualization_runtime'),
    (Join-Path $SharedDepsRoot 'bin\third_party_runtime'),
    (Join-Path $SharedDepsRoot 'bin\legacy_support_runtime'),
    (Join-Path $SharedDepsRoot 'bin\VTK'),
    (Join-Path $SharedDepsRoot 'bin\sensor_msgs_plus'),
    (Join-Path $SharedDepsRoot 'bin'),
    (Join-Path $SharedDepsRoot 'bin\x64'),
    (Join-Path $SharedDepsRoot 'lib\ros2'),
    (Join-Path $SharedDepsRoot 'lib\x64'),
    (Join-Path $SharedDepsRoot 'lib')
) | Where-Object { Test-Path -LiteralPath $_ -PathType Container }

$oldPath = $env:PATH
$env:PATH = "$($runtimePathEntries -join ';');$env:PATH"
try {
    & $exePath $RunDir $reportPath
    if ($LASTEXITCODE -ne 0) {
        throw "Graph evidence UI boundary smoke executable failed with exit code $LASTEXITCODE"
    }
}
finally {
    $env:PATH = $oldPath
}

Write-Host 'Graph evidence UI boundary smoke passed.'
Write-Host "  run    = $RunDir"
Write-Host "  report = $reportPath"
