<#
.SYNOPSIS
    Startet scrcpy-cam (--capture-card) und haelt die Verbindung ohne Eingriff am Handy.

.DESCRIPTION
    - findet das gekoppelte Handy ueber adb/mDNS (der Wireless-Debugging-Port darf sich aendern)
    - startet scrcpy mit --capture-card
    - beendet sich scrcpy wegen Verbindungsverlust oder Fehler, wird neu verbunden und
      neu gestartet (alle 2 s); scheitert der Start mehrmals hintereinander sofort (z. B.
      falsche Argumente), werden die Abstaende bis auf 60 s verlaengert
    - wird DeX ein- oder ausgeschaltet (scrcpy endet mit Code 3), startet scrcpy sofort neu
      und steuert dann das DeX- bzw. Handy-Display
    - wird das Fenster geschlossen (scrcpy endet normal), endet auch der Launcher
    - laeuft mit Windows PowerShell 5.1 und PowerShell 7

.PARAMETER ScrcpyDir
    Ordner mit scrcpy.exe, adb.exe und scrcpy-server. Standard: der Ordner dieses Skripts,
    falls dort scrcpy.exe liegt (Ausgabeordner), sonst ..\..\dist\scrcpy-cam-win64.

.PARAMETER Serial
    Optional: Teil der adb-Seriennummer bzw. des mDNS-Namens (z. B. "R5CX..." aus "adb devices"), falls
    mehrere Geraete gekoppelt sind.

.PARAMETER ScrcpyArgs
    Zusaetzliche scrcpy-Argumente, z. B. @('--fullscreen', '--display-id=0').

.EXAMPLE
    .\scrcpy-cam.ps1 -ScrcpyDir C:\dev\scrcpy-cam\dist\scrcpy-cam-win64 -ScrcpyArgs '--fullscreen'
#>
param(
    # Standard: neben diesem Skript (Ausgabeordner), sonst ..\..\dist\scrcpy-cam-win64
    [string]$ScrcpyDir = '',
    [string]$Serial = '',
    [string[]]$ScrcpyArgs = @(),
    [int]$RetrySeconds = 2,
    [int]$MaxRetrySeconds = 60
)

# Nicht 'Stop': unter Windows PowerShell 5.1 wird jede umgeleitete stderr-Zeile von adb
# (z. B. "* daemon not running; starting now") sonst zum abbrechenden Fehler.
$ErrorActionPreference = 'Continue'
if (-not $ScrcpyDir) {
    $ScrcpyDir = if (Test-Path (Join-Path $PSScriptRoot 'scrcpy.exe')) { $PSScriptRoot }
                 else { Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) 'dist\scrcpy-cam-win64' }
}
$ScrcpyDir = (Resolve-Path -LiteralPath $ScrcpyDir -ErrorAction Stop).Path
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
            $devs += $parts[0]
        }
    }
    return $devs
}

function Connect-Device {
    $devs = @(Get-ReadyDevices | Where-Object { -not $Serial -or $_ -like "*$Serial*" })
    if ($devs.Count -ge 1) { return $devs[0] }

    # Gekoppelte Geraete per mDNS suchen und verbinden (aktueller Port)
    $services = & $adb mdns services 2>$null
    $addresses = @()
    foreach ($line in $services) {
        if ($line -match '^(\S+)\s+_adb-tls-connect\._tcp\.?\s+(\S+:\d+)') {
            $name = $Matches[1]; $addr = $Matches[2]
            if ($Serial -and $name -notlike "*$Serial*" -and $addr -notlike "*$Serial*") { continue }
            $addresses += $addr
            & $adb connect $addr 2>&1 | Out-Null
        }
    }
    # Nach 'adb connect' kann adb das Geraet als IP:Port statt unter dem mDNS-Namen fuehren.
    $devs = @(Get-ReadyDevices | Where-Object {
        -not $Serial -or $_ -like "*$Serial*" -or $addresses -contains $_
    })
    if ($devs.Count -ge 1) { return $devs[0] }
    return $null
}

& $adb start-server 2>&1 | Out-Null
$waitingLogged = $false
$quickFails = 0          # Starts hintereinander, die nach weniger als 15 s mit Fehler endeten

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
    # Direkter Aufruf statt Start-Process -ArgumentList: PowerShell setzt Argumente mit
    # Leerzeichen (z. B. --capture-card="USB Video") korrekt in Anfuehrungszeichen.
    $started = Get-Date
    Push-Location $ScrcpyDir
    try {
        & $scrcpy @scrcpyArgList
        $code = $LASTEXITCODE
    } finally {
        Pop-Location
    }
    $ranSeconds = ((Get-Date) - $started).TotalSeconds

    if ($code -eq 0) {
        Write-Log 'scrcpy wurde beendet (Fenster geschlossen) - Launcher endet.'
        break
    }

    if ($code -eq 3) {
        # DeX ein-/ausgeschaltet: scrcpy waehlt das zu steuernde Display beim Start
        Write-Log 'Anzeige gewechselt (DeX ein/aus) - starte scrcpy sofort neu.'
        $quickFails = 0
        continue
    }

    if ($ranSeconds -lt 15) { $quickFails++ } else { $quickFails = 0 }
    $delay = $RetrySeconds
    if ($quickFails -ge 3) {
        $delay = [math]::Min($MaxRetrySeconds, $RetrySeconds * [math]::Pow(2, $quickFails - 2))
        if ($quickFails -eq 3) {
            Write-Log 'scrcpy startet wiederholt nicht - Argumente und Capture-Karte pruefen (Ausgabe oben).'
        }
    }
    Write-Log ("scrcpy beendet mit Code {0} (1 = Fehler, 2 = Verbindung verloren) - neuer Versuch in {1} s ..." -f $code, $delay)
    Start-Sleep -Seconds $delay
}
