<#
.SYNOPSIS
    Download the latest goonstein build into .\goonstein.

.DESCRIPTION
    irm https://raw.githubusercontent.com/Yugendren/goonstein/main/get.ps1 | iex

    Re-running updates in place: the binary and assets\ are replaced, anything else you left in
    .\goonstein (logs, screenshots, saves) is kept. No GitHub account and no build toolchain
    needed.
#>

$ErrorActionPreference = 'Stop'

$Repo  = 'Yugendren/goonstein'
$Tag   = 'latest'
$Dest  = 'goonstein'
$Asset = 'goonstein-windows-x86_64.zip'

# Only x86-64 is built. Windows on ARM runs it through the x64 emulator, so allow it with a note.
$archEnv = $env:PROCESSOR_ARCHITECTURE
if ($archEnv -eq 'ARM64') {
    Write-Host "get.ps1: no native ARM64 build — using the x86-64 one under emulation"
} elseif ($archEnv -ne 'AMD64') {
    throw "get.ps1: unsupported architecture '$archEnv'; build from source (see README.md)"
}

$url = "https://github.com/$Repo/releases/download/$Tag/$Asset"
$tmp = Join-Path ([System.IO.Path]::GetTempPath()) ("goonstein-" + [System.Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $tmp -Force | Out-Null

try {
    Write-Host "get.ps1: downloading $Asset"
    $zip = Join-Path $tmp $Asset
    # The progress bar makes Invoke-WebRequest crawl on large files.
    $oldProgress = $ProgressPreference
    $ProgressPreference = 'SilentlyContinue'
    try {
        Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing
    } finally {
        $ProgressPreference = $oldProgress
    }

    $unpacked = Join-Path $tmp 'x'
    Expand-Archive -Path $zip -DestinationPath $unpacked -Force

    $src = Join-Path $unpacked 'goonstein'
    if (-not (Test-Path (Join-Path $src 'goonstein.exe'))) {
        throw "get.ps1: unexpected archive layout in $Asset"
    }

    # Replace only what we ship, so a re-run is an update rather than a wipe.
    if (-not (Test-Path $Dest)) { New-Item -ItemType Directory -Path $Dest -Force | Out-Null }
    Remove-Item -Recurse -Force (Join-Path $Dest 'assets')        -ErrorAction SilentlyContinue
    Remove-Item -Force         (Join-Path $Dest 'goonstein.exe')  -ErrorAction SilentlyContinue
    Copy-Item -Path (Join-Path $src '*') -Destination $Dest -Recurse -Force

    # Clear the "downloaded from the internet" mark so SmartScreen does not block the exe.
    Get-ChildItem -Path $Dest -Recurse -File | Unblock-File -ErrorAction SilentlyContinue
} finally {
    Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
}

Write-Host "get.ps1: installed into .\$Dest"
Write-Host "get.ps1: run it with   .\$Dest\goonstein.exe"
