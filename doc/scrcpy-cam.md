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
| `--capture-card-fps=N` | `30` | Angeforderte Bildrate. 30 fps braucht etwa halb so viel CPU wie 60 fps (siehe [Leistung](#leistung-und-bildschirm)). |
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
scrcpy --capture-card --capture-card-fps=60    # flüssiger, doppelte CPU-Last
scrcpy --capture-card --fullscreen             # Full-HD-Bildschirm: Handybild 1:1 in voller Höhe
```

### Leistung und Bildschirm

CPU-Last von `scrcpy.exe` in Prozent eines Kerns, über alle Threads. Gemessen auf einem
i7-1365U mit Testgerät B und UGREEN 25772, jeweils 20–30 s, ohne Ton (`--no-audio`).
„Ruhend“ heißt: Handybildschirm unverändert. „Bewegung“ heißt: Die Benachrichtigungsleiste
klappt laufend auf und zu. Die Werte schwanken um etwa ±1 Prozentpunkt.

| Modus | ruhend | Bewegung |
|---|---|---|
| `--capture-card` (30 fps, Standard) | ≈ 4 % | ≈ 6–8 % |
| `--capture-card --capture-card-fps=20` | ≈ 2,6 % | – |
| `--capture-card --capture-card-fps=60` | ≈ 8 % | ≈ 11 % |
| zum Vergleich: normales scrcpy (H.264 vom Handy) | – | ≈ 7 % |
| zum Vergleich: normales scrcpy `--max-fps=30` | – | ≈ 3 % |
| zusätzlich: Ton vom Handy (ohne `--no-audio`) | + ≈ 1–1,5 % | |

Woher die Last kommt (Messung pro Thread):

- **SDL-Kamera / Media Foundation** (Frames von der Karte abholen): ≈ 3 % bei 30 fps. Das ist die
  Untergrenze, gemessen auch mit einem minimalen Leser ohne scrcpy. Sie hängt nur von der
  Bildrate ab. NV12 ist ein natives Format der Karte, Windows rechnet also nicht um.
- **Unveränderte Frames werden übersprungen:** Bei ruhendem Bild liefert die Karte bitgleiche
  Frames. Sie werden direkt im Kartenpuffer erkannt (Vergleich mit einer Kopie des letzten
  Ausschnitts) und weder umgewandelt noch gezeichnet. Hauptthread und Grafiktreiber
  (zusammen ≈ 3,5 %) fallen dann weg. `--print-fps` zeigt deshalb wie beim normalen scrcpy
  0 fps, solange sich nichts ändert.
- **Ton:** Ist der Handyton am PC nicht nötig, spart `--no-audio` noch ≈ 1–1,5 %.

Für einen schwächeren PC (z. B. i5-7300U, 2 Kerne/4 Threads) ist 30 fps der Standard. Grob
gerechnet ist ein Kern dort halb so schnell wie hier; zu erwarten sind etwa 8 % eines Kerns
ruhend und 12–16 % bei Bewegung. Reicht das nicht, hilft `--capture-card-fps=20`.

Fenstergröße: Ohne weitere Optionen öffnet sich das Fenster mit einem Kartenpixel pro
Bildschirmpixel, also hochkant 498×1080 Bildpunkte. Passt das nicht auf den Bildschirm,
verkleinert scrcpy das Fenster. Die Bildrate ist dabei egal.

- **Full-HD-Bildschirm (1920×1080):** Mit `--fullscreen` (oder MOD+<kbd>f</kbd>) wird das Bild
  ohne Skalierung in voller Höhe gezeigt, also 1:1 und scharf. Im Fenstermodus nehmen Taskleiste
  und Titelzeile Platz weg, deshalb verkleinert scrcpy leicht.
- **HiDPI-Bildschirm (z. B. 2560×1600, 150 %):** Das Fenster ist zunächst klein (1:1). Größer
  wird es mit `--window-height=<Pixel>` oder `--fullscreen`. Dann wird hochskaliert, mit
  etwas weicherem Bild, denn mehr als 1080 Bildzeilen liefert die Karte nicht.
- Seitenverhältnis: Standard ist `--render-fit=letterbox`, das Bild wird nie verzerrt, und
  Klicks in die schwarzen Ränder werden ignoriert.

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
                       [-ScrcpyArgs '--fullscreen','--no-audio'] [-RetrySeconds 2] [-MaxRetrySeconds 60]
```

- Läuft mit Windows PowerShell 5.1 (`powershell.exe`) und PowerShell 7 (`pwsh`).
- Sucht das gekoppelte Handy über `adb devices` und `adb mdns services`
  (der Port von Wireless Debugging darf sich ändern).
- Startet scrcpy mit `--serial=<Gerät> --capture-card` und den zusätzlichen Argumenten
  (Argumente mit Leerzeichen, z. B. `'--capture-card=USB Video'`, werden korrekt übergeben).
- Endet scrcpy mit einem Fehler (Code 1, z. B. Fehler der Capture-Karte) oder wegen
  Verbindungsverlust (Code 2), wird nach `RetrySeconds` neu verbunden und neu gestartet.
- Scheitert der Start dreimal hintereinander nach weniger als 15 s (z. B. falsches Argument),
  verdoppelt sich der Abstand bis `MaxRetrySeconds` (2, 4, 8 … 60 s) statt endlos alle 2 s.
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
| Wireless Debugging geht nach Neustart/WLAN-Verlust/AP-Wechsel aus | **Standard: bei Bedarf am Handy von Hand wieder einschalten.** Optional: eigene App „WLAN-Debugging“ ([`tools/adbwifi-android`](../tools/adbwifi-android), ≈ 45 KB, Kachel + Icon + Automatik) oder MacroDroid/Tasker; beide setzen mit `WRITE_SECURE_SETTINGS` `adb_wifi_enabled=1`. |
| Kopplung verfällt nach 7 Tagen ohne Nutzung | Entwickleroptionen: „Zeitüberschreitung der ADB-Autorisierung deaktivieren“. |
| Samsung blockiert ADB | „Automatische Sperre“ (Auto Blocker) aus. |
| Neustart → Handy gesperrt bis PIN | **Standard:** Neustarts vermeiden (automatischen Neustart und Auto-Updates aus), nach einem Neustart PIN am Handy eingeben. **Optional** (Zusatzhardware): Bluetooth-Tastatur per ESP32 ([`tools/bt-hid`](../tools/bt-hid)) gibt die PIN vom PC aus ein – sie verbindet sich schon vor dem ersten Entsperren. *Getestet auf Testgerät A.* Wireless Debugging muss danach noch eingeschaltet werden (von Hand oder per Automations-App). |
| Mesh-WLAN | Android merkt sich die Freigabe „Immer in diesem Netzwerk zulassen“ **pro Access Point (BSSID)** und schaltet Wireless Debugging bei einem AP-Wechsel ab (AOSP `AdbDebuggingManager`). Jeder AP muss einmal am Handy bestätigt werden – **auch 2,4 und 5 GHz desselben Routers** (getrennte BSSIDs; auf Testgerät A nach einem Neustart beobachtet). Danach schaltet die optionale App nach einem Wechsel selbst wieder ein. *Am echten Mesh noch ungetestet.* |

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
   enthält, mit `SDL_OpenCamera` im Format **NV12**, 1920×1080, 30 fps (Standard).
2. Pro Frame (`SDL_AcquireCameraFrame`): Ausschnitt bestimmen (siehe unten), Y-Zeilen kopieren,
   UV-Werte aus NV12 in getrennte U/V-Ebenen schreiben → `AVFrame` im Format `YUV420P`.
   Die Puffer kommen aus einem `AVBufferPool` (keine Speicherreservierung pro Frame). Nach
   einem Frame schläft der Thread ¾ der Frame-Dauer, erst dann wird alle 4 ms nach dem
   nächsten Frame gefragt. Ist der Ausschnitt bitgleich mit dem zuletzt gezeigten, wird der
   Frame verworfen, ohne ihn umzuwandeln oder zu zeichnen (siehe
   [Leistung](#leistung-und-bildschirm)).
3. Farbraum: von SDL gemeldet, sonst BT.709 mit begrenztem Bereich (üblich bei HDMI-Capture).
4. Beim ersten Frame werden die Sinks mit einem leeren `AVCodecContext` (Größe, Pixelformat)
   geöffnet – das öffnet das Fenster. Größenwechsel (Drehung) laufen über
   `sc_frame_source_sinks_push_session` und Frames in neuer Größe; das Fenster passt sich an.
5. Der Bildschirm (`screen.c`) schaltet bei `YUV420P` automatisch auf die Software-Ausgabe
   (`interop_software`) um.
6. Kommen 3 s lang keine Frames (Karte abgezogen), wird die Kamera geschlossen und jede Sekunde
   neu geöffnet. Das Fenster bleibt offen. *Noch nicht mit echtem Abziehen getestet.*
7. Schlägt das Weitergeben eines Frames fehl, meldet der Thread `SC_EVENT_CAPTURE_ERROR`:
   scrcpy endet mit Code 1 (der Launcher startet neu), statt mit eingefrorenem Bild
   weiterzulaufen.

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

Abfrage (einmal synchron beim Start, damit schon die ersten Frames richtig zugeschnitten sind):

```sh
{ cmd display get-displays 2>/dev/null | grep 'DisplayInfo{' \
  || dumpsys display | grep mOverrideDisplayInfo; } | sed -E \
  's/.*displayId ([0-9]+).* real ([0-9]+) x ([0-9]+).* rotation ([0-9]).*/\1 \2 \3 \4/'
```

Ausgabe: eine Zeile pro Display, z. B. `0 1080 2400 0`. Die „real“-Größe ist die logische Größe
und berücksichtigt die Drehung bereits. Die Orientierung wird bewusst **nicht** aus dem Bild
geraten (dunkle Apps im Querformat würden falsch erkannt).

Danach läuft dieselbe Abfrage im Thread `scrcpy-capmon` als Schleife in **einer** dauerhaften
`adb shell` (jede Sekunde, Blöcke durch eine Zeile `.` getrennt). Die Schleife läuft nur,
solange ein scrcpy-Server lebt (`pgrep -f 'com[.]genymobile[.]scrcpy[.]Server'`): Wird scrcpy
hart beendet (Absturz, Task-Manager), endet der Server, und mit ihm nach spätestens 1–2 s die
Schleife und der verwaiste `adb.exe`-Prozess. Endet die Shell (Verbindung weg), wird sie nach
1 s neu gestartet.

Kosten, gemessen auf Testgerät B: `cmd display get-displays` ≈ 13 ms und 6 KB Ausgabe pro
Aufruf, `dumpsys display` ≈ 36 ms und ≈ 400 KB (nur noch Rückfall für ältere Android-Versionen).
Auf dem PC entsteht kein neuer `adb.exe`-Prozess pro Sekunde mehr.

### Koordinaten

Ohne Videostream kennt der Server keine Bildgröße und verwendet die Koordinaten roh auf
`display_id` (`server/.../control/Controller.java`, `getEventPointAndDisplayId`). Deshalb rechnet
der Client um: `sc_controller_push_msg` ruft für Touch- und Scroll-Nachrichten
`sc_capture_map_position` auf. Der skaliert den Punkt vom Ausschnitt (Fensterinhalt) auf die
Displaygröße (Pixelmitten, auf das Display begrenzt) und setzt `screen_size` auf die
Displaygröße.

- Verwendet wird die Displaygröße, **mit der der angezeigte Frame zugeschnitten wurde**. Die
  Bildquelle merkt sich die letzten zwei Zuordnungen „Frame-Größe → Displaygröße“; direkt nach
  einer Drehung, solange das Fenster noch den alten Frame zeigt, passt so die alte Zuordnung.
- Ist die Displaygröße unbekannt (adb-Abfrage noch nicht erfolgreich), werden Touch- und
  Scroll-Ereignisse **verworfen**. Der Server würde sie sonst mit den rohen Fensterkoordinaten
  an falscher Stelle einspeisen.

Maus-Linksklicks mit Pointer-ID „Maus“ spritzt der Server als **Finger** ein
(`Controller.java`, Quelle `TOUCHSCREEN`), Hover und Rechts-/Mittelklick als Maus.

### Threads und Beenden

- `scrcpy-capture`: Kamera, Frames.
- `scrcpy-capmon`: dauerhafte adb-Shell für die Displaygröße; beim Beenden wird der adb-Prozess
  per `sc_intr` beendet.
- Aufwecken: Normalerweise weckt scrcpy ohne Video das Handy beim Start nicht. Mit
  `--capture-card` schon (das HDMI-Bild braucht ein eingeschaltetes Display);
  `--no-power-on` wird weiterhin beachtet.
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

### 19.11.2026 – Testgerät B, nach den Review-Korrekturen

| # | Test | Ergebnis |
|---|---|---|
| 1 | Handy im Ruhemodus (Dozing), Start mit `--capture-card` | alter Build: bleibt aus; neuer Build: wird geweckt |
| 2 | Displaygröße über dauerhafte adb-Shell (`wm size` vorübergehend geändert) | Änderung erkannt; kein neuer `adb.exe` pro Sekunde |
| 3 | scrcpy hart beendet (`Stop-Process -Force`) | Abfrage-Schleife auf Handy und PC nach ≤ 2 s beendet |
| 4 | Bild 30 fps, Zuschnitt, Wisch von oben, Rechtsklick → Zurück | bestanden |
| 5 | Launcher unter `powershell.exe` 5.1, adb-Server vorher beendet | läuft weiter |
| 6 | Launcher `-ScrcpyArgs '--capture-card=USB Video'` | Argument kommt vollständig an |
| 7 | Launcher mit ungültigem Argument | Abstände 2, 2, 4, 8, 16 … 60 s |
| 8 | `--capture-card-fps=30` ohne `--capture-card` | Fehlermeldung (vorher bei Standardwert still akzeptiert) |
| 9 | CPU-Vergleich | siehe [Leistung und Bildschirm](#leistung-und-bildschirm) |

Hinweis zu Tests: Ein `SetForegroundWindow` aus einem Hintergrundprozess wird von Windows oft
verweigert. Simulierte Tastendrücke landen dann im falschen Fenster. Mausklicks auf
Bildschirmkoordinaten sind zuverlässiger.

### Offen

- UGREEN-USB abziehen/einstecken → Bild kommt zurück
- DeX: `--display-id=<DeX-ID>`, 1:1-Abbildung
- MOD+r (Handy drehen)
- Vollständiger Login mit Freigabe per Authenticator
- Mesh/AP-Wechsel: wird die Freigabe „Wireless Debugging in diesem Netzwerk“ erneut erfragt?
- Optional: App „WLAN-Debugging“ (`tools/adbwifi-android`) auf Testgerät B und am Mesh
  (Testgerät A bestanden, siehe README der App)
- Optional: bt-hid auf Testgerät B (Kopplung, Entsperren, Neustart)

### 19.11.2026 – Testgerät A, optionale Bluetooth-Tastatur (`tools/bt-hid`, ESP32-C3 SuperMini)

| Test | Ergebnis |
|---|---|
| Kopplung im Kopplungsfenster („Just Works“, nur Bestätigung) | ✅ Android erkennt „Keyboard“ und „Consumer Control“ |
| Kopplung außerhalb des Fensters | wird abgewiesen und vom ESP32 sofort gelöscht (das Handy zeigt das Gerät trotzdem als gekoppelt) |
| `CC HOME`, `CC BACK`, `CC POWER` | ✅ |
| `TYPE yz-123/` in ein Suchfeld | ✅ unverändert angekommen (US-Layout) |
| `KEY BACKSPACE` | ✅ |
| Wecken aus dem Standby per Taste | ✅ |
| Entsperren: `KEY ENTER`, `SLEEP 1`, `TYPE <PIN>`, `KEY ENTER` | ✅ 3/3, kein Fehlversuch |
| Wecken mit `KEY SPACE`, dann PIN | ❌ Handy ging sofort wieder in den Standby, Ziffern kamen nicht an (kein Fehlversuch gezählt) |
| Neustart des Handys | ✅ Tastatur 74 s nach dem Neustart wieder verbunden, **vor** dem ersten Entsperren; PIN eingegeben, Handy entsperrt |
| ESP32 neu geflasht | ✅ Kopplung bleibt erhalten, Handy verbindet sich sofort wieder |
| Wireless Debugging per Tastatur einschalten | ❌ nicht zuverlässig: `GUI+N` + `TAB` erreicht das Zahnrad nur manchmal; Tippen auf dem Startbildschirm öffnet keine Suche; `CC SEARCH` öffnet die Google-Suche (nur Web-Vorschläge) |

## 13. Bekannte Einschränkungen

- Bei Verbindungsverlust beendet sich scrcpy; der Launcher startet es neu, das Fenster ist dabei
  1–3 s weg. Neu verbinden im selben Prozess ist noch nicht umgesetzt.
- Nach einem Neustart muss das Handy einmal am Gerät entsperrt werden (PIN); Wireless Debugging
  ist danach aus. Optional mit Zusatzhardware: PIN per Bluetooth-Tastatur
  ([`tools/bt-hid`](../tools/bt-hid)).
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
2. Optional: App „WLAN-Debugging“ ([`tools/adbwifi-android`](../tools/adbwifi-android)) auf beiden
   Testgeräten prüfen.
3. Zusätzliche Tastenkürzel für Gesten (Wischen links/rechts/hoch, langes Drücken).
4. Neuverbinden im selben Prozess (Fenster bleibt sichtbar).
5. Optional: Bluetooth-Tastatur ([`tools/bt-hid`](../tools/bt-hid)) auf Testgerät B testen. Eine
   Einbindung in den Launcher wäre ebenfalls optional (nur aktiv, wenn ein ESP32 angeschlossen ist).

## Anhang: Python-Prototyp

[`tools/python-prototype`](../tools/python-prototype) – der erste Versuch (OpenCV/pygame +
scrcpy-Server nur mit Steuerkanal). Abgelöst durch `--capture-card`. Bekannte Mängel stehen in
dessen README.
