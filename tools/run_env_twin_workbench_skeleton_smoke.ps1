param(
    [string]$WorkspaceRoot = ""
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($WorkspaceRoot)) {
    $WorkspaceRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
}

function Assert-True {
    param(
        [bool]$Condition,
        [string]$Message
    )
    if (-not $Condition) {
        throw $Message
    }
}

function Read-Text {
    param([string]$Path)
    Assert-True (Test-Path -LiteralPath $Path -PathType Leaf) "missing file: $Path"
    return Get-Content -LiteralPath $Path -Raw -Encoding UTF8
}

$uiRoot = Join-Path $WorkspaceRoot "flightenv-platform-ui"
$workbenchRoot = Join-Path $uiRoot "EnvTwinWorkbench"
$projectPath = Join-Path $workbenchRoot "EnvTwinWorkbench.vcxproj"
$platformSolution = Join-Path $uiRoot "flightenv-platform-ui.sln"
$legacySolution = Join-Path $uiRoot "flightenv-controller-ui.sln"

Assert-True (Test-Path -LiteralPath $platformSolution -PathType Leaf) "platform UI solution missing: $platformSolution"
Assert-True (-not (Test-Path -LiteralPath $legacySolution -PathType Leaf)) "new platform UI must not expose old controller solution: $legacySolution"

$project = Read-Text $projectPath
foreach ($required in @(
    'pages\DataPlanePage.cpp',
    'pages\OperatorLibraryPage.cpp',
    'pages\RuntimeHostPage.cpp',
    'datahub\FieldRenderGuard.cpp',
    'datahub\PlatformRunController.cpp',
    'widgets\GraphWorkflowDisplayWidgets.cpp',
    'widgets\BranchRunExplorerWidgets.cpp',
    'widgets\VtkModelFieldWidget.cpp',
    'pages\GraphPage.cpp',
    'pages\ObjectPage.cpp'
)) {
    Assert-True ($project.Contains($required)) "EnvTwinWorkbench project missing: $required"
}

foreach ($forbidden in @(
    'datahub\OrchestrationBlueprints.cpp',
    'widgets\BlueprintGraphWidget.cpp',
    '..\module-demos\common',
    '..\EnvPlatformController\StreamController.cpp',
    'rclcpp.lib',
    'sensor_msgs_plus__rosidl'
)) {
    Assert-True (-not $project.Contains($forbidden)) "EnvTwinWorkbench project still contains old dependency: $forbidden"
}

Assert-True ($project -notmatch "source-supported") "EnvTwinWorkbench must not compile source-supported directly"
Assert-True ($project -notmatch "runtime-private") "EnvTwinWorkbench must not compile runtime-private source directly"

$main = Read-Text (Join-Path $workbenchRoot "main.cpp")
Assert-True ($main.Contains("--object-package=")) "main must accept object package root"
Assert-True ($main.Contains("--online-root=")) "main must accept online runtime root"
Assert-True ($main.Contains("resolveObjectPackageRoot(int argc, char** argv, const QDir&)")) "main must allow no-object default"
Assert-True (-not $main.Contains('return workspace.filePath(QStringLiteral("flightenv-object-reentry-vehicle"))')) "main must not default-load reentry object"
Assert-True (-not $main.Contains("runtime-host-runs/reentry.online_filtering.v1")) "main must not default-follow old online evidence"
Assert-True (-not $main.Contains("StreamController")) "main must not create StreamController"
Assert-True (-not $main.Contains("rclcpp")) "main must not initialize ROS2 directly"

$nav = Read-Text (Join-Path $workbenchRoot "shell\NavRail.cpp")
foreach ($pageKey in @(
    "overview",
    "online",
    "runtime",
    "dataplane",
    "object",
    "models",
    "operators",
    "graph",
    "replay",
    "health",
    "config",
    "diagnostics"
)) {
    Assert-True ($nav.Contains($pageKey)) "navigation missing page key: $pageKey"
}
Assert-True ($nav.Contains("QStringLiteral(""online"")")) "navigation must expose live online run page"

$window = Read-Text (Join-Path $workbenchRoot "shell\TwinWorkbenchWindow.cpp")
foreach ($ctor in @(
    "OverviewPage",
    "OnlinePage",
    "ObjectPage",
    "ModelsPage",
    "OperatorLibraryPage",
    "GraphPage",
    "RuntimeHostPage",
    "DataPlanePage",
    "ReplayPage",
    "HealthPage",
    "ConfigPage",
    "DiagnosticsPage"
)) {
    Assert-True ($window.Contains($ctor)) "workbench window missing page: $ctor"
}
Assert-True ($window.Contains("PlatformRunController")) "workbench window must own platform run controller"
Assert-True ($window.Contains("startMainlineRequested")) "online page must be wired to backend launcher"
Assert-True ($window.Contains("loadObjectPackageDirectory")) "workbench must expose object package directory loading"
Assert-True ($window.Contains("loadObjectPackageFile")) "workbench must expose object file loading"
Assert-True ($window.Contains("rebuildPages")) "workbench must rebuild pages after object package changes"
foreach ($forbiddenCatalogCtor in @(
    "new ObjectPage(catalog_",
    "new ModelsPage(catalog_",
    "new OperatorLibraryPage(catalog_",
    "new GraphPage(catalog_",
    "new HealthPage(catalog_"
)) {
    Assert-True (-not $window.Contains($forbiddenCatalogCtor)) "R4 violation: object-package page still receives catalog: $forbiddenCatalogCtor"
}

