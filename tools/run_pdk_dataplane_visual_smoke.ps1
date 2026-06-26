param(
    [string]$WorkspaceRoot = "",
    [string]$RunDir = "",
    [string]$ObjectPackageRoot = ""
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($WorkspaceRoot)) {
    $WorkspaceRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
}
if ([string]::IsNullOrWhiteSpace($ObjectPackageRoot)) {
    $ObjectPackageRoot = Join-Path $WorkspaceRoot "flightenv-object-reentry-vehicle"
}
if ([string]::IsNullOrWhiteSpace($RunDir)) {
    $predictionRoot = Join-Path $WorkspaceRoot "_local_artifacts\platform-pdk\runtime-host-runs\reentry.posterior_future_prediction.v1"
    $candidate = Get-ChildItem -LiteralPath $predictionRoot -Directory |
        Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName "data_plane_manifest.json") } |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
    if ($null -eq $candidate) {
        throw "No prediction run with data_plane_manifest.json under $predictionRoot"
    }
    $RunDir = $candidate.FullName
}

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) {
        throw $Message
    }
}

function Read-Json {
    param([string]$Path)
    Assert-True (Test-Path -LiteralPath $Path -PathType Leaf) "missing JSON: $Path"
    return Get-Content -LiteralPath $Path -Raw -Encoding UTF8 | ConvertFrom-Json
}

$object = Read-Json (Join-Path $ObjectPackageRoot "object\twin_object.json")
$meshIds = @($object.resources |
    Where-Object { $_.resource_type -eq "mesh" } |
    ForEach-Object { [string]$_.resource_id })
Assert-True ($meshIds.Count -gt 0) "object mesh resources missing"

$manifestPath = Join-Path $RunDir "data_plane_manifest.json"
$manifest = Read-Json $manifestPath
Assert-True ($manifest.schema_version -eq "flightenv.platform.data_plane_manifest.v1") "bad data plane manifest schema"

$outputArtifacts = @($manifest.entries | Where-Object {
    $_.direction -eq "output" -and $_.representation -eq "artifact_ref"
})
Assert-True ($outputArtifacts.Count -gt 0) "no output artifact_ref entries"

$bad = @($outputArtifacts | Where-Object {
    [string]::IsNullOrWhiteSpace($_.artifact_uri) -or
    [int]$_.node_count -le 0 -or
    $null -eq $_.statistics -or
    $null -eq $_.time_point -or
    [string]::IsNullOrWhiteSpace($_.layout_ref) -or
    [string]::IsNullOrWhiteSpace($_.mesh_ref) -or
    [string]::IsNullOrWhiteSpace($_.component_id) -or
    [string]::IsNullOrWhiteSpace($_.unit)
})
if ($bad.Count -gt 0) {
    $bad | Select-Object -First 8 node_id, port_id, artifact_uri, node_count, layout_ref, mesh_ref, component_id, unit | Format-Table
    throw "some output artifact entries are missing UI visual metadata"
}

$badMeshRefs = @($outputArtifacts |
    ForEach-Object { [string]$_.mesh_ref } |
    Sort-Object -Unique |
    Where-Object { $meshIds -notcontains $_ })
Assert-True ($badMeshRefs.Count -eq 0) "manifest mesh_ref not declared by object resources: $($badMeshRefs -join ',')"

$first = $outputArtifacts[0]
$artifactPath = [string]$first.artifact_uri
if ($artifactPath.StartsWith("file://")) {
    $artifactPath = $artifactPath.Substring("file://".Length)
}
$artifact = Read-Json $artifactPath
$values = @($artifact.values)
Assert-True ($values.Count -eq [int]$artifact.node_count) "field artifact values count does not match node_count"
Assert-True (-not [string]::IsNullOrWhiteSpace($artifact.mesh_ref)) "field artifact mesh_ref missing"
Assert-True (-not [string]::IsNullOrWhiteSpace($artifact.component_id)) "field artifact component_id missing"
Assert-True (-not [string]::IsNullOrWhiteSpace($artifact.unit)) "field artifact unit missing"

Write-Host "[OK] PDK DataPlane visual metadata smoke passed."
Write-Host "run_dir=$RunDir"
Write-Host "output_artifact_entries=$($outputArtifacts.Count)"
Write-Host "mesh_refs=$(($outputArtifacts | ForEach-Object { $_.mesh_ref } | Sort-Object -Unique) -join ',')"
Write-Host "sample_artifact=$artifactPath"
Write-Host "sample_values=$($values.Count)"
