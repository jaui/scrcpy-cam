<#
.SYNOPSIS
    Startet scrcpy-cam (--capture-card) und haelt die Verbindung ohne Eingriff am Handy.

.DESCRIPTION
    - findet das gekoppelte Handy ueber adb/mDNS (der Wireless-Debugging-Port darf sich aendern)
    - startet scrcpy mit --capture-card
    - beendet sich scrcpy wegen Verbindungsverlust oder Startfehler, wird neu verbunden und
      neu gestartet (alle 2 s)
    - wird das Fenster geschlossen (scrcpy endet normal), endet auch der Launcher

.PARAMETER ScrcpyDir
    Ordner mit scrcpy.exe, adb.exe und scrcpy-server.

.PARAMETER Serial
    Optional: Teil der adb-Seriennummer bzw. des mDNS-Namens (z. B. "R5CX..." aus "adb devices"), falls
    mehrere Geraete gekoppelt sind.

.PARAMETER ScrcpyArgs
    Zusaetzliche scrcpy-Argumente, z. B. @('--fullscreen', '--display-id=0').

.EXAMPLE
    .\scrcpy-cam.ps1 -ScrcpyDir C:\dev\scrcpy-cam\dist\scrcpy-cam-win64 -ScrcpyArgs '--fullscreen'
#>
param(
    [string]$ScrcpyDir = (Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) 'dist\scrcpy-cam-win64'),
    [string]$Serial = '',
    [string[]]$ScrcpyArgs = @(),
    [int]$RetrySeconds = 2
)

$ErrorActionPreference = 'Stop'
$adb = Join-Path $ScrcpyDir 'adb.exe'
$scrcpy = Join-Path $ScrcpyDir 'scrcpy.exe'
foreach ($f in $adb, $scrcpy) {
    if (-not (Test-Path $f)) { throw "Nicht gefunden: $f" }
}

function Write-Log([string]$msg) {
    Write-Host ("[{0:HH:mm:ss}] {1}" -f (Get-Date), $msg)
}

function Get-ReadyDevices {
    $out = & $adb devices 2>$null
    $devs = @()
    foreach ($line in $out | Select-Object -Skip 1) {
        $parts = $line -split '\s+'
        if ($parts.Count -ge 2 -and $parts[1] -eq 'device') {
            if (-not $Serial -or $parts[0] -like "*$Serial*") { $devs += $parts[0] }
        }
    }
    return ,$devs
}

function Connect-Device {
    $devs = Get-ReadyDevices
    if ($devs.Count -ge 1) { return $devs[0] }

    # Gekoppelte Geraete per mDNS suchen und verbinden (aktueller Port)
    $services = & $adb mdns services 2>$null
    foreach ($line in $services) {
        if ($line -match '^(\S+)\s+_adb-tls-connect\._tcp\.?\s+(\S+:\d+)') {
            $name = $Matches[1]; $addr = $Matches[2]
            if ($Serial -and $name -notlike "*$Serial*") { continue }
            & $adb connect $addr 2>&1 | Out-Null
        }
    }
    $devs = Get-ReadyDevices
    if ($devs.Count -ge 1) { return $devs[0] }
    return $null
}

& $adb start-server 2>&1 | Out-Null
$waitingLogged = $false

while ($true) {
    $device = Connect-Device
    if (-not $device) {
        if (-not $waitingLogged) {
            Write-Log "Warte auf das Handy (Wireless Debugging an? gleiches WLAN?) ..."
            $waitingLogged = $true
        }
        Start-Sleep -Seconds $RetrySeconds
        continue
    }
    $waitingLogged = $false

    Write-Log "Starte scrcpy fuer $device"
    $scrcpyArgList = @("--serial=$device", '--capture-card') + $ScrcpyArgs
    $proc = Start-Process -FilePath $scrcpy -ArgumentList $scrcpyArgList -WorkingDirectory $ScrcpyDir `
                          -NoNewWindow -PassThru
    $proc.WaitForExit()
    $code = $proc.ExitCode

    if ($code -eq 0) {
        Write-Log 'scrcpy wurde beendet (Fenster geschlossen) - Launcher endet.'
        break
    }

    Write-Log "scrcpy beendet mit Code $code (1 = Fehler, 2 = Verbindung verloren) - verbinde neu ..."
    Start-Sleep -Seconds $RetrySeconds
}
