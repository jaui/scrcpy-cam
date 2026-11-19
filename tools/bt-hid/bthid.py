#!/usr/bin/env python3
"""Steuert die scrcpy-cam bt-hid Firmware (BLE-Tastatur) ueber die serielle Schnittstelle.

Beispiele:
    python bthid.py --port COM4 STATUS
    python bthid.py --port COM4 "PAIR 120"
    python bthid.py --port COM4 "TYPE 1234" "KEY ENTER"
    python bthid.py "KEY ENTER" "SLEEP 1" "TYPE -" "KEY ENTER"   # wecken + entsperren
    (ENTER weckt und oeffnet das PIN-Feld; ein leeres ENTER zaehlt nicht als Fehlversuch.
     "TYPE -" fragt den Text verdeckt ab, damit die PIN nicht in der Shell-History landet.)
    python bthid.py --port COM4 --listen 30        # Ereignisse 30 s lang ausgeben

Ohne --port wird der erste Espressif-USB-Port (VID 303A) bzw. ein CH34x/CP210x gesucht.
Benoetigt pyserial (z. B. aus der esptool-venv).
"""
import argparse
import getpass
import sys
import time

import serial
import serial.tools.list_ports

KNOWN_VIDS = {0x303A, 0x1A86, 0x10C4}


def find_port():
    for p in serial.tools.list_ports.comports():
        if p.vid in KNOWN_VIDS:
            return p.device
    return None


def open_port(name):
    s = serial.Serial()
    s.port = name
    s.baudrate = 115200
    s.timeout = 0.1
    # DTR/RTS nicht setzen: beim USB-Serial/JTAG des ESP32 loesen bestimmte
    # Pegelfolgen einen Reset bzw. den Download-Modus aus
    s.dtr = False
    s.rts = False
    s.open()
    return s


def read_lines(s, until, stop_on_reply=True):
    """Liest Zeilen bis zur Frist; gibt (Antwortzeile, Ereignisse) zurueck."""
    buf = b""
    events = []
    while time.time() < until:
        buf += s.read(256)
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            text = line.decode("utf-8", "replace").strip()
            if not text:
                continue
            if text.startswith(("OK", "ERR")) and stop_on_reply:
                return text, events
            events.append(text)
            if not stop_on_reply:
                print(text, flush=True)
    return None, events


def command(s, cmd, timeout=10.0):
    s.reset_input_buffer()
    s.write((cmd + "\n").encode("ascii"))
    s.flush()
    return read_lines(s, time.time() + timeout)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="serieller Port, z. B. COM4")
    ap.add_argument("--listen", type=float, default=0, help="danach N Sekunden Ereignisse ausgeben")
    ap.add_argument("commands", nargs="*", help="Befehle (je Argument eine Zeile)")
    args = ap.parse_args()

    port = args.port or find_port()
    if not port:
        sys.exit("Kein ESP32-Port gefunden (--port angeben)")

    with open_port(port) as s:
        rc = 0
        for cmd in args.commands:
            # SLEEP n: Pause auf dem PC (z. B. nach dem Wecken bis zum PIN-Feld)
            if cmd.upper().startswith("SLEEP"):
                time.sleep(float(cmd.split()[1]) if len(cmd.split()) > 1 else 1.0)
                continue
            # TYPE -: Text verdeckt abfragen (PIN nicht in der Befehlszeile/History)
            if cmd.strip().upper() == "TYPE -":
                cmd = "TYPE " + getpass.getpass("Text (wird nicht angezeigt): ")
            # Zeit fuer TYPE: ~45 ms pro Zeichen
            timeout = 10.0 + (len(cmd) * 0.06 if cmd.upper().startswith("TYPE") else 0)
            reply, events = command(s, cmd, timeout)
            for e in events:
                print(e)
            # Inhalt von TYPE nicht wiederholen (kann eine PIN sein)
            shown = "TYPE ***" if cmd.upper().startswith("TYPE ") else cmd
            print(f"{shown} -> {reply or 'keine Antwort'}")
            if not reply or reply.startswith("ERR"):
                rc = 1
        if args.listen:
            read_lines(s, time.time() + args.listen, stop_on_reply=False)
        sys.exit(rc)


if __name__ == "__main__":
    main()
