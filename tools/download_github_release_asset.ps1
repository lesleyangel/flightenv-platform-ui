param(
    [Parameter(Mandatory = $true)]
    [string]$Repository,

    [Parameter(Mandatory = $true)]
    [string]$Tag,

    [Parameter(Mandatory = $true)]
    [string]$AssetName,

    [Parameter(Mandatory = $true)]
    [string]$OutputPath,

    [string]$Token = '',

    [switch]$SkipChecksum
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
    'User-Agent' = 'FlightEnvReleaseDownloader'
}

function Get-ReleaseAsset {
    param([Parameter(Mandatory = $true)][string]$Name)

    $release = Invoke-RestMethod `
        -Method GET `
        -Uri "https://api.github.com/repos/$Repository/releases/tags/$([System.Uri]::EscapeDataString($Tag))" `
        -Headers $apiHeaders
    foreach ($asset in @($release.assets)) {
        if ($asset.name -eq $Name) {
            return $asset
        }
    }
    throw "Release asset not found: $Repository $Tag $Name"
}

function Download-AssetById {
    param(
        [Parameter(Mandatory = $true)]$Asset,
        [Parameter(Mandatory = $true)][string]$PathValue
    )

    $dir = [System.IO.Path]::GetDirectoryName([System.IO.Path]::GetFullPath($PathValue))
    if (-not (Test-Path -LiteralPath $dir -PathType Container)) {
        New-Item -ItemType Directory -Path $dir -Force | Out-Null
    }

    $downloadHeaders = @{
        Authorization = "Bearer $Token"
        Accept = 'application/octet-stream'
        'X-GitHub-Api-Version' = '2022-11-28'
        'User-Agent' = 'FlightEnvReleaseDownloader'
    }

    Invoke-WebRequest `
        -Method GET `
        -Uri "https://api.github.com/repos/$Repository/releases/assets/$($Asset.id)" `
        -Headers $downloadHeaders `
        -OutFile $PathValue
}

$asset = Get-ReleaseAsset -Name $AssetName
Download-AssetById -Asset $asset -PathValue $OutputPath

if (-not $SkipChecksum) {
    $shaAsset = Get-ReleaseAsset -Name "$AssetName.sha256"
    $shaPath = "$OutputPath.sha256"
    Download-AssetById -Asset $shaAsset -PathValue $shaPath
    $expected = ((Get-Content -LiteralPath $shaPath -Raw) -split '\s+')[0].Trim().ToLowerInvariant()
    $actual = (Get-FileHash -LiteralPath $OutputPath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($expected -ne $actual) {
        throw "Checksum mismatch for $AssetName. expected=$expected actual=$actual"
    }
}

Write-Host "Downloaded release asset: $OutputPath"
