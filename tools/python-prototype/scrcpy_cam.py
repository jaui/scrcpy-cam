"""
scrcpy-cam: Zeigt das Bild der UGREEN 25772 Capture-Karte (Handy-HDMI-Ausgang, z.B. Samsung DeX)
an und steuert das Handy ueber den scrcpy-Server (ADB ueber WLAN).

Video kommt latenzarm von der Capture-Karte, Maus/Tastatur gehen ueber das scrcpy-Steuerprotokoll
(scrcpy-server mit video=false, audio=false, control=true) direkt an das Display auf dem Handy.

Aufruf:
    python scrcpy_cam.py                       # automatisch: Kamera "UGREEN", einziges ADB-Geraet, DeX-Display
    python scrcpy_cam.py --serial 192.168.1.50:41234 --display-id 2
    python scrcpy_cam.py --pair 192.168.1.50:37123 123456     # Kopplung (Wireless Debugging)
    python scrcpy_cam.py --connect 192.168.1.50:41234
    python scrcpy_cam.py --no-control          # nur Bild anzeigen

Tasten im Fenster:
    F11 Vollbild | rechte Strg (RCtrl) + Taste = scrcpy-aehnliche Shortcuts:
        RCtrl+H Home, RCtrl+B / Esc Zurueck, RCtrl+S App-Uebersicht, RCtrl+P Power,
        RCtrl+Q Beenden
    Rechtsklick = Rechtsklick (DeX Kontextmenue), Mittelklick = Home, Mausrad = scrollen
"""

import argparse
import os
import random
import re
import socket
import struct
import subprocess
import sys
import threading
import time

import cv2
import numpy as np
import pygame

HERE = os.path.dirname(os.path.abspath(__file__))
SCRCPY_DIR = os.environ.get("SCRCPY_DIR", os.path.join(HERE, "scrcpy-win64-v5.0.1"))
ADB = os.path.join(SCRCPY_DIR, "adb.exe")
SERVER_JAR = os.path.join(SCRCPY_DIR, "scrcpy-server")
SERVER_VERSION = "5.0.1"
DEVICE_SERVER_PATH = "/data/local/tmp/scrcpy-server-cam.jar"

# --- Android Konstanten -------------------------------------------------------------------------
ACTION_DOWN, ACTION_UP, ACTION_MOVE, ACTION_HOVER_MOVE = 0, 1, 2, 7
BUTTON_PRIMARY, BUTTON_SECONDARY, BUTTON_TERTIARY = 1, 2, 4
POINTER_ID_MOUSE = 0xFFFFFFFFFFFFFFFF  # -1

META_SHIFT_ON, META_ALT_ON, META_CTRL_ON = 0x1, 0x2, 0x1000

KEYCODE_HOME, KEYCODE_BACK, KEYCODE_POWER = 3, 4, 26
KEYCODE_APP_SWITCH = 187

KEYMAP = {
    pygame.K_RETURN: 66, pygame.K_KP_ENTER: 160, pygame.K_BACKSPACE: 67, pygame.K_DELETE: 112,
    pygame.K_TAB: 61, pygame.K_ESCAPE: 111, pygame.K_HOME: 122, pygame.K_END: 123,
    pygame.K_PAGEUP: 92, pygame.K_PAGEDOWN: 93, pygame.K_UP: 19, pygame.K_DOWN: 20,
    pygame.K_LEFT: 21, pygame.K_RIGHT: 22, pygame.K_SPACE: 62, pygame.K_INSERT: 124,
    pygame.K_F1: 131, pygame.K_F2: 132, pygame.K_F3: 133, pygame.K_F4: 134, pygame.K_F5: 135,
    pygame.K_F6: 136, pygame.K_F7: 137, pygame.K_F8: 138, pygame.K_F9: 139, pygame.K_F10: 140,
    pygame.K_F12: 142,
}
# Buchstaben/Ziffern als Keycodes nur, wenn Strg/Alt gedrueckt ist (Shortcuts wie Strg+C);
# normaler Text geht als Text-Injection (beruecksichtigt deutsches Layout, Umlaute etc.)
LETTER_KEYCODES = {pygame.K_a + i: 29 + i for i in range(26)}
DIGIT_KEYCODES = {pygame.K_0 + i: 7 + i for i in range(10)}


