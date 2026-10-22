#!/usr/bin/env python3
"""Boot Safronov OS in headless QEMU, type keys and take screenshots.

Example:
  tools/qemu_shot.py --mode uefi --out shots sleep:6 shot:boot type:'help\\n' shot:help

Steps: sleep:SECONDS  type:TEXT  key:QEMU_KEY  shot:NAME  hmp:MONITOR_COMMAND
       moveto:X,Y  click  rclick  down  up  drag:X,Y  wheel:N
Mouse steps assume the kernel was booted with "noaccel" and the pointer
starts at the screen centre (--screen WxH).
"""
import argparse
import os
import socket
import subprocess
import sys
import tempfile
import time

KEYS = {" ": "spc", "\n": "ret", "-": "minus", "=": "equal", ".": "dot", ",": "comma", "/": "slash",
        ";": "semicolon", "'": "apostrophe", "[": "bracket_left", "]": "bracket_right", "\\": "backslash",
        "`": "grave_accent", "\t": "tab"}
SHIFTED = {">": "dot", "<": "comma", "(": "9", ")": "0", "*": "8", "+": "equal", "^": "6", "_": "minus",
           ":": "semicolon", '"': "apostrophe", "!": "1", "@": "2", "#": "3", "$": "4", "%": "5",
           "&": "7", "?": "slash", "{": "bracket_left", "}": "bracket_right", "|": "backslash", "~": "grave_accent"}


def qemu_key(ch):
    if ch.isascii() and ch.isalpha():
        return ("shift-" if ch.isupper() else "") + ch.lower()
    if ch.isdigit():
        return ch
    if ch in KEYS:
        return KEYS[ch]
    if ch in SHIFTED:
        return "shift-" + SHIFTED[ch]
    raise ValueError(f"no key for {ch!r}")


class Monitor:
    def __init__(self, path):
        for _ in range(100):
            try:
                self.s = socket.socket(socket.AF_UNIX)
                self.s.connect(path)
                break
            except OSError:
                time.sleep(0.1)
        self.s.settimeout(2)
        self.read()

    def read(self):
        data = b""
        try:
            while not data.endswith(b"(qemu) "):
                chunk = self.s.recv(4096)
                if not chunk:
                    break
                data += chunk
        except socket.timeout:
            pass
        return data.decode(errors="replace")

    def cmd(self, c):
        self.s.sendall((c + "\n").encode())
        return self.read()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mode", choices=["bios", "uefi", "usb-bios", "usb-uefi"], default="bios")
    ap.add_argument("--iso", default="build/safronov-os.iso")
    ap.add_argument("--out", default="shots")
    ap.add_argument("--mem", default="512M")
    ap.add_argument("--extra", default="")
    ap.add_argument("--screen", default="1024x768")
    ap.add_argument("steps", nargs="*")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    sock = os.path.join(tempfile.mkdtemp(), "mon.sock")
    cmd = ["qemu-system-x86_64", "-m", a.mem, "-display", "none", "-vga", "std",
           "-monitor", f"unix:{sock},server,nowait", "-serial", os.environ.get("QSERIAL", "null"), "-no-reboot"]
    if "uefi" in a.mode:
        cmd += ["-bios", "/usr/share/ovmf/OVMF.fd"]
    if a.mode.startswith("usb"):
        cmd += ["-drive", f"if=none,id=stick,format=raw,file={a.iso}", "-device", "qemu-xhci",
                "-device", "usb-storage,drive=stick,bootindex=0"]
    else:
        cmd += ["-cdrom", a.iso]
    cmd += a.extra.split()
    q = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    sw, sh = map(int, a.screen.split("x"))
    pos = [sw // 2, sh // 2]

    def move(m, x, y):
        dx, dy = x - pos[0], y - pos[1]
        while dx or dy:
            sx, sy = max(-100, min(100, dx)), max(-100, min(100, dy))
            m.cmd(f"mouse_move {sx} {sy}")
            dx -= sx
            dy -= sy
        pos[0], pos[1] = x, y
        time.sleep(0.1)

    try:
        m = Monitor(sock)
        for step in a.steps:
            kind, _, arg = step.partition(":")
            if q.poll() is not None:
                print(f"qemu exited with {q.returncode}", file=sys.stderr)
                break
            if kind == "sleep":
                time.sleep(float(arg))
            elif kind == "key":
                m.cmd(f"sendkey {arg}")
                time.sleep(0.15)
            elif kind == "type":
                text = arg.replace("\\n", "\n").replace("\\t", "\t")
                for ch in text:
                    m.cmd(f"sendkey {qemu_key(ch)}")
                    time.sleep(0.06)
            elif kind == "moveto":
                x, y = map(int, arg.split(","))
                move(m, x, y)
            elif kind in ("click", "rclick"):
                b = 1 if kind == "click" else 2
                m.cmd(f"mouse_button {b}")
                time.sleep(0.08)
                m.cmd("mouse_button 0")
                time.sleep(0.15)
            elif kind == "down":
                m.cmd("mouse_button 1")
                time.sleep(0.1)
            elif kind == "up":
                m.cmd("mouse_button 0")
                time.sleep(0.1)
            elif kind == "drag":
                x, y = map(int, arg.split(","))
                m.cmd("mouse_button 1")
                time.sleep(0.1)
                tx, ty = x, y
                for i in range(1, 9):
                    move(m, pos[0] + (tx - pos[0]) // (9 - i), pos[1] + (ty - pos[1]) // (9 - i))
                move(m, tx, ty)
                m.cmd("mouse_button 0")
                time.sleep(0.1)
            elif kind == "wheel":
                m.cmd(f"mouse_move 0 0 {int(arg)}")
                time.sleep(0.1)
            elif kind == "hmp":
                print(m.cmd(arg).strip())
            elif kind == "shot":
                path = os.path.abspath(os.path.join(a.out, arg + ".png"))
                print(m.cmd(f"screendump {path} -f png").strip().splitlines()[-1:] and path)
            else:
                raise SystemExit(f"bad step {step}")
        print("qemu still running" if q.poll() is None else f"qemu exited: {q.returncode}")
    finally:
        if q.poll() is None:
            q.kill()
        err = q.stderr.read().decode(errors="replace").strip()
        if err:
            print(err, file=sys.stderr)


if __name__ == "__main__":
    main()
