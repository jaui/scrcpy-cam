# adbwifi – App „WLAN-Debugging“ (optional)

> **Optionales Zusatzmodul.** scrcpy-cam funktioniert ohne diese App; Wireless Debugging lässt sich
> immer auch von Hand einschalten ([`SETUP-PHONE.md`](../SETUP-PHONE.md), Abschnitt 5).

Kleine Android-App (≈ 40 KB, keine Bibliotheken, kein Internet-Zugriff), die **Wireless Debugging
wieder einschaltet**, wenn Android es abgeschaltet hat:

- **automatisch** (abschaltbar): nach einem Neustart (sobald das Handy entsperrt ist), wenn das WLAN
  zurückkommt und nach einem Wechsel des Access Points
- **Schnelleinstellungs-Kachel** „WLAN-Debugging“: zeigt den Zustand, schaltet um
- **App-Icon** auf dem Startbildschirm: Antippen schaltet ein und zeigt den Zustand

Sie ersetzt eine Automations-App wie MacroDroid für genau diese eine Aufgabe und hängt nicht von
deren Updates ab.

## Wie es funktioniert

Android hält den Zustand in der Systemeinstellung `adb_wifi_enabled` (Tabelle *Global*) und setzt
sie selbst auf 0, wenn WLAN ausgeschaltet wird, die Verbindung abreißt oder der Access Point
(BSSID) wechselt; nach einem Neustart ist sie ebenfalls 0. Die App schreibt wieder 1. Dafür braucht
sie das Recht `WRITE_SECURE_SETTINGS`, das nur per adb erteilt werden kann (einmalig, bleibt über
Neustarts und App-Updates erhalten).

Auslöser – ohne dauerhaft laufenden Dienst:

| Auslöser | wofür |
|---|---|
| JobScheduler-Job auf Änderungen von `adb_wifi_enabled` | Hauptauslöser: Android hat abgeschaltet (WLAN weg, AP-Wechsel) |
| einmaliger Job mit Bedingung „WLAN verbunden“ | wird geplant, wenn kein WLAN da ist; läuft, sobald es zurück ist |
| `BOOT_COMPLETED` | nach Neustart (kommt erst nach dem ersten Entsperren) |
| periodischer Job alle 15 min (nur im WLAN) | Sicherheitsnetz, beendet auch eine abgelaufene Pause |

Ein Netzwerk-Callback per PendingIntent eignet sich nicht: Android sendet ihn nur einmal und gibt
ihn 5 s später frei (AOSP `ConnectivityService`), im Emulator nachvollzogen.

Eingeschaltet wird nur bei verbundenem WLAN. **Ratenbegrenzung:** Die App startet innerhalb von
10 Minuten höchstens drei automatische Aktivierungen. Das begrenzt wiederholte Freigabedialoge
in unbekannten WLANs auch dann, wenn Android die Auslöser verzögert. Häufige WLAN-Abbrüche können
das Limit ebenfalls erreichen. Danach wartet die App auf einen weiteren Auslöser. Manuelles
Einschalten bleibt möglich.

**Kachel „aus“** pausiert die Automatik für eine Stunde (oder bis zum Neustart); die Kachel zeigt
„pausiert bis …“. Dauerhaft abschalten lässt sie sich nur in der App (Schalter „Automatisch wieder
einschalten“). Zeitbasiert, damit ein versehentliches Antippen nie dauerhaft wirkt.

## Bauen

Voraussetzungen: Android-SDK (Plattform 37, z. B. aus Android Studio), JDK 21.

```powershell
tools\adbwifi-android\build.ps1             # Tests, Lint, APK nach ..\dist\adbwifi.apk
tools\adbwifi-android\build.ps1 -Install    # zusätzlich installieren und einrichten (adb verbunden)
```

**Signieren:** Der Release-Build wird mit einem eigenen Schlüssel signiert, wenn eine
`keystore.properties` gefunden wird (Standard: `..\keystore\adbwifi-keystore.properties` neben dem
Repository-Ordner, oder Umgebungsvariable `ADBWIFI_KEYSTORE_PROPERTIES`; Schlüssel `storeFile`,
`storePassword`, `keyAlias`, `keyPassword`). Ohne sie entsteht eine Debug-APK.
Keystore und Passwort **sichern**: Ein Update muss mit demselben Schlüssel signiert sein, sonst muss
die App deinstalliert, neu installiert und das Recht neu erteilt werden. Beides gehört nie ins Repository.