def adb(*args, serial=None, check=True, capture=True, timeout=30):
    cmd = [ADB]
    if serial:
        cmd += ["-s", serial]
    cmd += list(args)
    r = subprocess.run(cmd, capture_output=capture, text=True, timeout=timeout)
    if check and r.returncode != 0:
        raise RuntimeError(f"adb {' '.join(args)} fehlgeschlagen:\n{r.stdout}{r.stderr}")
    return (r.stdout or "") + (r.stderr or "")


def pick_device(serial):
    if serial:
        return serial
    out = adb("devices")
    devs = [l.split()[0] for l in out.splitlines()[1:] if l.strip().endswith("device")]
    if not devs:
        # mDNS-Discovery (Wireless Debugging, bereits gekoppelt)
        out = adb("mdns", "services", check=False)
        for l in out.splitlines():
            m = re.search(r"_adb-tls-connect\._tcp\.?\s+(\S+:\d+)", l)
            if m:
                print("Verbinde (mDNS):", m.group(1))
                print(adb("connect", m.group(1), check=False).strip())
        out = adb("devices")
        devs = [l.split()[0] for l in out.splitlines()[1:] if l.strip().endswith("device")]
    if not devs:
        sys.exit("Kein ADB-Geraet gefunden. Erst koppeln/verbinden: --pair IP:PORT CODE bzw. --connect IP:PORT")
    if len(devs) > 1:
        sys.exit(f"Mehrere Geraete: {devs} -> mit --serial waehlen")
    return devs[0]


def list_displays(serial):
    """Gibt {display_id: (info, w, h)} zurueck (ueber scrcpy-server list_displays)."""
    adb("push", SERVER_JAR, DEVICE_SERVER_PATH, serial=serial)
    out = adb("shell", f"CLASSPATH={DEVICE_SERVER_PATH}", "app_process", "/", "com.genymobile.scrcpy.Server",
              SERVER_VERSION, "list_displays=true", "log_level=info", serial=serial, check=False)
    displays = {}
    for m in re.finditer(r"--display-id=(\d+)\s+\((\d+)x(\d+)\)(.*)", out):
        displays[int(m.group(1))] = (m.group(4).strip(), int(m.group(2)), int(m.group(3)))
    if not displays:
        print(out)
    return displays


def pick_display(serial, display_id):
    displays = list_displays(serial)
    for did, (name, w, h) in sorted(displays.items()):
        print(f"  Display {did}: {name!r} {w}x{h}")
    if display_id is None:
        others = [d for d in displays if d != 0]
        display_id = others[0] if others else 0
    if display_id in displays:
        _, w, h = displays[display_id]
    else:
        out = adb("shell", "wm", "size", serial=serial, check=False)
        m = re.search(r"(\d+)x(\d+)", out)
        w, h = (int(m.group(1)), int(m.group(2))) if m else (1920, 1080)
    print(f"Steuere Display {display_id} ({w}x{h})")
    return display_id, w, h


