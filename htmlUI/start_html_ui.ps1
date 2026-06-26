param(
    [int]$Port = 5177
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Test-PortFree {
    param([int]$CandidatePort)
    $listener = $null
    try {
        $listener = [System.Net.Sockets.TcpListener]::new(
            [System.Net.IPAddress]::Parse('127.0.0.1'),
            $CandidatePort)
        $listener.Start()
        return $true
    }
    catch {
        return $false
    }
    finally {
        if ($listener) {
            $listener.Stop()
        }
    }
}

function Resolve-FreePort {
    param([int]$StartPort)
    for ($p = $StartPort; $p -lt ($StartPort + 50); ++$p) {
        if (Test-PortFree -CandidatePort $p) {
            return $p
        }
    }
    throw "No free localhost port found near $StartPort"
}

$python = Get-Command python -ErrorAction SilentlyContinue
if (-not $python) {
    $python = Get-Command py -ErrorAction SilentlyContinue
}
if (-not $python) {
    throw 'Python was not found. Install Python or run this page through any local static HTTP server.'
}

$resolvedPort = Resolve-FreePort -StartPort $Port
$url = "http://127.0.0.1:$resolvedPort/index.html"
$root = $PSScriptRoot

Write-Host "FlightEnv htmlUI local server"
Write-Host "  root = $root"
Write-Host "  url  = $url"
Write-Host ''

$server = $null
try {
    $server = Start-Process `
        -FilePath $python.Source `
        -ArgumentList @('-m', 'http.server', "$resolvedPort", '--bind', '127.0.0.1') `
        -WorkingDirectory $root `
        -WindowStyle Hidden `
        -PassThru

    Start-Sleep -Milliseconds 800
    Start-Process $url

    Write-Host "Browser opened. Keep this window open while using the prototype."
    Write-Host "Press Enter here to stop the local server."
    [void][Console]::ReadLine()
}
finally {
    if ($server -and -not $server.HasExited) {
        Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue
        Write-Host "Local server stopped."
    }
}