## Einrichten am Handy

`build.ps1 -Install` erledigt die Schritte 1–3. Von Hand:

1. `adb install -r adbwifi.apk`
2. `adb shell pm grant com.github.jaui.adbwifi android.permission.WRITE_SECURE_SETTINGS`
3. `adb shell cmd deviceidle whitelist +com.github.jaui.adbwifi` (Akku-Optimierung aus) und die App
   einmal öffnen.
4. Kachel hinzufügen: Schnelleinstellungen ganz aufziehen → Bearbeiten (Stift/⋮) → „WLAN-Debugging“
   in die Leiste ziehen.
5. Optional: App-Icon auf den Startbildschirm legen.

**Akku-Optimierung ist wichtig:** Eine App, die nie geöffnet wird, stuft Android (und Samsung
„Apps im Tiefschlaf“) herunter; ihre Jobs laufen dann erst nach Stunden. Die App zeigt an, ob sie
ausgenommen ist, und hat einen Knopf dafür. Bei Samsung zusätzlich: Akku → Hintergrundnutzungslimits
→ „Nie in den Standby versetzte Apps“.

## Grenzen

- **Vertrauenswürdiges Netz:** Wireless Debugging startet nur in einem WLAN, das am Handy einmal mit
  „Immer in diesem Netzwerk zulassen“ bestätigt wurde. Android merkt sich das **pro Access Point
  (BSSID)**, nicht pro WLAN-Name: Bei Mesh-WLAN oder mehreren Access Points muss jeder einmal
  bestätigt werden – **auch die beiden Funkbänder (2,4 und 5 GHz) eines Routers** haben getrennte
  BSSIDs. Auf Testgerät A kam nach einem Neustart die Rückfrage, weil sich das Handy diesmal mit dem
  anderen Band verbunden hatte. Das kann keine App übernehmen. Abhilfe: beim Einrichten jede
  Rückfrage mit „Immer zulassen“ bestätigen, bis keine mehr kommt; oder am Router für das Handy nur
  ein Band anbieten (z. B. eigene SSID nur für 5 GHz).
- **Nach einem Neustart** laufen Apps erst nach dem ersten Entsperren (PIN). Danach schaltet die App
  sofort ein. PIN ohne Eingriff am Gerät: optional per Bluetooth-Tastatur ([`bt-hid`](../bt-hid)).
- Die Kachel und das Icon schalten nur ein, wenn WLAN verbunden ist.

## Sicherheit

- Die App kann mit dem erteilten Recht nur Systemeinstellungen schreiben; sie schreibt ausschließlich
  `adb_wifi_enabled`. Sie hat keinen Internet-Zugriff und protokolliert keine Netzwerknamen.
- Dauerhaft aktives Wireless Debugging macht den adb-Schlüssel des PCs zu einem Generalschlüssel für
  das Handy – siehe [`SETUP-PHONE.md`](../SETUP-PHONE.md), Abschnitt 6.

## Vergleich mit MacroDroid

Untersucht wurde MacroDroid 6.1.5 (APK vom Testgerät, mit jadx dekompiliert; hier nur das Verhalten,
kein Code):

- MacroDroid hat **keine eigene Funktion** für Wireless Debugging; `adb_wifi_enabled` kommt in der App
  nicht vor. Die Lösung aus [`SETUP-PHONE.md`](../SETUP-PHONE.md) nutzt die allgemeine Aktion
  „Systemeinstellung ändern“, in die man Tabelle *Global* und den Schlüssel selbst einträgt.
- Diese Aktion schreibt mit Root per `settings put global …`, sonst über die Android-API
  (`Settings.Global.putInt`) mit dem per adb erteilten `WRITE_SECURE_SETTINGS` – **derselbe
  Mechanismus** wie diese App.