class ScrcpyControl:
    """Startet scrcpy-server nur mit Steuerkanal und sendet Steuer-Nachrichten."""

    def __init__(self, serial, display_id):
        self.serial = serial
        self.display_id = display_id
        self.scid = random.randint(0, 0x7FFFFFFF)
        self.sock = None
        self.proc = None
        self.lock = threading.Lock()

    def start(self):
        adb("push", SERVER_JAR, DEVICE_SERVER_PATH, serial=self.serial)
        socket_name = f"scrcpy_{self.scid:08x}"
        out = adb("forward", "tcp:0", f"localabstract:{socket_name}", serial=self.serial)
        self.port = int(out.strip().splitlines()[-1])
        self.socket_name = socket_name
        cmd = [ADB, "-s", self.serial, "shell", f"CLASSPATH={DEVICE_SERVER_PATH}", "app_process", "/",
               "com.genymobile.scrcpy.Server", SERVER_VERSION,
               f"scid={self.scid:08x}", "log_level=info", "video=false", "audio=false", "control=true",
               "tunnel_forward=true", f"display_id={self.display_id}", "send_device_meta=false",
               "clipboard_autosync=true", "cleanup=true", "power_on=false"]
        self.proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        threading.Thread(target=self._log_server, daemon=True).start()
        deadline = time.time() + 15
        while time.time() < deadline:
            if self.proc.poll() is not None:
                raise RuntimeError("scrcpy-server wurde beendet (siehe Log oben)")
            try:
                s = socket.create_connection(("127.0.0.1", self.port), timeout=2)
                b = s.recv(1)  # Dummy-Byte -> Verbindung zum Server steht
                if b:
                    s.settimeout(None)
                    s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                    self.sock = s
                    break
                s.close()
            except OSError:
                pass
            time.sleep(0.2)
        if not self.sock:
            raise RuntimeError("Keine Verbindung zum scrcpy-server")
        threading.Thread(target=self._drain, daemon=True).start()
        print("scrcpy-Steuerung verbunden.")

    def _log_server(self):
        for line in self.proc.stdout:
            print("[server]", line.rstrip())

    def _drain(self):
        # Geraete-Nachrichten (Clipboard etc.) lesen und verwerfen
        try:
            while self.sock and self.sock.recv(4096):
                pass
        except OSError:
            pass

    def send(self, data):
        if not self.sock:
            return
        with self.lock:
            try:
                self.sock.sendall(data)
            except OSError as e:
                print("Senden fehlgeschlagen:", e)
                self.sock = None

    def stop(self):
        if self.sock:
            try:
                self.sock.close()
            except OSError:
                pass
            self.sock = None
        if self.proc and self.proc.poll() is None:
            self.proc.terminate()
        try:
            adb("forward", "--remove", f"tcp:{self.port}", serial=self.serial, check=False)
        except Exception:
            pass

    # --- Nachrichten ---
    @staticmethod
    def _pos(x, y, w, h):
        return struct.pack(">iiHH", int(x), int(y), w, h)

    def touch(self, action, x, y, w, h, pressure, action_button, buttons):
        p = int(min(max(pressure, 0.0), 1.0) * 0xFFFF)
        self.send(struct.pack(">BBQ", 2, action, POINTER_ID_MOUSE) + self._pos(x, y, w, h)
                  + struct.pack(">HII", p, action_button, buttons))

    def scroll(self, x, y, w, h, hs, vs, buttons):
        def fx(v):
            v = max(-1.0, min(1.0, v / 16.0))
            return max(-0x8000, min(0x7FFF, int(v * 0x8000)))
        self.send(struct.pack(">B", 3) + self._pos(x, y, w, h) + struct.pack(">hhI", fx(hs), fx(vs), buttons))

    def key(self, action, keycode, meta=0, repeat=0):
        self.send(struct.pack(">BBiii", 0, action, keycode, repeat, meta))

    def press(self, keycode, meta=0):
        self.key(ACTION_DOWN, keycode, meta)
        self.key(ACTION_UP, keycode, meta)

    def text(self, s):
        b = s.encode("utf-8")
        self.send(struct.pack(">BI", 1, len(b)) + b)


