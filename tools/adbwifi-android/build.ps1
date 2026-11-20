<#
.SYNOPSIS
    Baut die App "WLAN-Debugging" (adbwifi) und installiert sie optional per adb.

.DESCRIPTION
    - Unit-Tests, Lint und Release-Build (Gradle-Wrapper, lokales Android-SDK, JDK 21)
    - signiert mit dem Release-Keystore, falls vorhanden (siehe README), sonst Debug-Build
    - legt die APK als adbwifi.apk in -OutDir ab
    - mit -Install: installieren, WRITE_SECURE_SETTINGS erteilen, von der Akku-Optimierung
      ausnehmen und die App einmal starten (registriert die Ausloeser und schaltet ein)
    - laeuft mit Windows PowerShell 5.1 und PowerShell 7

.PARAMETER Install
    Nach dem Bauen auf dem per adb verbundenen Handy installieren und einrichten.

.PARAMETER Serial
    adb-Seriennummer bzw. mDNS-Name, falls mehrere Geraete verbunden sind.

.PARAMETER KeystoreProperties
    Pfad zur keystore.properties (Standard: ..\..\..\keystore\adbwifi-keystore.properties,
    also ausserhalb des Repositorys).

.EXAMPLE
    .\build.ps1 -Install
#>
param(
    [switch]$Install,
    [string]$Serial = '',
    [string]$OutDir = (Join-Path $PSScriptRoot '..\..\..\dist'),
    [string]$KeystoreProperties = (Join-Path $PSScriptRoot '..\..\..\keystore\adbwifi-keystore.properties'),
    [string]$Adb = (Join-Path $PSScriptRoot '..\..\..\dist\scrcpy-cam-win64\adb.exe')
)

# Nicht 'Stop': unter Windows PowerShell 5.1 wird jede stderr-Zeile von adb/Gradle sonst
# zum abbrechenden Fehler. Exit-Codes werden einzeln geprueft.
$ErrorActionPreference = 'Continue'
$pkg = 'com.github.jaui.adbwifi'

# SDK-Pfad fuer Gradle (local.properties ist gitignored)
$localProps = Join-Path $PSScriptRoot 'local.properties'
if (-not (Test-Path $localProps)) {
    $sdk = if ($env:ANDROID_HOME) { $env:ANDROID_HOME } else { Join-Path $env:LOCALAPPDATA 'Android\Sdk' }
    if (-not (Test-Path $sdk)) { Write-Error "Android-SDK nicht gefunden ($sdk); ANDROID_HOME setzen."; exit 1 }
    "sdk.dir=$($sdk -replace '\\', '/' -replace ':', '\:')" | Set-Content -Encoding ASCII $localProps
}

$signed = Test-Path $KeystoreProperties
if ($signed) {
    $env:ADBWIFI_KEYSTORE_PROPERTIES = (Resolve-Path $KeystoreProperties).Path
    $task = 'assembleRelease'
    $apk = Join-Path $PSScriptRoot 'app\build\outputs\apk\release\app-release.apk'
} else {
    Write-Warning "Kein Release-Keystore ($KeystoreProperties) - baue Debug-APK."
    $task = 'assembleDebug'
    $apk = Join-Path $PSScriptRoot 'app\build\outputs\apk\debug\app-debug.apk'
}

Push-Location $PSScriptRoot
try {
    & .\gradlew.bat --no-daemon -q testDebugUnitTest lintRelease $task
    $rc = $LASTEXITCODE
} finally {
    Pop-Location
}
if ($rc -ne 0) { Write-Error "Gradle-Build fehlgeschlagen (Exit $rc)."; exit $rc }

New-Item -ItemType Directory -Force $OutDir | Out-Null
$dest = Join-Path (Resolve-Path $OutDir).Path 'adbwifi.apk'
Copy-Item -Force $apk $dest
Write-Host ("APK: {0} ({1:N0} KB{2})" -f $dest, ((Get-Item $dest).Length / 1KB), $(if ($signed) { ', signiert' } else { ', Debug' }))

if (-not $Install) { exit 0 }

if (-not (Test-Path $Adb)) { $Adb = 'adb' }
$sel = @()
if ($Serial) { $sel = @('-s', $Serial) }

function Invoke-Adb {
    param([string[]]$AdbArgs)
    $out = & $Adb @sel @AdbArgs 2>&1
    $out | ForEach-Object { Write-Host "  $_" }
    return $LASTEXITCODE
}

Write-Host 'Installiere ...'
if ((Invoke-Adb @('install', '-r', $dest)) -ne 0) { Write-Error 'Installation fehlgeschlagen.'; exit 1 }
Write-Host 'Erteile WRITE_SECURE_SETTINGS ...'
Invoke-Adb @('shell', 'pm', 'grant', $pkg, 'android.permission.WRITE_SECURE_SETTINGS') | Out-Null
Write-Host 'Nehme von der Akku-Optimierung aus ...'
Invoke-Adb @('shell', 'cmd', 'deviceidle', 'whitelist', "+$pkg") | Out-Null
Write-Host 'Starte die App einmal (registriert die Ausloeser) ...'
Invoke-Adb @('shell', 'am', 'start', '-n', "$pkg/.MainActivity") | Out-Null
Write-Host 'Fertig. Kachel am Handy hinzufuegen: Schnelleinstellungen -> Bearbeiten -> "WLAN-Debugging".'
