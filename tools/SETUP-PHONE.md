# Handy einrichten (einmalig)

Ziel: Das Handy bleibt ohne Eingriff am Gerät per WLAN-ADB steuerbar.
Die Menünamen gelten für Samsung One UI; getestete Geräte: [Kompatibilitätsliste](../README.md#kompatibilität).

> Stand 19.11.2026. Abschnitt 5 (Automatik) ist **optional** – Standard ist, Wireless Debugging bei
> Bedarf von Hand wieder einzuschalten. Punkte mit ⚠ sind ungeprüft.
> Gesamtdokumentation: [`doc/scrcpy-cam.md`](../doc/scrcpy-cam.md).
> PC-Seite für den Dauerbetrieb: `tools/scrcpy-cam.ps1` (verbindet selbst neu).

## 1. Entwickleroptionen

Einstellungen → Telefoninfo → Softwareinformationen → 7× auf **Buildnummer** tippen.
Dann Einstellungen → Entwickleroptionen:

- [ ] **Wireless Debugging** an. Beim ersten Mal „In diesem Netzwerk immer zulassen“ bestätigen.
- [ ] **Zeitüberschreitung der ADB-Autorisierung deaktivieren** an
      (sonst verfällt die Kopplung nach 7 Tagen ohne Verbindung).
- [ ] **Aktiv lassen** (Bildschirm beim Laden nicht ausschalten) an.

## 2. Samsung-Einstellungen

- [ ] Sicherheit und Datenschutz → **Automatische Sperre** (Auto Blocker) **aus** – blockiert sonst ADB-Befehle.
- [ ] Akku und Gerätewartung → Automatisierung → **Automatisch neu starten aus** und **Auto-Optimierung täglich aus**.
- [ ] Software-Update → **Automatisch über WLAN herunterladen aus**, Updates gezielt von Hand installieren.
- [ ] Android 16: **Erweiterter Schutz** (Advanced Protection) **nicht** aktivieren.

Hintergrund: Nach jedem Neustart ist das Handy bis zur ersten PIN-Eingabe gesperrt und Wireless Debugging ist aus.
Das lässt sich nur am Gerät beheben. Neustarts also vermeiden.

## 3. WLAN

- [ ] WLAN → Netzwerk → Erweitert → MAC-Adresstyp: **Telefon-MAC**.
- [ ] Im Router für das Handy eine **feste IP** (DHCP-Reservierung) einrichten.
- [ ] WLAN → Intelligentes WLAN → **Zu mobilen Daten wechseln aus**.
- [ ] Kein Gäste-WLAN und keine Client-Isolation: Der PC findet das Handy per mDNS (Multicast).
- [ ] ⚠ Mesh/Repeater: Falls Android die Freigabe „Wireless Debugging in diesem Netzwerk“ pro Access Point
      speichert, muss das Handy an jedem AP einmal bestätigt werden (oder fest an einem AP bleiben).

## 4. Koppeln mit dem PC

Am PC im Ordner mit `adb.exe`:

```
adb pair <IP>:<Kopplungsport> <Code>      # Werte unter Wireless Debugging → Gerät mit Kopplungscode koppeln
adb mdns services                         # zeigt _adb-tls-connect mit dem aktuellen Port
adb devices                               # Handy muss als "device" erscheinen
```

Der Port ändert sich bei jedem Einschalten, das ist egal: adb findet das gekoppelte Handy per mDNS selbst.

## 5. Wireless Debugging wieder einschalten

Wireless Debugging geht nach Neustart und bei WLAN-Verlust aus.

**Standard:** bei Bedarf am Handy von Hand wieder einschalten
(Entwickleroptionen → Debugging über WLAN). Neu koppeln ist nicht nötig, der PC verbindet sich selbst.
Bebilderte Anleitung (HTML/PDF): [`doc/anleitung-wlan-debugging/`](../doc/anleitung-wlan-debugging/wlan-debugging.adoc).

**Optional (ungetestet):** Eine Automations-App schaltet es selbst wieder ein.

1. **MacroDroid** (oder Tasker) aus dem Play Store installieren.
2. Am PC einmalig das Recht geben (Paketname MacroDroid: `com.arlosoft.macrodroid`):
   ```
   adb shell pm grant com.arlosoft.macrodroid android.permission.WRITE_SECURE_SETTINGS
   ```
3. Makro anlegen:
   - Auslöser: **Gerätestart**, **Bildschirm entsperrt**, **WLAN verbunden (<eigene SSID>)**,
     **Regelmäßig alle 5 Minuten** (Apps laufen nach einem Neustart erst nach dem ersten Entsperren)
   - Aktion: **Systemeinstellung ändern** → Typ *Global* → `adb_wifi_enabled` = `1`
     (zusätzlich `adb_enabled` = `1`)
4. Akku → Hintergrundnutzungslimits: MacroDroid unter **Nie in den Standby versetzen** eintragen.
5. Test: Wireless Debugging von Hand ausschalten → nach spätestens 5 Minuten wieder an, PC verbindet sich selbst.

## 6. Sicherheit

- Mit dauerhaft aktivem Wireless Debugging ist der adb-Schlüssel des PCs (`%USERPROFILE%\.android\adbkey`)
  ein Generalschlüssel für das Handy. Windows-Konto mit Passwort schützen, Schlüssel nicht kopieren.
- Kopplungen anderer Rechner unter Wireless Debugging → Gekoppelte Geräte entfernen.

## 7. Bekannte Grenze: Neustart

Nach einem Neustart muss einmal die PIN eingegeben werden. Erst danach laufen MacroDroid und
Wireless Debugging. Ohne Eingriff am Gerät geht das mit einer Bluetooth-Tastatur-Emulation per ESP32
([`tools/bt-hid`](bt-hid)): Sie verbindet sich schon vor dem ersten Entsperren und kann die PIN
eingeben. Dafür einmalig koppeln (siehe dort). Wireless Debugging danach von Hand oder per
Automations-App (Abschnitt 5) einschalten.
