# bt-hid – Bluetooth-Tastatur als Rückfallebene (optional)

> **Optionales Zusatzmodul.** scrcpy-cam funktioniert ohne bt-hid; der Client und der Launcher
> nutzen es nicht. Es braucht zusätzliche Hardware (ESP32) und ist nur für den Notfall gedacht,
> wenn adb nicht verfügbar ist.

Ein ESP32 meldet sich am Handy als **Bluetooth-LE-Tastatur** (mit Systemtasten wie Home, Zurück,
Power) und wird vom PC über USB gesteuert. Das funktioniert auch dann, wenn adb nicht verfügbar
ist – vor allem **nach einem Neustart des Handys**: Die Tastatur verbindet sich schon vor dem
ersten Entsperren, der PC kann so die PIN eingeben.

```
 PC ──USB (seriell)──► ESP32 ──Bluetooth LE (HID)──► Handy
     "TYPE …", "KEY ENTER"       Tastendrücke
```

## Hardware

Ein ESP32 mit Bluetooth LE **und eingebautem USB** (USB Serial/JTAG): ESP32-C3, -C6 oder -S3.
Getestet: ESP32-C3 SuperMini. Der klassische ESP32 hat kein natives USB und wird nicht unterstützt.

## Bauen und Flashen

Bauen mit ESP-IDF 5.4 im Docker-Container (PowerShell, Ausgabe nach `..\dist\bt-hid`):

```powershell
docker run --rm -v "${PWD}\tools\bt-hid:/src:ro" -v "${PWD}\..\dist\bt-hid:/out" espressif/idf:v5.4.2 bash -c `
  'cp -r /src /tmp/p && cd /tmp/p && idf.py set-target esp32c3 && idf.py build && cp build/bootloader/bootloader.bin build/partition_table/partition-table.bin build/bt-hid.bin /out/'
```

Für C6/S3 `esp32c6` bzw. `esp32s3` statt `esp32c3`.

Flashen (`pip install esptool pyserial`, Port im Geräte-Manager nachsehen):

```
esptool --chip esp32c3 --port COM4 -b 460800 write_flash --flash_mode dio --flash_size 4MB --flash_freq 80m ^
  0x0 bootloader.bin 0x8000 partition-table.bin 0x10000 bt-hid.bin