- Um Auslöser zu erkennen, läuft MacroDroid als **dauerhafter Vordergrund-Dienst** mit ständiger
  Benachrichtigung; dazu kommen Firebase-Messaging und ein WebView-Prozess. Das Manifest fordert
  119 Berechtigungen an; eingebettete Werbe-/Analyse-Bibliotheken lesen u. a. aus, ob Debugging
  eingeschaltet ist. Auf Testgerät B belegte MacroDroid ≈ 225 MB (PSS, großteils ausgelagert).

Diese App macht nur das eine, ohne Dienst, Bibliotheken oder Internet-Recht, mit einem Bruchteil
der Größe (≈ 40 KB statt ≈ 90 MB) und ohne Abhängigkeit von Updates eines Drittanbieters.
Zusätzlich reagiert sie sofort auf den Wechsel des Access Points statt auf ein Zeitintervall.

## Test im Emulator (Android 16, API 36)

| Test | Ergebnis |
|---|---|
| Installation mit `build.ps1 -Install` (Recht, Akku-Ausnahme, Start) | ✅ |
| Erstes Einschalten in nicht bestätigtem WLAN | Android fragt nach; 3 Versuche, dann Ratenbegrenzung (keine Dauerschleife) ✅ |
| Android setzt `adb_wifi_enabled=0` (4× im Abstand von 12 s) | ✅ jeweils nach ≈ 0,95 s wieder an (gemessen mit einer früheren Zählweise; die aktuelle Version begrenzt ab dem 4. automatischen Versuch innerhalb von 10 min) |
| WLAN aus → an | ✅ 6,3 s nach dem Einschalten des WLANs wieder an |
| Kachel aus / an | ✅ „pausiert bis …“, Automatik greift nicht; an → sofort an |
| Schalter „Automatisch“ aus | ✅ bleibt aus, keine Jobs mehr geplant |
| Neustart | ✅ 39 s nach dem Neustart-Befehl wieder an (`BOOT_COMPLETED`) |
| Ressourcen | kein Dienst, Prozess nur zwischengespeichert (≈ 5 MB PSS), Standby-Bucket „exempted“ |

## Test auf Testgerät A (Android 13)

| Test | Ergebnis |
|---|---|
| `build.ps1 -Install` | ✅ |
| `adb_wifi_enabled=0` per adb (trennt adb) | ✅ nach ≈ 10 s wieder per adb erreichbar (neuer Port, per mDNS gefunden) |
| Kachel hinzufügen, aus/an | ✅ |
| WLAN aus → an | ✅ Wireless Debugging kommt von selbst zurück; adb verbindet der Launcher neu |
| Neustart → PIN von Hand | ✅ nach dem Entsperren wieder an – hier erst nach der Rückfrage „In diesem Netzwerk zulassen?“, weil das Handy sich mit dem anderen Funkband (neue BSSID) verbunden hatte; nach „Immer zulassen“ ohne weiteren Eingriff |

## Test auf Testgerät B (Android 17)

| Test | Ergebnis |
|---|---|
| `build.ps1 -Install` | ✅ Recht erteilt, Akku-Ausnahme (Standby-Bucket „exempted“), Start ok |
| `adb_wifi_enabled=0` per adb (trennt adb) | ✅ Job lief ≈ 0,5 s danach; Wireless Debugging neu gestartet (neuer Port), adb über denselben mDNS-Namen binnen ≈ 2 s wieder verbunden |

| Kachel hinzufügen, aus/an | ✅ aus trennt adb, an startet Wireless Debugging neu (neuer Port), adb per mDNS wieder verbunden |

| WLAN aus → an | ✅ Wireless Debugging kommt von selbst zurück (neuer Port, per mDNS erreichbar) |

Noch offen: Neustart auf Testgerät B; Wechsel zwischen Mesh-Access-Points.

## Aufbau

```
app/src/main/java/com/github/jaui/adbwifi/
  Policy.kt              Entscheidung einschalten ja/nein, Ratenbegrenzung (Unit-Tests)
  AdbWifi.kt             Einstellung lesen/schreiben, WLAN-Zustand, Pause, Einstellungen
  Triggers.kt            Jobs planen/entfernen
  Receivers.kt           BootReceiver, EnableJobService
  AdbWifiTileService.kt  Schnelleinstellungs-Kachel
  MainActivity.kt        App-Icon: einschalten + Status
```
