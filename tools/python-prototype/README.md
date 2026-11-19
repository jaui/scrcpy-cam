# scrcpy-cam – Python-Prototyp

> **Status: Prototyp, abgelöst durch `scrcpy --capture-card` in diesem Fork.**
> Bekannte Mängel (geprüft am 19.11.2026 im Spiegelmodus): Auto-Auswahl nimmt das erste
> Nicht-0-Display (kann ein abgeschaltetes DeX-Display sein), Koordinaten berücksichtigen den schwarzen
> Rand bei hochkant gespiegeltem Bild nicht, Kamera-Index aus pygame (MSMF) kann vom DirectShow-Index abweichen.
>
> Pfad zu `adb.exe`/`scrcpy-server`: Umgebungsvariable `SCRCPY_DIR` (Standard: `scrcpy-win64-v5.0.1` neben dem Skript).

Zeigt das Bild der UGREEN 25772 (HDMI-Capture, Handy im DeX-Modus) an und steuert das Handy über den scrcpy-Server per ADB über WLAN.

- Bild: direkt von der Capture-Karte (1080p60, geringe Latenz)
- Steuerung: `scrcpy-server` 5.0.1 nur mit Steuerkanal (`video=false audio=false control=true`), Eingaben gehen an das DeX-Display

## Einmalig: ADB über WLAN koppeln

1. Handy: Einstellungen → Entwickleroptionen → **Wireless Debugging** aktivieren (PC und Handy im selben WLAN)
2. „Gerät mit Kopplungscode koppeln“ antippen → IP:Port und Code ablesen
3. `python scrcpy_cam.py --pair IP:KOPPLUNGSPORT CODE --connect IP:VERBINDUNGSPORT`
   (Verbindungsport = der Port, der oben auf der Wireless-Debugging-Seite steht, nicht der Kopplungsport)

## Starten

```
python scrcpy_cam.py              # findet Gerät (auch per mDNS), DeX-Display und UGREEN-Kamera automatisch
python scrcpy_cam.py -f           # Vollbild
python scrcpy_cam.py --no-control # nur Bild
```

Bedienung: Maus/Tastatur wie am Handy-Display. Rechtsklick = Kontextmenü, Mausrad = scrollen, F11 = Vollbild,
rechte Strg + H/B/S/P = Home/Zurück/App-Übersicht/Power, rechte Strg + Q = Beenden.
