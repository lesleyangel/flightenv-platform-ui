param(
    [Parameter(Mandatory = $true)]
    [string]$Repository,

    [Parameter(Mandatory = $true)]
    [string]$Tag,

    [string]$ReleaseName = '',

    [string]$Body = '',

    [switch]$Prerelease,

    [switch]$Draft,

    [Parameter(Mandatory = $true)]
    [string[]]$AssetPath,

    [string]$Token = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($Token)) {
    $Token = $env:FLIGHTENV_ARTIFACT_TOKEN
}
if ([string]::IsNullOrWhiteSpace($Token)) {
    $Token = $env:GITHUB_TOKEN
}
if ([string]::IsNullOrWhiteSpace($Token)) {
    throw 'GitHub token is required. Set FLIGHTENV_ARTIFACT_TOKEN or pass -Token.'
}

if ($Repository -notmatch '^[^/]+/[^/]+$') {
    throw "Repository must be in owner/name form: $Repository"
}

$apiHeaders = @{
    Authorization = "Bearer $Token"
    Accept = 'application/vnd.github+json'
    'X-GitHub-Api-Version' = '2022-11-28'
    'User-Agent' = 'FlightEnvReleasePublisher'
}

function Invoke-GitHubJson {
    param(
        [Parameter(Mandatory = $true)][string]$Method,
        [Parameter(Mandatory = $true)][string]$Uri,
        $BodyObject = $null
    )

    $args = @{
        Method = $Method
        Uri = $Uri
        Headers = $apiHeaders
        ErrorAction = 'Stop'
    }
    if ($null -ne $BodyObject) {
        $args.ContentType = 'application/json'
        $args.Body = ($BodyObject | ConvertTo-Json -Depth 20)
    }
    return Invoke-RestMethod @args
}

function Get-ReleaseByTag {
    param([Parameter(Mandatory = $true)][string]$ReleaseTag)

    $uri = "https://api.github.com/repos/$Repository/releases/tags/$([System.Uri]::EscapeDataString($ReleaseTag))"
    try {
        return Invoke-GitHubJson -Method 'GET' -Uri $uri
    }
    catch {
        $status = $_.Exception.Response.StatusCode.value__
        if ($status -eq 404) {
            return $null
        }
        throw
    }
}

function Ensure-Release {
    $release = Get-ReleaseByTag -ReleaseTag $Tag
    if ($release) {
        return $release
    }

    $resolvedReleaseName = $ReleaseName
    if ([string]::IsNullOrWhiteSpace($resolvedReleaseName)) {
        $resolvedReleaseName = $Tag
    }
    $bodyObject = @{
        tag_name = $Tag
        name = $resolvedReleaseName
        body = $Body
        draft = [bool]$Draft
        prerelease = [bool]$Prerelease
    }
    return Invoke-GitHubJson -Method 'POST' -Uri "https://api.github.com/repos/$Repository/releases" -BodyObject $bodyObject
}

function New-Sha256File {
    param([Parameter(Mandatory = $true)][string]$PathValue)

    $hash = (Get-FileHash -LiteralPath $PathValue -Algorithm SHA256).Hash.ToLowerInvariant()
    $shaPath = "$PathValue.sha256"
    $name = [System.IO.Path]::GetFileName($PathValue)
    Set-Content -LiteralPath $shaPath -Encoding ASCII -Value "$hash  $name"
    return $shaPath
}

function Upload-Asset {
    param(
        [Parameter(Mandatory = $true)]$Release,
        [Parameter(Mandatory = $true)][string]$PathValue
    )

    $resolved = (Resolve-Path -LiteralPath $PathValue).Path
    $assetName = [System.IO.Path]::GetFileName($resolved)

    foreach ($asset in @($Release.assets)) {
        if ($asset.name -eq $assetName) {
            Invoke-GitHubJson -Method 'DELETE' -Uri "https://api.github.com/repos/$Repository/releases/assets/$($asset.id)" | Out-Null
            break
        }
    }

    $uploadUri = $Release.upload_url -replace '\{\?name,label\}', ''
    $uploadUri = "$uploadUri?name=$([System.Uri]::EscapeDataString($assetName))"
    $uploadHeaders = @{
        Authorization = "Bearer $Token"
        Accept = 'application/vnd.github+json'
        'X-GitHub-Api-Version' = '2022-11-28'
        'User-Agent' = 'FlightEnvReleasePublisher'
    }

    Write-Host "Uploading release asset: $assetName"
    Invoke-RestMethod `
        -Method POST `
        -Uri $uploadUri `
        -Headers $uploadHeaders `
        -ContentType 'application/octet-stream' `
        -InFile $resolved | Out-Null
}

$release = Ensure-Release
$assetsToUpload = [System.Collections.Generic.List[string]]::new()
foreach ($asset in $AssetPath) {
    if (-not (Test-Path -LiteralPath $asset -PathType Leaf)) {
        throw "Asset file not found: $asset"
    }
    $assetsToUpload.Add((Resolve-Path -LiteralPath $asset).Path) | Out-Null
    $assetsToUpload.Add((New-Sha256File -PathValue $asset)) | Out-Null
}

foreach ($asset in $assetsToUpload) {
    $release = Get-ReleaseByTag -ReleaseTag $Tag
    Upload-Asset -Release $release -PathValue $asset
}

Write-Host "Published assets to $Repository release $Tag"