$graphPage = Read-Text (Join-Path $workbenchRoot "pages\GraphPage.cpp")
Assert-True ($graphPage.Contains("PdkObjectPackageReader")) "GraphPage must load object package workflows"
Assert-True ($graphPage.Contains("WorkflowDagWidget")) "GraphPage must visualize workflow DAG from object package"
Assert-True ($graphPage.Contains("collectWorkflowPortEdges")) "GraphPage must expose workflow edge port mappings"
Assert-True ($graphPage.Contains("edgeTable_")) "GraphPage must show from.port_id -> to.port_id mappings"
Assert-True (-not $graphPage.Contains("GraphRuntimeControllerRunner")) "GraphPage must not mention old runner"
Assert-True (-not $graphPage.Contains("BlueprintGraphWidget")) "GraphPage must not use fixed blueprint widget"
Assert-True (-not $graphPage.Contains("CatalogSource")) "GraphPage must not depend on CatalogSource"

$objectPage = Read-Text (Join-Path $workbenchRoot "pages\ObjectPage.cpp")
foreach ($token in @(
    "PdkObjectPackageReader",
    "components",
    "assetGroups",
    "twinObjectJson",
    "Asset groups",
    "showResourceDetail",
    "showResourceUsageTable"
)) {
    Assert-True ($objectPage.Contains($token)) "ObjectPage must be object-package-native: $token"
}
Assert-True (-not $objectPage.Contains("CatalogSource")) "ObjectPage must not depend on CatalogSource"

$modelsPage = Read-Text (Join-Path $workbenchRoot "pages\ModelsPage.cpp")
Assert-True ($modelsPage.Contains("assets/resources.json")) "ModelsPage must expose object package asset groups"
Assert-True ($modelsPage.Contains("database / mesh / sensor / POD")) "ModelsPage must describe object resources, not only catalog models"
Assert-True ($modelsPage.Contains("operatorRefsUsingResource")) "ModelsPage must show which AtomicOperators use each resource"
Assert-True ($modelsPage.Contains("input_contract_ids")) "ModelsPage must show model/resource input contracts"
Assert-True ($modelsPage.Contains("outputContractsText")) "ModelsPage must show model/resource output contracts"
Assert-True (-not $modelsPage.Contains("CatalogSource")) "ModelsPage must not depend on CatalogSource"

$operatorPage = Read-Text (Join-Path $workbenchRoot "pages\OperatorLibraryPage.cpp")
Assert-True ($operatorPage.Contains("PdkObjectPackageReader")) "OperatorLibraryPage must load operators from object package"
Assert-True ($operatorPage.Contains("portsText(op.inputs)")) "OperatorLibraryPage must show operator input ports"
Assert-True ($operatorPage.Contains("portsText(op.outputs)")) "OperatorLibraryPage must show operator output ports"
Assert-True (-not $operatorPage.Contains("CatalogSource")) "OperatorLibraryPage must not depend on CatalogSource"

$healthPage = Read-Text (Join-Path $workbenchRoot "pages\HealthPage.cpp")
Assert-True ($healthPage.Contains("objectPackageRegions")) "HealthPage must derive regions from object package"
Assert-True (-not $healthPage.Contains("catalog->objects")) "HealthPage must not read catalog objects"

$configPage = Read-Text (Join-Path $workbenchRoot "pages\ConfigPage.cpp")
Assert-True ($configPage.Contains("objectPackage.ok()")) "ConfigPage must label object package as primary source"
Assert-True ($configPage.Contains("legacyRunCatalog->runs().size()")) "ConfigPage must limit legacy catalog to historical run index"
Assert-True (-not (Test-Path (Join-Path $workbenchRoot "datahub\CatalogSource.h"))) "CatalogSource.h must be retired"
Assert-True (-not (Test-Path (Join-Path $workbenchRoot "datahub\CatalogSource.cpp"))) "CatalogSource.cpp must be retired"
Assert-True (Test-Path (Join-Path $workbenchRoot "datahub\LegacyRunCatalogSource.h")) "LegacyRunCatalogSource.h must exist"
$legacyCatalogSource = Read-Text (Join-Path $workbenchRoot "datahub\LegacyRunCatalogSource.cpp")
Assert-True ($legacyCatalogSource.Contains("read_runs()")) "LegacyRunCatalogSource must read only run catalog"
foreach ($forbiddenLegacyRead in @("read_objects()", "read_models()", "read_bindings()")) {
    Assert-True (-not $legacyCatalogSource.Contains($forbiddenLegacyRead)) "LegacyRunCatalogSource must not expose object/model/binding truth source: $forbiddenLegacyRead"
}

$diagnostics = Read-Text (Join-Path $workbenchRoot "pages\DiagnosticsPage.cpp")
Assert-True ($diagnostics.Contains("state_transition")) "DiagnosticsPage must check operator families"
Assert-True ($diagnostics.Contains("runtime_node_snapshot.json")) "DiagnosticsPage must check Runtime Host evidence"
Assert-True (-not $diagnostics.Contains("GraphRuntimeControllerRunner")) "DiagnosticsPage must not mention old runner"

Write-Host "[OK] EnvTwinWorkbench platform-native skeleton smoke passed."
Write-Host "workspace=$WorkspaceRoot"
Write-Host "project=$projectPath"
