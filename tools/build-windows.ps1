<#
.SYNOPSIS
    Baut den scrcpy-cam Windows-Client (win64) in einem Docker-Container.

.DESCRIPTION
    - startet Docker Desktop, falls noetig
    - baut das Image tools/docker (Ubuntu + mingw-w64)
    - kompiliert im Container mit release/build_windows.sh 64
    - Drittbibliotheken (FFmpeg, SDL3, dav1d, libusb, adb) werden im Volume
      "scrcpy-cam-deps" gecacht; nur der erste Build dauert lange (~20-40 min)
    - legt das Ergebnis nach <OutDir>\scrcpy-cam-win64 und kopiert den
      unveraenderten scrcpy-server 5.0.1 dazu

.EXAMPLE
    .\tools\build-windows.ps1
    .\tools\build-windows.ps1 -OutDir D:\scrcpy-cam -NoCache
#>
param(
    [string]$OutDir = (Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) 'dist'),
    [string]$ServerJar = (Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) 'scrcpy-win64-v5.0.1\scrcpy-server'),
    [switch]$NoCache
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$image = 'scrcpy-cam-build'
$volume = 'scrcpy-cam-deps'

function Wait-Docker {
    docker info *> $null
    if ($LASTEXITCODE -eq 0) { return }
    $desktop = Join-Path $env:ProgramFiles 'Docker\Docker\Docker Desktop.exe'
    if (-not (Test-Path $desktop)) { throw "Docker laeuft nicht und Docker Desktop wurde nicht gefunden." }
    Write-Host 'Starte Docker Desktop ...'
    Start-Process $desktop
    $deadline = (Get-Date).AddMinutes(3)
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Seconds 3
        docker info *> $null
        if ($LASTEXITCODE -eq 0) { return }
    }
    throw 'Docker ist nach 3 Minuten nicht bereit.'
}

Wait-Docker

if ($NoCache) {
    docker volume rm $volume 2>$null | Out-Null
}

Write-Host "Baue Image $image ..."
docker build -t $image (Join-Path $repo 'tools\docker')
if ($LASTEXITCODE -ne 0) { throw 'docker build fehlgeschlagen' }

New-Item -ItemType Directory -Force $OutDir | Out-Null
Write-Host "Kompiliere (Ausgabe nach $OutDir) ..."
docker run --rm `
    -v "${repo}:/src:ro" `
    -v "${volume}:/deps" `
    -v "${OutDir}:/out" `
    $image
if ($LASTEXITCODE -ne 0) { throw 'Build im Container fehlgeschlagen' }

$dist = Join-Path $OutDir 'scrcpy-cam-win64'
if (Test-Path $ServerJar) {
    Copy-Item $ServerJar (Join-Path $dist 'scrcpy-server') -Force
} else {
    Write-Warning "scrcpy-server nicht gefunden ($ServerJar) - bitte aus dem offiziellen 5.0.1-Release danebenlegen."
}
Write-Host "Fertig: $dist"
