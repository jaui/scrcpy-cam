# scrcpy-cam – Dokumentation

Inoffizieller Fork von [scrcpy](https://github.com/Genymobile/scrcpy) 5.0.1.
Repository: <https://github.com/jaui/scrcpy-cam>

Stand: 19.11.2026

---

## 1. Ziel

Ein Android-Handy vollständig vom Windows-PC aus mit Maus und Tastatur bedienen –
**auch Apps mit `FLAG_SECURE`** (z. B. Banking- oder Authenticator-Apps).

Normales scrcpy zeigt solche Apps schwarz, weil Android sichere Fenster aus der
Bildschirmaufnahme ausblendet. Der HDMI-Ausgang des Handys zeigt sie dagegen an (das
HDMI-Display ist als „sicheres Display“ gekennzeichnet). scrcpy-cam nimmt deshalb das **Bild
von einer HDMI-Capture-Karte** und nutzt scrcpy **nur für die Steuerung**.

Der Betrieb soll möglichst ohne Eingriff am Handy weiterlaufen
(siehe [Abschnitt 8](#8-wlan-adb-ohne-eingriff-am-handy)).

Welche Handys getestet sind, steht in der Kompatibilitätsliste im [README](../README.md#kompatibilität).

## 2. Aufbau

```
 Handy (Android)                                   Windows-PC
 ┌──────────────┐  USB-C   ┌─────────┐  HDMI  ┌──────────────┐  USB  ┌──────────────────────────┐
 │ Display 0    ├─────────►│ USB-C-  ├───────►│ UGREEN 25772 ├──────►│ scrcpy --capture-card    │
 │ (gespiegelt) │  (DP Alt)│ Hub +PD │        │ HDMI-Capture │ (UVC) │  ├─ Bild: SDL-Kamera     │
 │              │◄─────────┤ Laden   │        └──────────────┘       │  │   (NV12, 1080p60)     │
 │              │          └─────────┘                               │  └─ Fenster (Maus/Tast.) │
 │ scrcpy-server│◄─────────────── WLAN: adb (Wireless Debugging) ───┤     │                    │
 │ video=false  │                 Steuerbefehle (Touch, Tasten)      │◄────┘                    │
 └──────────────┘                                                    └──────────────────────────┘
```

- **Bild:** Handy → HDMI → Capture-Karte → PC. Kein Videostream vom Handy.
- **Steuerung:** scrcpy-Server auf dem Handy, gestartet mit `video=false control=true`,
  Verbindung über adb per WLAN. Der USB-C-Anschluss des Handys ist durch HDMI belegt
  (eine Buchse kann nicht gleichzeitig USB-Host für den Hub und USB-Gerät für den PC sein).
- **Server unverändert:** Nur der Client (PC) ist geändert. Der offizielle `scrcpy-server`
  5.0.1 wird weiterverwendet.

## 3. Voraussetzungen

| Komponente | Anforderung | Hinweis |
|---|---|---|
| Handy | Android mit Bildausgabe über USB-C (DisplayPort-Alt-Mode), Wireless Debugging (Android 11+) | Getestete Geräte: [Kompatibilitätsliste](../README.md#kompatibilität) |
| Capture-Karte | UGREEN 25772 (getestet), andere UVC-Karten mit NV12 wahrscheinlich | Liefert unter Windows (Media Foundation) NV12/YUY2/MJPG, 1920×1080 mit 60 fps |
| Hub | USB-C mit HDMI und Ladeanschluss (PD) | |
| PC | Windows 11, x64 | Docker Desktop nur zum Bauen |
| Netzwerk | Handy und PC im selben WLAN, Multicast erlaubt | mDNS für die Gerätesuche |

## 4. Schnellstart

1. Handy einmalig einrichten: [`tools/SETUP-PHONE.md`](../tools/SETUP-PHONE.md)
   (Entwickleroptionen, Wireless Debugging, Koppeln, Automatik).
2. Handy über den Hub per HDMI an die Capture-Karte, Karte per USB an den PC.
3. Starten:

```powershell
# Dauerbetrieb mit automatischem Neuverbinden (empfohlen)
.\tools\scrcpy-cam.ps1 -ScrcpyArgs '--fullscreen'

# oder direkt
cd C:\dev\scrcpy-cam\dist\scrcpy-cam-win64
.\scrcpy.exe --capture-card
```

Das Fenster zeigt nur den Handy-Inhalt, ohne schwarze Ränder. Maus und Tastatur steuern das Handy.

## 5. Optionen

Neue Optionen (alle übrigen scrcpy-Optionen gelten weiter, siehe `scrcpy --help`):

| Option | Standard | Bedeutung |
|---|---|---|
| `--capture-card[=NAME]` | `UGREEN` | Bild von der Kamera, deren Name `NAME` enthält (Groß-/Kleinschreibung egal). Der Server läuft ohne Video. |
| `--capture-card-size=BxH` | `1920x1080` | Angeforderte Auflösung der Karte (gerade Werte). |
| `--capture-card-fps=N` | `60` | Angeforderte Bildrate. |
| `--capture-card-crop=B:H:X:Y` | automatisch | Fester Bildausschnitt statt automatischer Randerkennung. |
| `--display-id=N` | `0` | Welches Android-Display gesteuert wird (scrcpy-Standardoption). Im Spiegelmodus `0`. |

Nicht kombinierbar mit `--capture-card` (Fehlermeldung beim Start): `--no-window`,
`--no-control`, `--otg`, `--video-source=camera`, `--record`, `--new-display`,
`--flex-display`, V4L2, `--hwdec=<erzwungen>`.

Beispiele:

```powershell
scrcpy --capture-card                          # UGREEN, Display 0
scrcpy --capture-card="USB Video"              # andere Karte
scrcpy --capture-card --display-id=2           # z. B. DeX-Display
scrcpy --capture-card --capture-card-crop=1920:1080:0:0   # ganzer Frame (z. B. DeX)
scrcpy --capture-card --print-fps              # Bildrate im Terminal anzeigen
```

## 6. Bedienung

### Maus

| Aktion am PC | Wirkung am Handy |
|---|---|
| Linksklick | Tippen |
| Linke Taste halten und ziehen | Wischen (wird als Finger eingespeist, auch Wisch vom oberen Rand) |
| Rechtsklick | Zurück |
| Mittelklick | Home |
| Maustaste 4 | App-Übersicht |
| Maustaste 5 | Benachrichtigungen |
| Mausrad | Scrollen |
| Doppelklick links auf schwarzen Rand | Fenster an Inhalt anpassen |

### Tastenkürzel

`MOD` ist standardmäßig die linke <kbd>Alt</kbd>- oder die linke <kbd>Win</kbd>-Taste
(änderbar mit `--shortcut-mod`). Die Kürzel lassen sich auch von Bildschirmtastaturen oder
Makro-Tools auslösen:

| Kürzel | Wirkung |
|---|---|
| MOD+<kbd>b</kbd> | Zurück |
| MOD+<kbd>h</kbd> | Home |
| MOD+<kbd>s</kbd> | App-Übersicht |
| MOD+<kbd>n</kbd> | Benachrichtigungen öffnen (zweimal: Schnelleinstellungen) |
| MOD+Shift+<kbd>n</kbd> | Benachrichtigungen schließen |
| MOD+<kbd>↑</kbd> / <kbd>↓</kbd> | Lauter / Leiser |
| MOD+<kbd>p</kbd> | Ein/Aus-Taste |
| MOD+<kbd>r</kbd> | Handy drehen (nicht getestet mit `--capture-card`) |
| MOD+<kbd>v</kbd> | PC-Zwischenablage einfügen |
| MOD+<kbd>f</kbd> / <kbd>F11</kbd> | Vollbild |
| MOD+<kbd>q</kbd> | Beenden |

Text wird normal getippt.

### Nicht verwenden

- **`--turn-screen-off` und MOD+<kbd>o</kbd>:** Im Spiegelmodus wird damit auch das
  HDMI-Bild schwarz (getestet). Nach dem Beenden von scrcpy schaltet der Server das Display
  wieder ein; sonst MOD+Shift+<kbd>o</kbd>.
- MOD+Shift+<kbd>r</kbd> (Video zurücksetzen) hat ohne Videostream keine Wirkung.

## 7. Dauerbetrieb: Launcher

[`tools/scrcpy-cam.ps1`](../tools/scrcpy-cam.ps1)

```powershell
.\tools\scrcpy-cam.ps1 [-ScrcpyDir <Ordner>] [-Serial <Teil der Seriennummer>] `
                       [-ScrcpyArgs '--fullscreen','--no-audio'] [-RetrySeconds 2]
```

- Sucht das gekoppelte Handy über `adb devices` und `adb mdns services`
  (der Port von Wireless Debugging darf sich ändern).
- Startet scrcpy mit `--serial=<Gerät> --capture-card` und den zusätzlichen Argumenten.
- Endet scrcpy mit Code 1 (Startfehler) oder 2 (Verbindung verloren), wird nach
  `RetrySeconds` neu verbunden und neu gestartet.
- Endet scrcpy normal (Code 0, Fenster geschlossen), endet auch der Launcher.
- Standard für `-ScrcpyDir`: `..\..\dist\scrcpy-cam-win64` relativ zum Skript, also
  `C:\dev\scrcpy-cam\dist\scrcpy-cam-win64`.

Einschränkung: Während des Neustarts (1–3 s) verschwindet das Fenster kurz.

## 8. WLAN-ADB ohne Eingriff am Handy

Ausführliche Checkliste: [`tools/SETUP-PHONE.md`](../tools/SETUP-PHONE.md).
Bebilderte Anleitung zum Einschalten von „Debugging über WLAN“ (One UI, persönliche Daten
weichgezeichnet): [`anleitung-wlan-debugging/`](anleitung-wlan-debugging/wlan-debugging.adoc) –
`wlan-debugging.html`/`.pdf`, neu erzeugen mit `build.ps1` (asciidoctor aus `C:\localapps\ruby`).

Kurzfassung:

| Problem | Lösung |
|---|---|
| Port ändert sich bei jedem Einschalten | Kein Problem: Kopplung bleibt, adb findet das Handy per mDNS; der Launcher verbindet neu. *Getestet: neuer Port, ohne neues Koppeln.* |
| Wireless Debugging geht nach Neustart/WLAN-Verlust aus | **Standard: bei Bedarf am Handy von Hand wieder einschalten.** Optional: MacroDroid/Tasker mit `WRITE_SECURE_SETTINGS` setzt `adb_wifi_enabled=1` (bei Start, WLAN-Verbindung, alle 5 min). *Ungetestet.* |
| Kopplung verfällt nach 7 Tagen ohne Nutzung | Entwickleroptionen: „Zeitüberschreitung der ADB-Autorisierung deaktivieren“. |
| Samsung blockiert ADB | „Automatische Sperre“ (Auto Blocker) aus. |
| Neustart → Handy gesperrt bis PIN | **Nicht lösbar ohne Eingriff am Handy.** Neustarts vermeiden (automatischen Neustart und Auto-Updates aus). Später evtl. Bluetooth-HID. |
| Mesh-WLAN | Prüfen, ob die WLAN-Freigabe für Wireless Debugging pro Access Point erfragt wird (offen). |

Alternativen zu WLAN wurden geprüft und verworfen: Ein USB-C-Anschluss kann nicht gleichzeitig
HDMI-Hub (Handy = Host) und ADB-Gerät (Handy = Device) bedienen; ADB über Ethernet am Hub
braucht `adb tcpip`, das nach jedem Neustart per USB neu gesetzt werden muss.

**Sicherheit:** Dauerhaft aktives Wireless Debugging ohne Ablauf macht den adb-Schlüssel des PCs
(`%USERPROFILE%\.android\adbkey`) zu einem Generalschlüssel für das Handy. Windows-Konto mit
Passwort schützen, Schlüssel nicht weitergeben.

## 9. Bauen

Voraussetzung: Docker Desktop.

```powershell
.\tools\build-windows.ps1              # Ergebnis: C:\dev\scrcpy-cam\dist\scrcpy-cam-win64\
.\tools\build-windows.ps1 -NoCache     # Abhängigkeiten neu bauen
.\tools\build-windows.ps1 -OutDir D:\x -ServerJar D:\scrcpy-server
```

Ablauf:

1. Startet Docker Desktop, falls nötig.
2. Baut das Image `scrcpy-cam-build` aus [`tools/docker/Dockerfile`](../tools/docker/Dockerfile)
   (Ubuntu 24.04, mingw-w64, meson, ninja, nasm, cmake …).
3. [`tools/docker/build.sh`](../tools/docker/build.sh) im Container:
   - kopiert das Repo per `rsync` ins Container-Dateisystem (ohne `.git`, ohne Build-Ordner),
   - übersetzt und startet den Unit-Test `test_capture_geometry` (gcc nativ),
   - setzt `VERSION=5.0.1-cam` (weil `.git` fehlt) und ruft das offizielle
     `release/build_windows.sh 64` auf.
4. FFmpeg, SDL3, dav1d, libusb und adb werden im Docker-Volume `scrcpy-cam-deps` gecacht.
   Erster Build: 20–40 min. Folgebuilds: wenige Minuten.
5. Kopiert das Ergebnis nach `dist\scrcpy-cam-win64` und legt den unveränderten
   `scrcpy-server` 5.0.1 dazu (Standard: `C:\dev\scrcpy-cam\scrcpy-win64-v5.0.1\scrcpy-server`).

Wichtig:

- **Die Versionsnummer in `meson.build` muss `5.0.1` bleiben.** Der Client schickt seine
  Version an den Server, und der Server bricht bei Abweichung ab. Der Server wird nicht neu gebaut.
- **Zeilenenden:** Im Repo ist `core.autocrlf=false` gesetzt, und `.gitattributes` erzwingt LF für
  `*.sh`. Mit CRLF bricht bash im Container ab.

### Repository

| Remote | URL | Zweck |
|---|---|---|
| `origin` | `https://github.com/jaui/scrcpy-cam.git` | Fork, Branch `main` |
| `upstream` | `https://github.com/Genymobile/scrcpy.git` | Original |

`main` = upstream-Tag `v5.0.1` + eigene Commits.

Auf eine neue scrcpy-Version umstellen (Beispiel 5.1):

```powershell
git fetch upstream --tags
git rebase --onto v5.1 v5.0.1 main     # eigene Commits auf v5.1 setzen, Konflikte lösen
# scrcpy-server aus dem offiziellen 5.1-Release verwenden (-ServerJar), dann bauen und testen
git push --force-with-lease origin main
```

## 10. Interna

### Neue und geänderte Dateien

| Datei | Inhalt |
|---|---|
| `app/src/capture.{c,h}` | Bildquelle (`struct sc_capture`, Frame-Source-Trait), Kamera-Thread, Display-Monitor-Thread, Positions-Mapper |
| `app/src/capture_geometry.{c,h}` | Reine Rechenfunktionen: Einpassen, Zuschnitt, Koordinaten-Abbildung, Parser. Ohne FFmpeg/SDL-Abhängigkeit. |
| `app/tests/test_capture_geometry.c` | Unit-Test dazu |
| `app/src/controller.{c,h}` | Optionaler Hook `map_position` für Touch- und Scroll-Nachrichten |
| `app/src/adb/adb.{c,h}` | `sc_adb_shell_read()`: Ausgabe von `adb shell <cmd>` lesen |
| `app/src/options.{c,h}`, `app/src/cli.c` | Optionen `--capture-card*`, Prüfungen |
| `app/src/scrcpy.c` | `SDL_INIT_CAMERA`, Fenster mit Video bei `--capture-card`, Start/Stop der Bildquelle, Mapper setzen |
| `app/meson.build` | Neue Quellen und Test |
| `tools/` | Build, Launcher, Handy-Setup, Python-Prototyp |

### Datenfluss Bild

1. `run_capture` (Thread `scrcpy-capture`) öffnet die erste Kamera, deren Name den Filter
   enthält, mit `SDL_OpenCamera` im Format **NV12**, 1920×1080, 60 fps.
2. Pro Frame (`SDL_AcquireCameraFrame`): Ausschnitt bestimmen (siehe unten), Y-Zeilen kopieren,
   UV-Werte aus NV12 in getrennte U/V-Ebenen schreiben → `AVFrame` im Format `YUV420P`.
3. Farbraum: von SDL gemeldet, sonst BT.709 mit begrenztem Bereich (üblich bei HDMI-Capture).
4. Beim ersten Frame werden die Sinks mit einem leeren `AVCodecContext` (Größe, Pixelformat)
   geöffnet – das öffnet das Fenster. Größenwechsel (Drehung) laufen über
   `sc_frame_source_sinks_push_session` und Frames in neuer Größe; das Fenster passt sich an.
5. Der Bildschirm (`screen.c`) schaltet bei `YUV420P` automatisch auf die Software-Ausgabe
   (`interop_software`) um.
6. Kommen 3 s lang keine Frames (Karte abgezogen), wird die Kamera geschlossen und jede Sekunde
   neu geöffnet. Das Fenster bleibt offen. *Noch nicht mit echtem Abziehen getestet.*

### Ausschnitt (schwarze Ränder)

Das Handy skaliert sein Display seitenverhältnistreu und zentriert in das HDMI-Bild.
`sc_capture_geometry_fit(frame, display)` berechnet daraus das Rechteck (Werte gerade, für
YUV 4:2:0). Beispiele:

| Display (logisch) | Frame | Ausschnitt |
|---|---|---|
| 1080×2400 hochkant | 1920×1080 | 486×1080 bei x=716 |
| 2400×1080 quer | 1920×1080 | 1920×864 bei y=108 |
| 1080×2340 hochkant | 1920×1080 | 498×1080 bei x=710 |
| 2340×1080 quer | 1920×1080 | 1920×886 bei y=96 |
| 1920×1080 (DeX) | 1920×1080 | ganzer Frame |

Mit `--capture-card-crop` wird stattdessen ein fester Ausschnitt verwendet.

### Displaygröße und Drehung

Thread `scrcpy-capmon` fragt einmal pro Sekunde (erstes Mal synchron beim Start) ab:

```sh
dumpsys display | grep mOverrideDisplayInfo | sed -E \
  's/.*displayId ([0-9]+).* real ([0-9]+) x ([0-9]+).* rotation ([0-9]).*/\1 \2 \3 \4/'
```

Ausgabe: eine Zeile pro Display, z. B. `0 1080 2400 0`. Die „real“-Größe ist die logische Größe
und berücksichtigt die Drehung bereits. Dauer über WLAN: rund 300 ms. Die Orientierung wird
bewusst **nicht** aus dem Bild geraten (dunkle Apps im Querformat würden falsch erkannt).

### Koordinaten

Ohne Videostream kennt der Server keine Bildgröße und verwendet die Koordinaten roh auf
`display_id` (`server/.../control/Controller.java`, `getEventPointAndDisplayId`). Deshalb rechnet
der Client um: `sc_controller_push_msg` ruft für Touch- und Scroll-Nachrichten
`sc_capture_map_position` auf. Der skaliert den Punkt vom Ausschnitt (Fensterinhalt) auf die
aktuelle Displaygröße (Pixelmitten, auf das Display begrenzt) und setzt `screen_size` auf die
Displaygröße. Ist die Displaygröße unbekannt, bleibt die Position unverändert.

Maus-Linksklicks mit Pointer-ID „Maus“ spritzt der Server als **Finger** ein
(`Controller.java`, Quelle `TOUCHSCREEN`), Hover und Rechts-/Mittelklick als Maus.

### Threads und Beenden

- `scrcpy-capture`: Kamera, Frames.
- `scrcpy-capmon`: adb-Abfrage; laufende adb-Prozesse werden beim Beenden per `sc_intr` abgebrochen.
- Beenden: `sc_capture_stop` (beim Stoppen der Decoder) → `sc_capture_join` → `sc_capture_destroy`,
  vor dem Zerstören des Bildschirms.

## 11. Entscheidungen und Erkenntnisse

| Thema | Entscheidung / Befund | Grund |
|---|---|---|
| Bildquelle | HDMI-Capture statt scrcpy-Video | `FLAG_SECURE`-Apps sind über HDMI sichtbar, im scrcpy-Video schwarz. |
| Einbau | In den scrcpy-Client statt Python-Skript | scrcpy-Funktionen (Kürzel, Zwischenablage, saubere Eingabe) bleiben erhalten. |
| Kamera-API | SDL3-Kamera (Media Foundation) | Schon in scrcpy enthalten; das FFmpeg von scrcpy hat kein `avdevice`/DirectShow. |
| Pixelformat | NV12 anfordern, selbst umwandeln | IYUV-Anforderung lieferte mit SDL 3.4 nur Nullen (grünes Bild), auch mit gesetztem Farbraum. |
| Optionsname | `--capture-card` | `--capture-orientation` gibt es schon (Aufnahme auf dem Handy). |
| Spiegeln vs. DeX | Spiegeln (hochkant) bevorzugt | Größere Klickziele (Handy-UI für Finger gebaut), einfache Bedienung; Gesten über Kürzel ersetzbar. DeX: kleine Elemente, Fenster, Hover. |
| Display-Auswahl | `--display-id`, Standard 0 | Neben Display 0 gibt es je nach Gerät ein DeX-Display („Desktop“, im Spiegelmodus aus) und das HDMI-Display, dessen ID wechselt (z. B. 7, 14, 15). Im Spiegelmodus wird Display 0 gesteuert. |
| `--turn-screen-off` | Nicht verwenden | Macht im Spiegelmodus auch das HDMI-Bild schwarz. |
| Server | Unverändert 5.0.1 | Kein Android-SDK-Build nötig; Version muss passen. |
| Verbindung | WLAN-ADB + Launcher; Wireless Debugging bei Bedarf von Hand einschalten (Automatik optional) | USB-Anschluss ist durch HDMI belegt. |
| Sichere Apps | Ansatz tragfähig | `FLAG_SECURE`-Apps über HDMI sichtbar, kein HDCP-Schwarz, Eingaben werden angenommen, Apps laufen mit Entwickleroptionen (Testgerät B). |

## 12. Testprotokoll

Die Testgeräte sind in der [Kompatibilitätsliste](../README.md#kompatibilität) aufgeführt.

### 19.11.2026 – Testgerät A (Android 13), UGREEN 25772, Spiegelmodus

| # | Test | Ergebnis |
|---|---|---|
| 1 | Docker-Build, `--help` zeigt Optionen, keine Compiler-Warnungen | bestanden |
| 2 | Unit-Test `test_capture_geometry` | bestanden |
| 3 | Bild: Farben, Ränder entfernt (486×1080), Bildrate | bestanden, 59–61 fps |
| 4 | Wisch vom oberen Rand → Benachrichtigungen | bestanden |
| 5 | Rechtsklick → Zurück | bestanden |
| 6 | Klick auf App-Symbol („Notizen“) → App öffnet | bestanden |
| 7 | Klick auf Zurück-Taste unten rechts | bestanden (Ecke trifft) |
| 8 | Drehung quer: Erkennung, Fenster 1920×864, Klick ins Suchfeld | bestanden |
| 9 | Drehung zurück hochkant | bestanden |
| 10 | `--turn-screen-off` | HDMI-Bild schwarz → nicht verwenden; nach Beenden wieder Bild |
| 11 | Launcher: `adb disconnect` → Neustart nach ~2 s | bestanden |
| 12 | Launcher: Fenster schließen → Launcher endet | bestanden |

Vorgängertests mit dem Python-Prototyp: Tasten, Finger- und Maus-Wisch über den
scrcpy-Server mit `video=false` funktionieren auf Display 0; Display 2 (DeX, aus) reagiert nicht.

### 19.11.2026 – Testgerät B (Android 17, One UI 9.0), UGREEN 25772, Spiegelmodus

Displayliste mit HDMI: Display 0 „Integrierter Bildschirm“ 1080×2340 (FHD+-Modus; WQHD 1440×3120
wäre einstellbar, die Abfrage liest immer die aktuelle Größe) und Display 7 „UGREEN-UHD“
1920×1080, `EXTERNAL`, `FLAG_PRESENTATION`, Zustand OFF (Spiegelmodus). Gesteuert wird Display 0.

| # | Test | Ergebnis |
|---|---|---|
| 1 | Koppeln per Code, Verbindung per WLAN | bestanden |
| 2 | Bild: Ränder entfernt (498×1080), Bildrate | bestanden, 60 fps |
| 3 | Wisch vom oberen Rand → Benachrichtigungen, Rechtsklick → Zurück | bestanden |
| 4 | **Authenticator-App** (`FLAG_SECURE`): adb-Screenshot schwarz, über HDMI Inhalt sichtbar | **bestanden** (kein HDCP-Schwarz) |
| 5 | **App mit Login** (`FLAG_SECURE`): über HDMI sichtbar, startet trotz aktiver Entwickleroptionen und Wireless Debugging | **bestanden** |
| 6 | Dieselbe App: Klick „Anmelden“ → Login-Seite; Texteingabe ins Login-Feld per PC-Tastatur und per Mausklicks auf die Bildschirmtastatur | **bestanden** |
| 7 | Drehung quer: Erkennung 2340×1080/90°, Fenster 1920×886, Klick auf Zurück-Pfeil oben links trifft | bestanden |
| 8 | Drehung zurück hochkant, automatische Drehung wiederhergestellt | bestanden |
| 9 | Wireless Debugging aus/an → neuer Port, adb verbindet per mDNS ohne neues Koppeln | bestanden |
| 10 | Launcher mit `-Serial <Seriennummer>` startet scrcpy mit `--capture-card` | bestanden |
| 11 | `settings put global adb_wifi_enabled 0/1` aus einem adb-Shell-Hintergrundprozess | nicht aussagekräftig: der Prozess endet mit der WLAN-adb-Verbindung, Wireless Debugging blieb aus und wurde von Hand eingeschaltet |

Nicht getestet: vollständiger Login mit Freigabe per Authenticator (keine Zugangsdaten verwendet).

Hinweis zu Tests: Ein `SetForegroundWindow` aus einem Hintergrundprozess wird von Windows oft
verweigert. Simulierte Tastendrücke landen dann im falschen Fenster. Mausklicks auf
Bildschirmkoordinaten sind zuverlässiger.

### Offen

- UGREEN-USB abziehen/einstecken → Bild kommt zurück
- DeX: `--display-id=<DeX-ID>`, 1:1-Abbildung
- MOD+r (Handy drehen)
- Vollständiger Login mit Freigabe per Authenticator
- Mesh/AP-Wechsel: wird die Freigabe „Wireless Debugging in diesem Netzwerk“ erneut erfragt?
- Optional: MacroDroid-Makro nach Neustart/WLAN-Verlust

## 13. Bekannte Einschränkungen

- Bei Verbindungsverlust beendet sich scrcpy; der Launcher startet es neu, das Fenster ist dabei
  1–3 s weg. Neu verbinden im selben Prozess ist noch nicht umgesetzt.
- Nach einem Neustart muss das Handy einmal am Gerät entsperrt werden (PIN).
- Die Abbildung setzt voraus, dass das Handy sein Display unverzerrt und zentriert ausgibt.
  Falls nicht: `--capture-card-crop`.
- Im Spiegelmodus kein `--turn-screen-off` (siehe oben); das Handy-Display bleibt an.
- Ton: Der HDMI-Ton der Karte wird nicht abgespielt. scrcpy-Audio vom Handy funktioniert wie
  gewohnt (oder `--no-audio`).
- Nur Windows getestet (SDL-Kamera gibt es auch unter Linux/macOS, aber ungeprüft).

## 14. Fehlersuche

| Symptom | Ursache / Lösung |
|---|---|
| `Capture card: no camera matching "UGREEN"` + Liste | Anderer Name → `--capture-card="<Teil des Namens>"`. Karte belegt (OBS, Kamera-App, Python-Skript) → schließen. |
| Grünes Bild | Pixelformat-Problem (z. B. alte Version mit IYUV) → aktuellen Build verwenden. |
| Schwarzes Bild | Handy-Display aus (`--turn-screen-off`, MOD+o) → MOD+Shift+o; HDMI-Kabel/Hub; HDCP bei geschützten Inhalten. |
| Bild verzerrt/abgeschnitten, Klicks daneben | Displaygröße falsch/unbekannt (Warnung `could not read the size of display`) → `--display-id` prüfen, adb-Verbindung prüfen, notfalls `--capture-card-crop`. |
| Klicks wirken nicht | Falsches Display (z. B. DeX-Display aus) → im Spiegelmodus `--display-id=0`. |
| `adb: no devices` / Launcher wartet | Wireless Debugging aus (Automatik prüfen), anderes WLAN, Multicast blockiert, Kopplung abgelaufen. `adb mdns services` zeigt, ob das Handy gefunden wird. |
| Server bricht direkt nach dem Start ab, Log nennt unterschiedliche Versionen | `scrcpy-server` neben `scrcpy.exe` muss Version 5.0.1 sein. |
| Build bricht mit `$'\r': command not found` ab | CRLF in `.sh`-Dateien → `git config core.autocrlf false`, neu auschecken. |

Mehr Ausgaben: `--verbosity=debug`.

## 15. Nächste Schritte

1. Offene Tests (siehe Testprotokoll „Offen“).
2. Optional: Handy-Automatik (MacroDroid), die Wireless Debugging selbst wieder einschaltet.
3. Zusätzliche Tastenkürzel für Gesten (Wischen links/rechts/hoch, langes Drücken).
4. Neuverbinden im selben Prozess (Fenster bleibt sichtbar).
5. Optional: Bluetooth-HID als Rückfallebene für den Sperrbildschirm nach Neustart.

## Anhang: Python-Prototyp

[`tools/python-prototype`](../tools/python-prototype) – der erste Versuch (OpenCV/pygame +
scrcpy-Server nur mit Steuerkanal). Abgelöst durch `--capture-card`. Bekannte Mängel stehen in
dessen README.