```

Gespeicherte Kopplungen liegen im NVS-Bereich und bleiben beim erneuten Flashen erhalten
(`esptool erase_flash` löscht sie).

## Koppeln

Neue Kopplungen nimmt der ESP32 nur an, solange der PC das **Kopplungsfenster** geöffnet hat;
sonst werden fremde Geräte abgewiesen. Bereits gekoppelte Handys verbinden sich jederzeit.

1. `python bthid.py "PAIR 300"` – 5 Minuten Kopplungsfenster.
2. Am Handy: Einstellungen → Verbindungen → Bluetooth → **„scrcpy-cam HID“** → Koppeln
   (nur Bestätigung, kein Code).
3. `python bthid.py STATUS` → `connected=1 bonds=1`.

Nach der ersten Kopplung schließt sich das Fenster. `UNPAIR` löscht alle Kopplungen.

## PC-Werkzeug `bthid.py`

Benötigt Python 3 mit `pyserial`. Ohne `--port` wird der ESP32 automatisch gesucht.
Jedes Argument ist eine Befehlszeile:

```
python bthid.py STATUS
python bthid.py "CC HOME"
python bthid.py "KEY ENTER" "SLEEP 1" "TYPE -" "KEY ENTER"    # wecken und entsperren
python bthid.py --listen 30                                   # Ereignisse 30 s lang anzeigen
```

- `TYPE -` fragt den Text verdeckt ab. So landet eine PIN weder in der Befehlszeile noch in der
  Shell-History. Der Inhalt von `TYPE` wird nie ausgegeben.
- `SLEEP n` pausiert auf dem PC (wird nicht an den ESP32 gesendet).
- `--port` setzt DTR/RTS nicht, damit der ESP32 beim Öffnen des Ports nicht neu startet.

## Protokoll (USB seriell, 115200 Baud, Zeilen mit `\n`)

| Befehl | Antwort / Wirkung |
|---|---|
| `PING` | `OK pong` |
| `STATUS` | `OK connected=<0\|1> bonds=<n> pairing=<0\|1>` |
| `PAIR [s]` | Kopplungsfenster für s Sekunden (Standard 120, max. 3600) |
| `UNPAIR` | alle Kopplungen löschen, Verbindung trennen |
| `TYPE <text>` | ASCII-Text tippen (US-Tastenpositionen) |
| `KEY <k>[+<k>…]` | Tastenkombination, z. B. `KEY ENTER`, `KEY CTRL+A`, `KEY GUI+N` |
| `CC <name>` | Systemtaste: `HOME BACK POWER VOLUP VOLDOWN MUTE PLAYPAUSE MENU SEARCH` |

Tasten für `KEY`: ein einzelnes ASCII-Zeichen (`A`, `5`, `/` …) oder `ENTER ESC BACKSPACE TAB SPACE UP DOWN LEFT RIGHT HOME END
PGUP PGDN DEL INSERT MENU F1`–`F12`; Modifikatoren `CTRL SHIFT ALT GUI` (`META` = `GUI`).
Fehler: `ERR <grund>`. Ereignisse ohne Anfrage: `EVT …` (z. B. `EVT connected`, `EVT paired`).

## Handy entsperren

```
python bthid.py "KEY ENTER" "SLEEP 1" "TYPE -" "KEY ENTER"
```

- **ENTER** weckt das Handy und öffnet das PIN-Feld. Ein ENTER bei leerem PIN-Feld zählt
  nicht als Fehlversuch.
- **Nicht SPACE zum Wecken:** Das Handy ging im Test danach sofort wieder in den Standby, die
  Ziffern kamen nicht an.
- Android ordnet die Tasten standardmäßig nach US-Layout zu; Ziffern sind unabhängig vom Layout.
  Wird für diese Tastatur am Handy ein anderes Layout eingestellt, ändern sich Buchstaben und
  Sonderzeichen.

## Grenzen

- **Wireless Debugging** bleibt nach einem Neustart aus. Der Weg dorthin nur per Tastatur
  (`GUI+N` → Zahnrad → Einstellungen) ist nicht zuverlässig: Der Tastaturfokus landete im Test
  mal auf dem Zahnrad, mal auf einer Kachel (ein ENTER dort schaltet z. B. WLAN aus).
  Empfohlen: eine Automations-App, die beim Entsperren Wireless Debugging einschaltet
  ([`SETUP-PHONE.md`](../SETUP-PHONE.md), Abschnitt 5).
- Nur Tastatur, keine Maus/Touch. Tippen auf beliebige Stellen ist damit nicht möglich.
- Ein Handy, das mit einer älteren Firmware-Version gekoppelt wurde, zeigt evtl. noch den Namen
  „nimble“ an. Neu koppeln behebt das.

## Sicherheit

- Wer Zugriff auf den PC (oder den USB-Anschluss des ESP32) hat, kann auf dem Handy tippen.
- Kopplung nur im Kopplungsfenster; „Just Works“-Pairing mit Bonding und LE Secure Connections.
  Während das Fenster offen ist, könnte sich ein anderes Gerät in Funkreichweite koppeln –
  deshalb kurz halten und mit `STATUS` (`bonds=`) prüfen.
- Die PIN nicht in Skripten oder Dateien speichern; `TYPE -` verwenden.

## Quellen

`main/esp_hid_gap.{c,h}` stammen aus dem ESP-IDF-Beispiel `esp_hid_device` (Unlicense OR CC0-1.0)
und sind angepasst (Sicherheit, Kopplungsfenster, Dauer-Advertising). `main/main.c` ist neu.
