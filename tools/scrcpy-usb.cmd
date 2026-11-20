@echo off
rem scrcpy-cam - Notbetrieb ohne Capture-Karte (z. B. Karte oder Hub defekt)
rem
rem Startet normales scrcpy (Videostrom vom Handy) ueber das USB-Kabel.
rem Voraussetzungen: Handy per USB direkt am PC (statt am HDMI-Hub),
rem am Handy "USB-Debugging" an. Apps mit FLAG_SECURE (z. B. Banking,
rem Authenticator) bleiben in diesem Modus schwarz.
rem
rem Weitere scrcpy-Argumente werden durchgereicht, z. B.:
rem   scrcpy-usb.cmd --fullscreen --no-audio

setlocal
rem scrcpy.exe neben diesem Skript (Ausgabeordner) oder im Build-Ordner
set "DIR=%~dp0"
if not exist "%DIR%scrcpy.exe" set "DIR=%~dp0..\..\dist\scrcpy-cam-win64\"
if not "%SCRCPY_DIR%"=="" set "DIR=%SCRCPY_DIR%\"
if not exist "%DIR%scrcpy.exe" (
    echo scrcpy.exe nicht gefunden: %DIR%
    pause
    exit /b 1
)

cd /d "%DIR%"
rem -d: das per USB angeschlossene Geraet (auch wenn es zusaetzlich per WLAN verbunden ist)
scrcpy.exe -d %*
if errorlevel 1 (
    echo.
    echo scrcpy ist mit Fehler beendet. Pruefen: USB-Kabel, USB-Debugging am Handy,
    echo Freigabe "USB-Debugging zulassen?" am Handy bestaetigt.
    pause
)