class CamReader(threading.Thread):
    def __init__(self, index, width, height, fps):
        super().__init__(daemon=True)
        self.cap = cv2.VideoCapture(index, cv2.CAP_DSHOW)
        if not self.cap.isOpened():
            raise RuntimeError(f"Kamera {index} laesst sich nicht oeffnen")
        self.cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*"MJPG"))
        self.cap.set(cv2.CAP_PROP_FRAME_WIDTH, width)
        self.cap.set(cv2.CAP_PROP_FRAME_HEIGHT, height)
        self.cap.set(cv2.CAP_PROP_FPS, fps)
        self.cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)
        self.frame = None
        self.seq = 0
        self.running = True

    def run(self):
        while self.running:
            ok, f = self.cap.read()
            if ok:
                self.frame = f
                self.seq += 1
            else:
                time.sleep(0.01)
        self.cap.release()


def find_camera(name):
    try:
        import pygame.camera as pc
        pc.init()
        cams = pc.list_cameras()
        print("Kameras:", cams)
        for i, c in enumerate(cams):
            if name.lower() in str(c).lower():
                return i
    except Exception as e:
        print("Kamera-Suche fehlgeschlagen:", e)
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--serial", "-s", help="ADB-Seriennummer, z.B. 192.168.1.50:41234")
    ap.add_argument("--pair", nargs=2, metavar=("IP:PORT", "CODE"), help="Wireless Debugging koppeln")
    ap.add_argument("--connect", metavar="IP:PORT", help="adb connect")
    ap.add_argument("--display-id", type=int, help="Android Display-ID (Standard: erstes externes Display = DeX)")
    ap.add_argument("--cam", type=int, help="Kamera-Index (Standard: Geraet mit 'UGREEN' im Namen)")
    ap.add_argument("--cam-name", default="UGREEN")
    ap.add_argument("--size", default="1920x1080")
    ap.add_argument("--fps", type=int, default=60)
    ap.add_argument("--no-control", action="store_true", help="Nur Bild, keine Steuerung")
    ap.add_argument("--fullscreen", "-f", action="store_true")
    args = ap.parse_args()

    if args.pair:
        print(adb("pair", args.pair[0], args.pair[1], check=False))
    if args.connect:
        print(adb("connect", args.connect, check=False))
    if args.connect and not args.serial:
        args.serial = args.connect

    ctrl = None
    dev_w, dev_h = map(int, args.size.split("x"))
    if not args.no_control:
        serial = pick_device(args.serial)
        print("Geraet:", serial)
        display_id, dev_w, dev_h = pick_display(serial, args.display_id)
        ctrl = ScrcpyControl(serial, display_id)
        ctrl.start()

    cam_index = args.cam if args.cam is not None else find_camera(args.cam_name)
    cw, ch = map(int, args.size.split("x"))
    print("Kamera-Index:", cam_index)
    cam = CamReader(cam_index, cw, ch, args.fps)
    cam.start()

    pygame.init()
    pygame.display.set_caption("scrcpy-cam" + ("" if ctrl else " (nur Bild)"))
    flags = pygame.RESIZABLE
    screen = pygame.display.set_mode((1280, 720), flags)
    if args.fullscreen:
        pygame.display.toggle_fullscreen()
    pygame.key.start_text_input()
    clock = pygame.time.Clock()

    dest = pygame.Rect(0, 0, 1280, 720)
    buttons = 0
    last_seq = -1
    surf = None
    running = True

    def to_dev(pos):
        x = (pos[0] - dest.x) * dev_w / max(dest.w, 1)
        y = (pos[1] - dest.y) * dev_h / max(dest.h, 1)
        return min(max(int(x), 0), dev_w - 1), min(max(int(y), 0), dev_h - 1)

    while running:
        rctrl = pygame.key.get_mods() & pygame.KMOD_RCTRL
        for ev in pygame.event.get():
            if ev.type == pygame.QUIT:
                running = False
            elif ev.type == pygame.KEYDOWN and ev.key == pygame.K_F11:
                pygame.display.toggle_fullscreen()
            elif ev.type == pygame.KEYDOWN and rctrl and ev.key == pygame.K_q:
                running = False
            elif not ctrl:
                continue
            elif ev.type == pygame.MOUSEMOTION:
                x, y = to_dev(ev.pos)
                if buttons:
                    ctrl.touch(ACTION_MOVE, x, y, dev_w, dev_h, 1.0, 0, buttons)
                else:
                    ctrl.touch(ACTION_HOVER_MOVE, x, y, dev_w, dev_h, 0.0, 0, 0)
            elif ev.type in (pygame.MOUSEBUTTONDOWN, pygame.MOUSEBUTTONUP) and ev.button in (1, 2, 3):
                x, y = to_dev(ev.pos)
                b = {1: BUTTON_PRIMARY, 2: BUTTON_TERTIARY, 3: BUTTON_SECONDARY}[ev.button]
                if ev.type == pygame.MOUSEBUTTONDOWN:
                    buttons |= b
                    ctrl.touch(ACTION_DOWN, x, y, dev_w, dev_h, 1.0, b, buttons)
                else:
                    buttons &= ~b
                    ctrl.touch(ACTION_UP, x, y, dev_w, dev_h, 0.0, b, buttons)
            elif ev.type == pygame.MOUSEWHEEL:
                x, y = to_dev(pygame.mouse.get_pos())
                ctrl.scroll(x, y, dev_w, dev_h, -ev.precise_x, ev.precise_y, buttons)
            elif ev.type == pygame.TEXTINPUT:
                if not rctrl:
                    ctrl.text(ev.text)
            elif ev.type in (pygame.KEYDOWN, pygame.KEYUP):
                action = ACTION_DOWN if ev.type == pygame.KEYDOWN else ACTION_UP
                if rctrl:
                    if ev.type == pygame.KEYDOWN:
                        sc = {pygame.K_h: KEYCODE_HOME, pygame.K_b: KEYCODE_BACK, pygame.K_BACKSPACE: KEYCODE_BACK,
                              pygame.K_s: KEYCODE_APP_SWITCH, pygame.K_p: KEYCODE_POWER}.get(ev.key)
                        if sc:
                            ctrl.press(sc)
                    continue
                mods = ev.mod
                meta = 0
                if mods & pygame.KMOD_SHIFT:
                    meta |= META_SHIFT_ON
                if mods & pygame.KMOD_LCTRL:
                    meta |= META_CTRL_ON
                if mods & pygame.KMOD_LALT:
                    meta |= META_ALT_ON
                kc = KEYMAP.get(ev.key)
                if kc == 62:  # Leertaste kommt als Text, ausser mit Strg/Alt
                    kc = None if not (meta & (META_CTRL_ON | META_ALT_ON)) else kc
                if kc is None and (meta & (META_CTRL_ON | META_ALT_ON)) and not (mods & pygame.KMOD_RALT):
                    kc = LETTER_KEYCODES.get(ev.key) or DIGIT_KEYCODES.get(ev.key)
                if kc is not None:
                    ctrl.key(action, kc, meta, 1 if getattr(ev, "repeat", False) else 0)

        if cam.seq != last_seq and cam.frame is not None:
            last_seq = cam.seq
            f = cam.frame
            fh, fw = f.shape[:2]
            surf = pygame.image.frombuffer(cv2.cvtColor(f, cv2.COLOR_BGR2RGB).tobytes(), (fw, fh), "RGB")
            sw, sh = screen.get_size()
            scale = min(sw / fw, sh / fh)
            dest = pygame.Rect(0, 0, int(fw * scale), int(fh * scale))
            dest.center = (sw // 2, sh // 2)
            screen.fill((0, 0, 0))
            if dest.size == (fw, fh):
                screen.blit(surf, dest)
            else:
                screen.blit(pygame.transform.smoothscale(surf, dest.size), dest)
            pygame.display.flip()
        clock.tick(240)

    cam.running = False
    if ctrl:
        ctrl.stop()
    pygame.quit()


if __name__ == "__main__":
    main()
